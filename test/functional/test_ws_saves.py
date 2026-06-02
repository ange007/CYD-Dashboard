"""WS save action — ack flow, service-mode gating, broadcast side-effect."""
import json
import time
import pytest

websocket = pytest.importorskip("websocket")


def _enter_service(ws):
    ws.send(json.dumps({"action": "enter_service_mode"}))
    ws.settimeout(5)
    for _ in range(10):
        raw = ws.recv()
        m = json.loads(raw)
        if m.get("action") == "service_mode_entered":
            return m.get("token")
        if m.get("action") == "service_mode_denied":
            pytest.skip("another session holds service mode")
    pytest.fail("service_mode_entered never received")


def _wait_for(ws, predicate, timeout=6.0):
    ws.settimeout(timeout)
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            return None
        try:
            m = json.loads(raw)
        except Exception:
            continue
        if predicate(m):
            return m
    return None


def test_save_macros_requires_service_mode(ws_url):
    ws = websocket.create_connection(ws_url, timeout=8)
    ws.send(json.dumps({
        "action": "save", "what": "macros", "items": [],
        "req_id": "s1",
    }))
    ack = _wait_for(ws, lambda m: m.get("req_id") == "s1")
    ws.close()
    assert ack is not None
    assert ack.get("action") == "save_ack"
    assert ack.get("ok") is False
    assert ack.get("error") == "not_in_service_mode"


def test_save_macros_happy_path(ws_url):
    ws = websocket.create_connection(ws_url, timeout=8)
    _enter_service(ws)
    items = [{"id": "t-mac", "title": "Test", "type": "command", "cmd": "echo"}]
    ws.send(json.dumps({
        "action": "save", "what": "macros", "items": items,
        "req_id": "s2",
    }))
    ack = _wait_for(ws, lambda m: m.get("req_id") == "s2")
    ws.send(json.dumps({"action": "exit_service_mode"}))
    ws.close()
    assert ack is not None
    assert ack.get("ok") is True


def test_save_settings_happy_path(ws_url):
    ws = websocket.create_connection(ws_url, timeout=8)
    _enter_service(ws)
    ws.send(json.dumps({
        "action": "save", "what": "settings", "items": {"brightness": 60},
        "req_id": "s3",
    }))
    ack = _wait_for(ws, lambda m: m.get("req_id") == "s3")
    ws.send(json.dumps({"action": "exit_service_mode"}))
    ws.close()
    assert ack is not None
    assert ack.get("ok") is True


def test_save_unknown_what(ws_url):
    ws = websocket.create_connection(ws_url, timeout=8)
    _enter_service(ws)
    ws.send(json.dumps({
        "action": "save", "what": "totally_made_up",
        "req_id": "s4",
    }))
    ack = _wait_for(ws, lambda m: m.get("req_id") == "s4")
    ws.send(json.dumps({"action": "exit_service_mode"}))
    ws.close()
    assert ack is not None
    assert ack.get("ok") is False
    assert ack.get("error") == "unknown_what"
