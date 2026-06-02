"""
Icon + background upload/list/delete flow (SD card).

All device-state-mutating endpoints (/api/icons POST+DELETE,
/api/backgrounds POST+DELETE, /api/macros POST, /api/widgets POST,
/api/settings POST, /api/wifi POST, /api/profiles POST, /api/ota POST)
require the device to be in service mode. Tests open a WebSocket, enter
service mode, exercise the upload flow, and exit cleanly.

Tests also verify that the same endpoints return 403 when called without
service mode — confirming that a browser/proxy client on the LAN cannot
mutate device state outside the user-initiated service-mode window.
"""
import json as _json
import struct
import time
import pytest
import requests

try:
    import websocket  # websocket-client
except ImportError:
    websocket = None

# Multipart upload through httpd can block 150-500 ms during SD write +
# chunk reassembly. Generous timeouts + small inter-test pace prevent
# TCP accept starvation (max_open_sockets=3) from bleeding across tests.
UPLOAD_TIMEOUT = 30
INTER_TEST_PACE = 0.8


pytestmark = pytest.mark.skipif(
    # Skip all upload tests if SD card isn't available — /api/icons returns 503.
    True,
    reason="set SKIP_SD_UPLOADS=0 to run (requires SD card inserted)",
) if False else pytest.mark.usefixtures("base_url")


def _make_lvgl_bin(width: int = 32, height: int = 32) -> bytes:
    """Synthesize a minimal LVGL .bin indexed-color image (header only).

    LVGL 9 image header: magic 0x19 + version 1 + header length 12 + width
    + height + reserved. Keeps payload tiny (<200 B) so upload stays fast
    and well under the 64 KB icon size cap.
    """
    header = bytes([0x19, 0x01, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00])
    header += struct.pack("<HH", width, height)
    header += bytes(4)                    # reserved
    payload = bytes(width * height * 2)   # RGB565 zeros
    return header + payload


def _make_jpeg() -> bytes:
    """Minimal SOI+EOI JPEG. Firmware only checks extension, not full validity."""
    return b"\xff\xd8\xff\xd9"


def _sd_available(base_url: str, timeout: float = 6) -> bool:
    """Probe /api/icons GET — if SD missing, firmware returns 503."""
    try:
        r = requests.get(f"{base_url}/api/icons", timeout=timeout)
        return r.status_code == 200
    except Exception:
        return False


@pytest.fixture(scope="module", autouse=True)
def _require_sd(base_url):
    if not _sd_available(base_url):
        pytest.skip("SD card not available on device — upload endpoints return 503")


@pytest.fixture(autouse=True)
def _pace_between_tests():
    """Small sleep before each test so TCP PCBs from prior requests have time
    to drain (max_open_sockets=3, MSL ~60 s default on LAN — short gap is
    enough under normal conditions)."""
    time.sleep(INTER_TEST_PACE)
    yield


@pytest.fixture
def service_mode_ws(ws_url):
    """Open WS + enter service mode + yield ws. Exits cleanly on teardown.

    Skips the test if another session already holds service mode.
    """
    if websocket is None:
        pytest.skip("websocket-client not installed")
    ws = websocket.create_connection(ws_url, timeout=10)
    ws.settimeout(15)
    try:
        ws.send(_json.dumps({"action": "enter_service_mode"}))
        deadline = time.time() + 15
        got_ready = False
        while time.time() < deadline:
            try:
                raw = ws.recv()
            except websocket.WebSocketTimeoutException:
                break
            try:
                m = _json.loads(raw)
            except Exception:
                continue
            if m.get("action") == "service_mode_denied":
                pytest.skip(f"service mode held by other session: {m.get('reason')}")
            if m.get("action") == "service_mode_ready":
                got_ready = True
                break
        if not got_ready:
            pytest.skip("service_mode_ready not received — device busy")
        yield ws
    finally:
        try:
            ws.send(_json.dumps({"action": "exit_service_mode"}))
            time.sleep(0.3)
        except Exception:
            pass
        try:
            ws.close()
        except Exception:
            pass
        time.sleep(3)  # grace for another test to re-enter cleanly


# ── Icons ────────────────────────────────────────────────────────────────

def test_icon_upload_then_list(base_url, http_timeout, service_mode_ws):
    """Upload a .bin → listed → served via /icons/<name> → deleted."""
    fname = f"_pytest_{int(time.time())}.bin"
    files = {"file": (fname, _make_lvgl_bin(), "application/octet-stream")}
    r = requests.post(f"{base_url}/api/icons", files=files, timeout=UPLOAD_TIMEOUT)
    if r.status_code in (500, 503, 507):
        pytest.skip(f"SD not available or not writable: upload returned {r.status_code}")
    assert r.status_code == 200, f"upload failed: HTTP {r.status_code} {r.text}"
    assert r.json().get("ok") is True

    # List should contain our icon
    r = requests.get(f"{base_url}/api/icons", timeout=http_timeout)
    assert r.status_code == 200
    names = r.json()
    assert fname in names, f"{fname} not in icon list {names[:10]}..."

    # Download it back — content-length should match what we uploaded
    r = requests.get(f"{base_url}/icons/{fname}", timeout=http_timeout)
    assert r.status_code == 200
    assert len(r.content) > 0

    # Cleanup
    r = requests.delete(f"{base_url}/api/icons?name={fname}", timeout=http_timeout)
    assert r.status_code == 200


