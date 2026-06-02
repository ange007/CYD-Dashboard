"""Service-mode enter/exit WS flow."""
import json
import time
import pytest

websocket = pytest.importorskip("websocket")


def _ws_open(ws_url):
    ws = websocket.create_connection(ws_url, timeout=10)
    ws.settimeout(15)
    return ws


def _wait_action(ws, wanted, timeout_s=15):
    end = time.time() + timeout_s
    while time.time() < end:
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            return None
        try:
            m = json.loads(raw)
        except Exception:
            continue
        if m.get("action") in wanted:
            return m
    return None


def test_service_mode_cycle(ws_url):
    """Full cycle: enter -> wait READY -> exit. Measures ready latency."""
    ws = _ws_open(ws_url)
    try:
        t0 = time.time()
        ws.send(json.dumps({"action": "enter_service_mode"}))

        m = _wait_action(ws, {"service_mode_entered", "service_mode_denied"}, 10)
        assert m is not None, "no entered/denied response"
        if m["action"] == "service_mode_denied":
            pytest.skip(f"Another session owns service mode ({m.get('reason')})")

        ready = _wait_action(ws, {"service_mode_ready"}, 15)
        assert ready is not None, "service_mode_ready not received"
        ready_ms = int((time.time() - t0) * 1000)
        assert ready_ms < 10_000, f"service_mode_ready too slow: {ready_ms} ms"

        ws.send(json.dumps({"action": "exit_service_mode"}))
        time.sleep(0.5)
    finally:
        try:
            ws.close()
        except Exception:
            pass
    # Grace period so another test can re-acquire service mode cleanly.
    time.sleep(3)


def test_second_session_denied_while_held(ws_url):
    """Second WS attempting enter while first holds it must get denied (not crash)."""
    ws_a = _ws_open(ws_url)
    ws_b = None
    try:
        ws_a.send(json.dumps({"action": "enter_service_mode"}))
        m = _wait_action(ws_a, {"service_mode_entered", "service_mode_denied"}, 10)
        assert m is not None
        if m["action"] == "service_mode_denied":
            pytest.skip("Third-party already holds service mode")

        # Wait for ready so ws_a definitely owns it
        _wait_action(ws_a, {"service_mode_ready"}, 15)

        # Second session attempts enter — should be denied.
        ws_b = _ws_open(ws_url)
        ws_b.send(json.dumps({"action": "enter_service_mode"}))
        m2 = _wait_action(ws_b, {"service_mode_entered", "service_mode_denied"}, 6)
        assert m2 is not None and m2["action"] == "service_mode_denied", \
            f"expected denied, got {m2}"

        # Clean up: ws_a exits
        ws_a.send(json.dumps({"action": "exit_service_mode"}))
        time.sleep(0.5)
    finally:
        for w in (ws_a, ws_b):
            try:
                if w: w.close()
            except Exception:
                pass
    time.sleep(3)
