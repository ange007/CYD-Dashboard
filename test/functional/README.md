# Functional tests

Host-driven tests that exercise a running CYD-Dashboard device over the
network (HTTP + WebSocket). These complement the PlatformIO unit-test dir
(`test/`) — they verify the firmware end-to-end, not individual C++ units.

## Setup

```bash
pip install pytest requests websocket-client playwright
playwright install chromium   # only needed for browser-flow tests
```

## Run

```bash
# Default host 192.168.1.222
pytest test/functional

# Custom host (IP or mDNS)
pytest test/functional --host 192.168.1.50
pytest test/functional --host cyd-dashboard.local

# Subset
pytest test/functional -k diag
pytest test/functional/test_websocket.py

# Verbose
pytest test/functional -v
```

## What's covered

| File | Checks |
|------|--------|
| `test_diag.py` | `/api/_diag` reachability, schema, heap alive, wifi connected |
| `test_static_assets.py` | proxy page, sw.js, manifest, favicon, parallel load, 404 |
| `test_websocket.py` | WS connect, ping/pong, get-state, unknown action safety |
| `test_service_mode.py` | Enter→ready→exit cycle, second-session denial |
| `test_stability.py` | 60 s idle soak, monotonic counters, repeated proxy loads |
| `test_e2e_workflow.py` | Full happy-path E2E: WS handshake, all public GETs, macros/widgets CRUD, settings save, 3× service-mode cycles, heap-floor regression guard for Fix-G |

A session-wide fixture in `conftest.py` captures the device uptime at the
start and fails the whole run if it goes backwards (silent reboot detection).

## Notes

- Tests assume the device is already running and joined to the same LAN.
- Service-mode tests hold the mode for a few seconds — do not run them
  while the companion app / browser is actively using service mode.
- Upload tests (`test_uploads.py`) require an SD card inserted. They
  auto-skip if `/api/icons` returns 503 (no SD card).
- Nothing here mocks device state; failures are real device failures.
- **Best practice**: power-cycle the device before a full-suite run.
  Running many tests back-to-back against a device already under load
  (weak WiFi, running companion proxy) can exhaust the lwIP TCP PCB
  pool — subsequent tests then time out even though the firmware is
  healthy. A clean boot gives each test stable network state.

## Running the E2E suite

`test_e2e_workflow.py` is intended as a periodic regression run after any
change to the WS/HTTP API, widget update path, or service-mode flow.

```bash
# Re-flash for a clean state, wait ~25 s for boot+WiFi, then run:
pio run -e esp32-2432S028Rv2 -t upload --upload-port COM7 && sleep 25
pytest test/functional/test_e2e_workflow.py --host 192.168.1.212 -v

# If pytest auto-loads a broken plugin on Windows
# (e.g. pytest_homeassistant_custom_component pulls in `fcntl`):
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 pytest test/functional/test_e2e_workflow.py \
  --host 192.168.1.212 -v
```

### Known fragilities on no-PSRAM boards (Rv2 / Rv3 / R)

- `/api/init` streams a large chunked payload that frequently fails on no-PSRAM
  hardware (pre-existing firmware bug, not a test bug). The test skips on
  timeout rather than failing.
- Heavy service-mode cycling can temporarily wedge the HTTP server when
  `largest_free_block` falls below ~1.5 KB. The `_wait_for_device_recovery`
  autouse fixture gives the device 30 s to recover; if it can't, the
  affected test is skipped.
- On PSRAM boards (`JC4827W543C`, etc.) all tests should pass cleanly.