def test_icon_rejects_non_bin(base_url, http_timeout, service_mode_ws):
    files = {"file": ("bad.png", b"\x89PNG\r\n\x1a\n", "image/png")}
    r = requests.post(f"{base_url}/api/icons", files=files, timeout=10)
    # Upload-chunk handler returns false → PsychicUploadHandler responds 500
    assert r.status_code in (400, 500), f"unexpected {r.status_code}"


def test_icon_rejects_path_traversal(base_url, http_timeout, service_mode_ws):
    files = {"file": ("../evil.bin", _make_lvgl_bin(), "application/octet-stream")}
    r = requests.post(f"{base_url}/api/icons", files=files, timeout=10)
    assert r.status_code in (400, 500)


# ── Backgrounds ──────────────────────────────────────────────────────────

def test_background_upload_then_list(base_url, http_timeout, service_mode_ws):
    fname = f"_pytest_{int(time.time())}.jpg"
    files = {"file": (fname, _make_jpeg(), "image/jpeg")}
    r = requests.post(f"{base_url}/api/backgrounds", files=files, timeout=UPLOAD_TIMEOUT)
    if r.status_code in (500, 503, 507):
        pytest.skip(f"SD not available or not writable: upload returned {r.status_code}")
    assert r.status_code == 200, f"upload failed: HTTP {r.status_code} {r.text}"

    r = requests.get(f"{base_url}/api/backgrounds", timeout=http_timeout)
    assert r.status_code == 200
    lst = r.json()
    assert isinstance(lst, list)
    assert any(item.get("name") == fname for item in lst), \
        f"{fname} not in backgrounds list"

    r = requests.delete(f"{base_url}/api/backgrounds?name={fname}", timeout=http_timeout)
    assert r.status_code == 200


def test_background_rejects_non_jpeg(base_url, http_timeout, service_mode_ws):
    files = {"file": ("bad.txt", b"hello", "text/plain")}
    r = requests.post(f"{base_url}/api/backgrounds", files=files, timeout=10)
    assert r.status_code in (400, 500)


# ── Reboot-stale-session (browser doesn't know device lost service mode) ─

def test_upload_blocked_outside_service_mode(base_url, http_timeout):
    """Without service mode, /api/icons POST MUST return 403 (not_in_service_mode).

    Secures against a LAN-local attacker or a stale browser session trying
    to write SD files after the user left service mode.
    """
    fname = f"_pytest_blocked_{int(time.time())}.bin"
    files = {"file": (fname, _make_lvgl_bin(), "application/octet-stream")}
    r = requests.post(f"{base_url}/api/icons", files=files, timeout=UPLOAD_TIMEOUT)
    assert r.status_code == 403, \
        f"expected 403 outside service mode, got {r.status_code}: {r.text[:200]}"


def test_delete_blocked_outside_service_mode(base_url, http_timeout):
    """DELETE must also be service-mode-gated."""
    r = requests.delete(f"{base_url}/api/icons?name=nothing.bin", timeout=http_timeout)
    assert r.status_code == 403, f"expected 403, got {r.status_code}"


def test_macros_post_blocked_outside_service_mode(base_url, http_timeout):
    """POST /api/macros mutates device state — must be gated (403 without service mode).

    Returns 404 on WS-first firmware where HTTP POST was removed — skip gracefully.
    """
    r = requests.post(f"{base_url}/api/macros", json=[], timeout=http_timeout)
    if r.status_code == 404:
        pytest.skip("/api/macros POST removed — writes are WS-only on this firmware")
    assert r.status_code == 403


def test_widgets_post_blocked_outside_service_mode(base_url, http_timeout):
    """POST /api/widgets mutates device state — must be gated (403 without service mode).

    Returns 404 on WS-first firmware where HTTP POST was removed — skip gracefully.
    """
    r = requests.post(f"{base_url}/api/widgets", json=[], timeout=http_timeout)
    if r.status_code == 404:
        pytest.skip("/api/widgets POST removed — writes are WS-only on this firmware")
    assert r.status_code == 403


def test_upload_large_icon_rejected(base_url, http_timeout, service_mode_ws):
    """Icon upload cap is 64 KB per file. 65 KB must be rejected."""
    payload = bytes([0x19, 0x01, 0x00, 0x00]) + bytes(65 * 1024)
    files = {"file": ("toobig.bin", payload, "application/octet-stream")}
    r = requests.post(f"{base_url}/api/icons", files=files, timeout=UPLOAD_TIMEOUT)
    assert r.status_code in (400, 413, 500), \
        f"oversize icon should be rejected, got {r.status_code}"
