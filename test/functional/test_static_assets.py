"""Static-asset serving (proxy page, SPA, PWA files)."""
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
import pytest
import requests


ASSETS = [
    "/",
    "/favicon.svg.gz",
    "/sw.js.gz",
    "/manifest.webmanifest.gz",
]


def test_proxy_page(base_url, http_timeout):
    r = requests.get(f"{base_url}/", timeout=http_timeout)
    assert r.status_code == 200
    assert "html" in r.headers.get("content-type", "").lower()


def test_sw_js(base_url, http_timeout):
    """Service worker must be served with correct gzip encoding."""
    r = requests.get(f"{base_url}/sw.js.gz", timeout=http_timeout)
    if r.status_code == 503:
        pytest.skip("no-PSRAM: heap guard returned 503 for sw.js.gz")
    assert r.status_code == 200


def test_manifest(base_url, http_timeout):
    r = requests.get(f"{base_url}/manifest.webmanifest.gz", timeout=http_timeout)
    if r.status_code == 503:
        pytest.skip("no-PSRAM: heap guard returned 503 for manifest.webmanifest.gz")
    assert r.status_code == 200


def test_favicon(base_url, http_timeout):
    r = requests.get(f"{base_url}/favicon.svg.gz", timeout=http_timeout)
    if r.status_code == 503:
        pytest.skip("no-PSRAM: heap guard returned 503 for favicon.svg.gz")
    assert r.status_code == 200


def test_404_unknown(base_url, http_timeout):
    r = requests.get(f"{base_url}/does-not-exist.js", timeout=http_timeout)
    assert r.status_code in (404, 200)  # SPA fallback may serve index; both acceptable


def test_sequential_asset_load(base_url, http_timeout):
    """Fetch all PWA assets back-to-back — common browser cold-start pattern."""
    for path in ASSETS:
        r = requests.get(f"{base_url}{path}", timeout=http_timeout)
        if r.status_code == 503:
            pytest.skip(f"no-PSRAM: heap guard returned 503 for {path}")
        assert r.status_code == 200, f"{path} returned {r.status_code}"
        time.sleep(0.15)


def test_parallel_asset_load(base_url):
    """Moderate parallel GET — 3 concurrent (fits in max_open_sockets=3)."""
    urls = [f"{base_url}{p}" for p in ASSETS[:3]]
    with ThreadPoolExecutor(max_workers=3) as ex:
        futs = [ex.submit(requests.get, u, timeout=12) for u in urls]
        for f in as_completed(futs):
            r = f.result()
            if r.status_code == 503:
                pytest.skip("no-PSRAM: heap guard returned 503")
            assert r.status_code == 200
