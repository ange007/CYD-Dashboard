"""ETag / If-None-Match handling on /api/init."""
import pytest
import requests


def test_etag_round_trip(base_url, http_timeout):
    r1 = requests.get(f"{base_url}/api/init", timeout=http_timeout)
    if r1.status_code == 503:
        pytest.skip(f"/api/init: returned 503 (known OOM on no-PSRAM): {r1.text[:80]!r}")
    assert r1.status_code == 200
    etag = r1.headers.get("ETag")
    assert etag, "no ETag on first response"

    r2 = requests.get(
        f"{base_url}/api/init",
        headers={"If-None-Match": etag},
        timeout=http_timeout,
    )
    assert r2.status_code == 304
    assert r2.headers.get("ETag") == etag
    assert len(r2.content) == 0


def test_etag_mismatch_returns_full(base_url, http_timeout):
    r = requests.get(
        f"{base_url}/api/init",
        headers={"If-None-Match": '"999999"'},
        timeout=http_timeout,
    )
    if r.status_code == 503:
        pytest.skip(f"/api/init: returned 503 (known OOM on no-PSRAM): {r.text[:80]!r}")
    assert r.status_code == 200
    assert r.headers.get("ETag")
    assert len(r.content) > 0
    j = r.json()
    assert "macros" in j and "widgets" in j
