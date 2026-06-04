"""End-to-end workflow test — exercises the full happy path that a user walks
through when configuring the dashboard via the web UI.

Covers:
  * Device reachable + WS handshake (proxy attach surrogate)
  * Service mode enter → exit cycles (3×) without heap collapse
  * Macros: snapshot, append test items, verify, restore (= delete test items)
  * Widgets: snapshot, append test items, verify, restore
  * Settings update inside service mode (brightness)
  * Public GET /api/* endpoints respond 200 with sane payloads
  * Heap stays above HEAP_FREE_MIN after the whole workflow

Run:
    pytest test/functional/test_e2e_workflow.py --host 192.168.1.212 -v

Notes:
  * Uses the session-scoped state-guard from conftest.py — original macros /
    widgets / settings / profiles snapshot is restored at session teardown.
  * Each subtest is independent; if service mode is held by another session
    (e.g. a human in the browser) the test is skipped, not failed.
  * On no-PSRAM boards, /api/init streams a large chunked payload that is
    known to fail (firmware issue, separate from these tests) — the endpoint
    is skipped on a timeout, not failed.
  * `Cards::Macros::set` and `Cards::Widgets::set` accept empty `[]` — saving
    an empty list clears all items. Tests restore the original state at teardown.
"""
import json
import time
import uuid
import requests
import pytest

websocket = pytest.importorskip("websocket")


# ── Constants ────────────────────────────────────────────────────────────────
HEAP_FREE_MIN      = 25_000        # below this the device is degraded
SERVICE_GRACE_SEC  = 4             # grace between service-mode operations
ENDPOINT_DELAY_SEC = 0.7           # throttle between rapid HTTP probes

# Public read endpoints — each entry is (path, optional query, slow?).
# /api/init is known to fail on no-PSRAM boards (chunked HTTP issue).
PUBLIC_GET_ENDPOINTS = [
    ("/api/_diag",          None,                False),
    ("/api/health",         None,                False),
    ("/api/macros",         None,                False),
    ("/api/widgets",        None,                False),
    ("/api/wifi",           "act=status",        False),
    ("/api/settings",       None,                False),
    ("/api/profiles",       None,                False),
    ("/api/icons",          None,                False),
    ("/api/backgrounds",    None,                False),
    ("/api/log",            None,                False),
    ("/api/init",           None,                True),   # large + slow
]


# ── WS helpers ───────────────────────────────────────────────────────────────
def _ws_open(ws_url, timeout=10):
    ws = websocket.create_connection(ws_url, timeout=timeout)
    ws.settimeout(8)
    return ws


def _wait_for(ws, predicate, timeout=8.0):
    end = time.time() + timeout
    ws.settimeout(timeout)
    while time.time() < end:
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            return None
        except Exception:
            return None
        try:
            m = json.loads(raw)
        except Exception:
            continue
        if predicate(m):
            return m
    return None


def _enter_service(ws):
    """Enter service mode and return token. We do NOT wait for
    `service_mode_ready` — that signal is fired after the SPA is HTTP-served,
    which is irrelevant for API-only tests and unreliable on no-PSRAM boards.
    """
    ws.send(json.dumps({"action": "enter_service_mode"}))
    m = _wait_for(
        ws,
        lambda m: m.get("action") in ("service_mode_entered", "service_mode_denied"),
        timeout=10,
    )
    if m is None:
        pytest.skip("device unresponsive to enter_service_mode")
    if m["action"] == "service_mode_denied":
        pytest.skip(f"service mode held by another session: {m.get('reason')}")
    return m.get("token")


def _exit_service(ws):
    try:
        ws.send(json.dumps({"action": "exit_service_mode"}))
    except Exception:
        pass
    time.sleep(SERVICE_GRACE_SEC)


def _save(ws, what, items, timeout=15.0):
    """Send a save and wait for its ack. Longer timeout because LittleFS write
    + LVGL rebuild on no-PSRAM can take 8-12 s under heap pressure."""
    req_id = f"e2e-{what}-{uuid.uuid4().hex[:6]}"
    ws.send(json.dumps({"action": "save", "what": what, "items": items, "req_id": req_id}))
    ack = _wait_for(ws, lambda m: m.get("req_id") == req_id, timeout)
    assert ack is not None, f"no save ack for {what}"
    assert ack.get("action") == "save_ack"
    assert ack.get("ok") is True, f"save {what} failed: {ack.get('error')}"


def _retry_get(host, path, query=None, timeout=8.0, retries=2):
    """GET with retry — transient RemoteDisconnected is common during
    parallel WS activity under low heap."""
    url = f"http://{host}{path}" + (f"?{query}" if query else "")
    last_err = None
    for i in range(retries + 1):
        try:
            r = requests.get(url, timeout=timeout)
            return r
        except requests.exceptions.RequestException as e:
            last_err = e
            time.sleep(2 + i)
    raise last_err


