"""Device health + /api/_diag endpoint tests."""
import requests


def test_diag_reachable(base_url, http_timeout):
    r = requests.get(f"{base_url}/api/_diag", timeout=http_timeout)
    assert r.status_code == 200


def test_diag_schema(base_url, http_timeout):
    """Structural check — keys the firmware promises to expose."""
    r = requests.get(f"{base_url}/api/_diag", timeout=http_timeout)
    d = r.json()
    assert "uptime_ms" in d
    assert "wifi" in d and "status" in d["wifi"]
    assert "heap" in d and "free" in d["heap"]
    assert "http" in d and "enter" in d["http"]
    assert "tcp"  in d and "opens" in d["tcp"]


def test_diag_heap_alive(base_url, http_timeout):
    """Free heap must be above the 503 guard threshold."""
    r = requests.get(f"{base_url}/api/_diag", timeout=http_timeout)
    heap = r.json()["heap"]["free"]
    assert heap > 3000, f"heap dangerously low: {heap} B"


def test_diag_wifi_connected(base_url, http_timeout):
    """WiFi status 3 = WL_CONNECTED. Device unreachable tests wouldn't reach here."""
    r = requests.get(f"{base_url}/api/_diag", timeout=http_timeout)
    assert r.json()["wifi"]["status"] == 3
