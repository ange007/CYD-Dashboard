"""Soak/stability — confirms the device survives sustained, gentle traffic."""
import time
import requests
import pytest


def test_idle_stable_60s(base_url, http_timeout):
    """Device must stay alive + responsive for 60 s of idle + periodic diag."""
    start_uptime = requests.get(f"{base_url}/api/_diag", timeout=http_timeout).json()["uptime_ms"]
    deadline = time.time() + 60
    samples = 0
    while time.time() < deadline:
        r = requests.get(f"{base_url}/api/_diag", timeout=http_timeout)
        assert r.status_code == 200
        samples += 1
        time.sleep(5)
    end = requests.get(f"{base_url}/api/_diag", timeout=http_timeout).json()
    assert end["uptime_ms"] > start_uptime, "uptime went backwards — device rebooted"
    assert samples >= 10, f"only {samples} samples — test too slow"


def test_repeated_proxy_page_5x(base_url, http_timeout):
    """5 sequential proxy-page loads with 2 s gap — must all succeed."""
    for i in range(5):
        r = requests.get(f"{base_url}/", timeout=http_timeout)
        assert r.status_code == 200, f"iter {i+1}: HTTP {r.status_code}"
        time.sleep(2)


def test_diag_counter_monotonic(base_url, http_timeout):
    """http.enter must only increase across two diag calls."""
    d1 = requests.get(f"{base_url}/api/_diag", timeout=http_timeout).json()
    time.sleep(1)
    d2 = requests.get(f"{base_url}/api/_diag", timeout=http_timeout).json()
    assert d2["http"]["enter"] >= d1["http"]["enter"]
    assert d2["uptime_ms"] > d1["uptime_ms"]