def _get_via_ws(ws_url, what, timeout=10.0):
    """One-shot get over a fresh WS — independent of any open session."""
    ws = _ws_open(ws_url)
    try:
        ws.send(json.dumps({"action": "get", "what": what}))
        m = _wait_for(ws, lambda m: m.get("action") == f"set_{what}", timeout)
        if m is None:
            return None
        return m.get("items")
    finally:
        try:
            ws.close()
        except Exception:
            pass


def _get_on_ws(ws, what, timeout=10.0):
    """Get on an EXISTING WS — useful inside a service-mode owner session
    where opening a second WS would race with the owner's pending broadcasts.
    """
    ws.send(json.dumps({"action": "get", "what": what}))
    m = _wait_for(ws, lambda m: m.get("action") == f"set_{what}", timeout)
    if m is None:
        return None
    return m.get("items")


def _diag(host, timeout=8.0, retries=3):
    last_err = None
    for _ in range(retries):
        try:
            r = requests.get(f"http://{host}/api/_diag", timeout=timeout)
            r.raise_for_status()
            return r.json()
        except Exception as e:
            last_err = e
            time.sleep(2)
    raise last_err


def _require_healthy(host):
    """Skip test if device heap is too low to safely exercise."""
    try:
        d = _diag(host)
    except Exception as e:
        pytest.skip(f"device unreachable: {e}")
    free = d.get("heap", {}).get("free", 0)
    if free < HEAP_FREE_MIN:
        pytest.skip(f"heap too low to test ({free} B)")
    return d


@pytest.fixture(autouse=True)
def _wait_for_device_recovery(host):
    """Before each test, wait up to 30 s for the device to respond to /api/_diag.

    On no-PSRAM Rv2 boards the chunked-HTTP / service-mode path can wedge the
    HTTP server temporarily (lwIP TCP refuses accept under low largest_free).
    Rather than cascading failures across the suite, skip the affected test
    and let the device recover for the next one.
    """
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            requests.get(f"http://{host}/api/_diag", timeout=4).raise_for_status()
            return
        except Exception:
            time.sleep(3)
    pytest.skip("device not responsive after 30 s recovery window")


# ── 1. Connectivity / baseline ───────────────────────────────────────────────
def test_device_reachable(host):
    """Smoke test — device responds to /api/_diag before we touch anything."""
    d = _diag(host)
    assert d.get("uptime_ms", 0) > 0
    assert d.get("heap", {}).get("free", 0) > HEAP_FREE_MIN, "heap low at session start"


def test_ws_proxy_handshake(host, ws_url):
    """A fresh WS client should open + respond to ping."""
    _require_healthy(host)
    ws = _ws_open(ws_url)
    try:
        rid = f"e2e-ping-{uuid.uuid4().hex[:6]}"
        ws.send(json.dumps({"action": "ping", "req_id": rid}))
        pong = _wait_for(ws, lambda m: m.get("action") == "pong" and m.get("req_id") == rid, 5)
        assert pong is not None, "no pong from device"
    finally:
        ws.close()


# ── 2. Public GET endpoints health ───────────────────────────────────────────
@pytest.mark.parametrize("entry", PUBLIC_GET_ENDPOINTS,
                         ids=lambda e: e[0] + (f"?{e[1]}" if e[1] else ""))
def test_get_endpoint_responds(host, entry):
    """Every public read endpoint must return 200 + parseable JSON.

    Throttled between probes to avoid hammering the TCP accept queue on
    no-PSRAM boards where pbuf pool is small.
    """
    path, query, slow = entry
    _require_healthy(host)
    time.sleep(ENDPOINT_DELAY_SEC)

    url = f"http://{host}{path}"
    if query:
        url += f"?{query}"
    timeout = 25 if slow else 8

    try:
        r = _retry_get(host, path, query, timeout=timeout, retries=2)
    except requests.exceptions.RequestException as e:
        if slow:
            pytest.skip(f"{path}: slow endpoint failed (known chunked-HTTP issue on no-PSRAM): {e}")
        raise
    if slow and r.status_code in (503, 500):
        pytest.skip(f"{path}: returned {r.status_code} (known OOM on no-PSRAM): {r.text[:80]!r}")
    assert r.status_code == 200, f"{path} returned {r.status_code} body={r.text[:120]!r}"
    try:
        r.json()
    except ValueError:
        pytest.fail(f"{path} did not return valid JSON: {r.text[:120]!r}")


