"""WebSocket handshake, ping/pong, and core API actions."""
import json
import time
import pytest

websocket = pytest.importorskip("websocket")


def test_ws_connect(ws_url):
    ws = websocket.create_connection(ws_url, timeout=8)
    assert ws.connected
    ws.close()


def test_ws_ping_pong(ws_url):
    ws = websocket.create_connection(ws_url, timeout=8)
    ws.settimeout(5)
    ws.send(json.dumps({"action": "ping"}))
    got_pong = False
    for _ in range(5):
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            break
        try:
            m = json.loads(raw)
        except Exception:
            continue
        if m.get("action") == "pong":
            got_pong = True
            break
    ws.close()
    assert got_pong, "no pong received within timeout"


def test_ws_get_settings(ws_url):
    """{action:'get'} returns init frames: set_macros, set_widgets, etc."""
    ws = websocket.create_connection(ws_url, timeout=8)
    ws.settimeout(6)
    ws.send(json.dumps({"action": "get"}))
    got = False
    # Response may be large and split across frames; read up to 10 frames.
    for _ in range(10):
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            break
        try:
            m = json.loads(raw)
        except Exception:
            continue
        if m.get("action") in ("set_macros", "set_widgets", "set_settings",
                               "set_profiles", "init_ack"):
            got = True
            break
    ws.close()
    assert got, "no state response within timeout"


def test_ws_unknown_action_ignored(ws_url):
    """Sending an unknown action must NOT disconnect/crash the WS."""
    ws = websocket.create_connection(ws_url, timeout=8)
    ws.settimeout(3)
    ws.send(json.dumps({"action": "this_does_not_exist_ever"}))
    # WS should still be alive — send ping right after to confirm
    ws.send(json.dumps({"action": "ping"}))
    got_pong = False
    for _ in range(4):
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            break
        try:
            m = json.loads(raw)
        except Exception:
            continue
        if m.get("action") == "pong":
            got_pong = True
            break
    ws.close()
    assert got_pong, "WS did not stay alive after unknown action"
