"""
/api/reboot — soft-reset endpoint (service-mode gated).

Used by SPA "Apply & Reboot" flows and by automated tests that need a
known-clean device state between runs.
"""
import json as _json
import time
import pytest
import requests

try:
    import websocket
except ImportError:
    websocket = None


def _service_mode_enter(ws_url, timeout_s: float = 15) -> "websocket.WebSocket":
    if websocket is None:
        pytest.skip("websocket-client not installed")
    ws = websocket.create_connection(ws_url, timeout=10)
    ws.settimeout(timeout_s)
    ws.send(_json.dumps({"action": "enter_service_mode"}))
    end = time.time() + timeout_s
    ready = False
    while time.time() < end:
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            break
        try:
            m = _json.loads(raw)
        except Exception:
            continue
        if m.get("action") == "service_mode_denied":
            try: ws.close()
            except: pass
            pytest.skip(f"svc mode denied: {m.get('reason')}")
        if m.get("action") == "service_mode_ready":
            ready = True
            break
    if not ready:
        try: ws.close()
        except: pass
        pytest.skip("service_mode_ready not received")
    return ws


def test_reboot_blocked_outside_service_mode(base_url, http_timeout):
    """/api/reboot HTTP endpoint requires service mode (403 without).

    If the endpoint returns 404, reboot moved to WS-only (action='reboot') —
    skip gracefully so the suite still passes on WS-first firmware.
    """
    r = requests.post(f"{base_url}/api/reboot", timeout=http_timeout)
    if r.status_code == 404:
        pytest.skip("/api/reboot HTTP endpoint removed — reboot is WS-only")
    assert r.status_code == 403, f"expected 403 without svc mode, got {r.status_code}"


def test_reboot_cycles_device(base_url, ws_url, http_timeout):
    """Enter service mode → WS reboot action → wait for device to come back → verify uptime reset."""
    if websocket is None:
        pytest.skip("websocket-client not installed")

    d0 = requests.get(f"{base_url}/api/_diag", timeout=http_timeout).json()
    start_uptime = d0["uptime_ms"] // 1000
    assert start_uptime >= 10, "device just booted, give it a moment"

    ws = _service_mode_enter(ws_url)
    try:
        ws.send(_json.dumps({"action": "reboot"}))
        # Drain until WS closes (device reboots) or timeout
        deadline = time.time() + 5
        while time.time() < deadline:
            try:
                ws.recv()
            except Exception:
                break
    finally:
        try: ws.close()
        except: pass

    # Wait for device to come back — up to 60 s (boot + WiFi reconnect)
    deadline = time.time() + 60
    new_uptime = None
    while time.time() < deadline:
        time.sleep(3)
        try:
            r = requests.get(f"{base_url}/api/_diag", timeout=5)
            if r.status_code == 200:
                new_uptime = r.json()["uptime_ms"] // 1000
                if new_uptime < start_uptime:
                    break  # fresh boot confirmed
        except Exception:
            continue

    assert new_uptime is not None, "device did not come back after reboot"
    assert new_uptime < start_uptime, \
        f"uptime did not reset: before={start_uptime}s after={new_uptime}s"
    time.sleep(2)
