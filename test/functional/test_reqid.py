"""req_id correlation round-trip over WebSocket."""
import json
import pytest

websocket = pytest.importorskip("websocket")


def _recv_with_req_id(ws, target_req_id, timeout=5.0):
    """Drain frames until one matches the given req_id (or timeout)."""
    ws.settimeout(timeout)
    for _ in range(20):
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            return None
        try:
            m = json.loads(raw)
        except Exception:
            continue
        if m.get("req_id") == target_req_id:
            return m
    return None


def test_ping_echoes_req_id(ws_url):
    ws = websocket.create_connection(ws_url, timeout=8)
    ws.send(json.dumps({"action": "ping", "req_id": "abc123"}))
    reply = _recv_with_req_id(ws, "abc123")
    ws.close()
    assert reply is not None, "no pong with matching req_id"
    assert reply.get("action") == "pong"


def test_ping_without_req_id_still_works(ws_url):
    ws = websocket.create_connection(ws_url, timeout=8)
    ws.send(json.dumps({"action": "ping"}))
    ws.settimeout(3)
    got = False
    for _ in range(5):
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            break
        m = json.loads(raw)
        if m.get("action") == "pong":
            assert "req_id" not in m
            got = True
            break
    ws.close()
    assert got


def test_get_init_terminator_ack(ws_url):
    ws = websocket.create_connection(ws_url, timeout=8)
    ws.send(json.dumps({"action": "get", "what": "init", "req_id": "init-1"}))
    reply = _recv_with_req_id(ws, "init-1", timeout=8)
    ws.close()
    assert reply is not None, "no init_ack received"
    assert reply.get("action") == "init_ack"
    assert reply.get("ok") is True