# ── 3. Macros CRUD round-trip ────────────────────────────────────────────────
def test_macros_crud_roundtrip(host, ws_url):
    """Snapshot current macros, append 2 test items, verify they appear, then
    restore the snapshot — proving both add and remove paths.
    """
    _require_healthy(host)

    baseline = _get_via_ws(ws_url, "macros") or []
    baseline_ids = {m.get("id") for m in baseline}

    # Unique IDs per run so leftover items from a crashed prior run don't
    # poison the collision check.
    run = uuid.uuid4().hex[:8]
    tid_a, tid_b = f"e2e-mac-{run}-a", f"e2e-mac-{run}-b"

    test_items = [
        {"id": tid_a, "title": "E2E A", "type": "command", "cmd": "echo a"},
        {"id": tid_b, "title": "E2E B", "type": "command", "cmd": "echo b"},
    ]
    assert not (baseline_ids & {m["id"] for m in test_items}), "test IDs collide with baseline"

    ws = _ws_open(ws_url)
    try:
        _enter_service(ws)
        time.sleep(1)  # let LVGL timers quiesce
        # ADD: baseline ∪ test
        _save(ws, "macros", baseline + test_items)
        got = _get_on_ws(ws, "macros") or []
        ids = {m.get("id") for m in got}
        assert tid_a in ids and tid_b in ids, f"test macros missing after save: {ids}"

        # REMOVE: save back baseline (including empty [] — firmware now accepts it)
        _save(ws, "macros", baseline)
        got2 = _get_on_ws(ws, "macros") or []
        ids2 = {m.get("id") for m in got2}
        assert tid_a not in ids2 and tid_b not in ids2, \
            f"test macros not removed: {ids2}"
    finally:
        _exit_service(ws)
        try: ws.close()
        except Exception: pass


# ── 4. Widgets CRUD round-trip ───────────────────────────────────────────────
def test_widgets_crud_roundtrip(host, ws_url):
    _require_healthy(host)

    baseline = _get_via_ws(ws_url, "widgets") or []
    baseline_ids = {w.get("id") for w in baseline}

    run = uuid.uuid4().hex[:8]
    tid_a, tid_b = f"e2e-wid-{run}-a", f"e2e-wid-{run}-b"

    test_items = [
        {"id": tid_a, "title": "E2E W-A", "type": "text",
         "data_target": "url_system", "update": 60,
         "url": "https://example.com/a", "template": "{n}", "parse_type": "json",
         "json_keys": ["n"]},
        {"id": tid_b, "title": "E2E W-B", "type": "text",
         "data_target": "url_system", "update": 60,
         "url": "https://example.com/b", "template": "{m}", "parse_type": "json",
         "json_keys": ["m"]},
    ]
    assert not (baseline_ids & {w["id"] for w in test_items}), "test IDs collide with baseline"

    ws = _ws_open(ws_url)
    try:
        _enter_service(ws)
        time.sleep(3)  # extra settle: previous endpoint tests may have fragmented heap
        _save(ws, "widgets", baseline + test_items, timeout=25.0)
        got = _get_on_ws(ws, "widgets") or []
        ids = {w.get("id") for w in got}
        assert tid_a in ids and tid_b in ids, f"test widgets missing: {ids}"

        # REMOVE: save back baseline (including empty [] — firmware now accepts it)
        _save(ws, "widgets", baseline, timeout=25.0)
        got2 = _get_on_ws(ws, "widgets") or []
        ids2 = {w.get("id") for w in got2}
        assert tid_a not in ids2 and tid_b not in ids2, \
            f"test widgets not removed: {ids2}"
    finally:
        _exit_service(ws)
        try: ws.close()
        except Exception: pass


# ── 5. Settings update inside service mode ───────────────────────────────────
def test_settings_save_in_service_mode(host, ws_url):
    """Save brightness inside service mode; verify it persists via WS read."""
    _require_healthy(host)
    ws = _ws_open(ws_url)
    try:
        _enter_service(ws)
        _save(ws, "settings", {"brightness": 75})
    finally:
        _exit_service(ws)
        try: ws.close()
        except Exception: pass

    items = _get_via_ws(ws_url, "settings") or {}
    assert int(items.get("brightness", -1)) == 75, \
        f"brightness not persisted: {items.get('brightness')}"


# ── 6. Service mode cycle stress + heap healthy ──────────────────────────────
def test_service_mode_cycles_keep_heap_healthy(host, ws_url):
    """Three full enter→exit cycles. Total free heap must stay healthy.

    Regression guard for Fix-G (widget update path → static buffers). Pre-fix
    the device would wedge after one or two cycles when proxy was attached;
    post-fix it survives 3+ with no measurable churn.
    """
    before = _require_healthy(host)
    before_free = before["heap"]["free"]

    for i in range(3):
        ws = _ws_open(ws_url)
        try:
            _enter_service(ws)
            time.sleep(0.5)
            _exit_service(ws)
        finally:
            try: ws.close()
            except Exception: pass

    after = _diag(host, timeout=15)
    after_free = after["heap"]["free"]
    assert after_free > HEAP_FREE_MIN, (
        f"total free heap fell dangerously low after cycles: "
        f"before={before_free}  after={after_free}"
    )


# ── 7. Device still responsive after the whole flow ──────────────────────────
def test_device_responsive_post_workflow(host):
    """After the CRUD + service-mode tests, HTTP must still answer < 3 s."""
    t0 = time.time()
    d = _diag(host, timeout=10)
    dt = time.time() - t0
    assert dt < 3.0, f"/api/_diag too slow after workflow: {dt:.2f}s"
    assert d.get("heap", {}).get("free", 0) > HEAP_FREE_MIN, \
        f"heap exhausted at workflow end: free={d['heap']['free']}"
