"""
Pytest fixtures + CLI options for CYD-Dashboard functional tests.

These tests drive a running device over the network (HTTP + WebSocket).
They do not simulate device code — they exercise the actual firmware.

Usage:
    pytest test/functional --host 192.168.1.212
    pytest test/functional --host cyd-dashboard.local -k diag
"""
import json
import time
import pytest
import requests

try:
    import websocket  # type: ignore
except ImportError:  # pragma: no cover
    websocket = None


def pytest_addoption(parser):
    parser.addoption(
        "--host", default="cyd-dashboard.local",
        help="Device hostname or IP (default: cyd-dashboard.local)",
    )
    parser.addoption(
        "--http-timeout", type=float, default=8.0,
        help="HTTP request timeout in seconds (default: 8)",
    )


@pytest.fixture(scope="session")
def host(request):
    return request.config.getoption("--host")


@pytest.fixture(scope="session")
def base_url(host):
    return f"http://{host}"


@pytest.fixture(scope="session")
def ws_url(host):
    return f"ws://{host}/ws"


@pytest.fixture(scope="session")
def http_timeout(request):
    return request.config.getoption("--http-timeout")


def fetch_diag(host: str, timeout: float = 8) -> dict:
    """Fetch /api/_diag and return parsed JSON, or {'_error': ...} on failure."""
    try:
        r = requests.get(f"http://{host}/api/_diag", timeout=timeout)
        if r.status_code != 200:
            return {"_error": f"HTTP {r.status_code}"}
        return r.json()
    except Exception as e:
        return {"_error": str(e)}


@pytest.fixture(scope="session")
def diag_before_session(host):
    """One-shot baseline diag — used to check for mid-session reboot."""
    d = fetch_diag(host, timeout=15)
    if "_error" in d:
        pytest.skip(f"Device unreachable at session start: {d['_error']}")
    return d


def _ws_fetch(ws_url: str, what: str, timeout: float = 12.0):
    """Fetch one `set_<what>` frame via WS. Returns items or None on failure."""
    if websocket is None:
        return None
    try:
        ws = websocket.create_connection(ws_url, timeout=timeout)
    except Exception:
        return None
    try:
        ws.settimeout(timeout)
        ws.send(json.dumps({"action": "get", "what": what}))
        for _ in range(8):
            try:
                raw = ws.recv()
            except Exception:
                return None
            try:
                m = json.loads(raw)
            except Exception:
                continue
            if m.get("action") == f"set_{what}":
                return m.get("items")
        return None
    finally:
        try: ws.close()
        except Exception: pass


def _svc_mode_status(host: str, timeout: float = 5.0):
    """Returns dict {active, owner} from /api/health?act=service_mode, or None."""
    try:
        r = requests.get(f"http://{host}/api/health?act=service_mode", timeout=timeout)
        if r.status_code == 200:
            return r.json()
    except Exception:
        pass
    return None


def _ws_save_all(ws_url: str, host: str, snapshot: dict, timeout: float = 15.0) -> bool:
    """Restore snapshot via save actions (service mode required).

    Skips if another client currently owns service mode (human user mid-session).
    Caller should retry after grace window expires.
    """
    st = _svc_mode_status(host)
    if st and st.get("active") and st.get("owner"):
        print(f"[state-guard] skipping restore — service mode held by client {st.get('owner')}")
        return False

    if websocket is None:
        return False
    try:
        ws = websocket.create_connection(ws_url, timeout=timeout)
    except Exception:
        return False
    try:
        ws.settimeout(timeout)
        try:
            ws.send(json.dumps({"action": "enter_service_mode"}))
        except Exception:
            return False
        entered = False
        for _ in range(10):
            try:
                m = json.loads(ws.recv())
            except Exception:
                return False
            if m.get("action") == "service_mode_entered":
                entered = True; break
            if m.get("action") == "service_mode_denied":
                return False
        if not entered:
            return False

        ok_all = True
        # settings first (brightness etc.), then profiles/widgets/macros
        for what in ("settings", "profiles", "widgets", "macros"):
            items = snapshot.get(what)
            if items is None:
                continue
            rid = f"restore-{what}"
            try:
                ws.send(json.dumps({"action":"save","what":what,"items":items,"req_id":rid}))
            except Exception:
                ok_all = False; break
            got = False
            for _ in range(10):
                try:
                    m = json.loads(ws.recv())
                except Exception:
                    ok_all = False; break
                if m.get("req_id") == rid:
                    if not m.get("ok"):
                        ok_all = False
                    got = True; break
            if not got:
                ok_all = False
            time.sleep(0.8)
        try:
            ws.send(json.dumps({"action": "exit_service_mode"}))
        except Exception:
            pass
        return ok_all
    finally:
        try: ws.close()
        except Exception: pass


@pytest.fixture(autouse=True, scope="session")
def _session_state_guard(host, ws_url, request):
    """Snapshot macros/widgets/settings/profiles at session start; restore at end.

    Protects user data from destructive tests (test_save_*, import flows).
    Skipped gracefully if websocket library missing or device unreachable.
    """
    snap = {}
    for what in ("macros", "widgets", "settings", "profiles"):
        data = _ws_fetch(ws_url, what)
        if data is not None:
            snap[what] = data
    if not snap:
        yield
        return
    print(f"\n[state-guard] snapshot: "
          f"macros={len(snap.get('macros',[]))} "
          f"widgets={len(snap.get('widgets',[]))} "
          f"profiles={len(snap.get('profiles',[]))} "
          f"settings={len(snap.get('settings',{}))}")

    # Clear dynamic state before tests run.
    # URL widgets poll the net on background timers and fragment heap on no-PSRAM
    # boards, causing flaky HTTP failures in subsequent tests. Clear them now;
    # state-guard will restore everything at session end.
    _ws_save_all(ws_url, host, {"macros": [], "widgets": [], "profiles": []})
    time.sleep(2)  # let widget timers disarm
    print("[state-guard] cleared dynamic state (widgets/macros/profiles)")

    yield
    # restore at session teardown
    for attempt in range(3):
        if _ws_save_all(ws_url, host, snap):
            print(f"[state-guard] restored state (attempt {attempt+1})")
            return
        time.sleep(3)
    print("[state-guard] WARN: state restore failed after 3 attempts")


@pytest.fixture(autouse=True, scope="session")
def _session_reboot_guard(diag_before_session, host, request):
    """Run after entire session — warn if uptime regressed unexpectedly.

    NOTE: this used to fail the run on any uptime regression, but that fought
    with test_reboot.py which intentionally reboots the device. Now it only
    warns (printed to the session report); individual tests are responsible
    for their own crash detection via the serial log scan.
    """
    start_up = diag_before_session.get("uptime_ms", 0) // 1000
    yield
    end_diag = {}
    for _ in range(3):
        time.sleep(3)
        end_diag = fetch_diag(host, timeout=10)
        if "_error" not in end_diag:
            break
    if "_error" in end_diag:
        return
    end_up = end_diag.get("uptime_ms", 0) // 1000
    if end_up < start_up:
        print(f"\n[INFO] Device uptime reset during session "
              f"(before={start_up}s, after={end_up}s). "
              f"Expected if test_reboot ran; otherwise investigate crash log.")
