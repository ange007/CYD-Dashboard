# Architecture

Non-obvious design decisions behind CYD Dashboard. Read this before changing the web-delivery, memory, or release machinery — most of it exists to survive the no-PSRAM ESP32's tight internal DRAM.

## Web delivery: CDN shell + on-device fallback

The device does **not** store or serve the full SPA. Instead:

1. `GET /` returns a ~1 KB gzipped HTML **shell** (`web/shell.html`, baked into `src/modules/wifi/service_shell.h`).
2. The shell `import()`s `app.js` from a fallback chain: `jsdelivr → rawcdn.githack → /assets/app.js` (on-device LittleFS).
3. `app.js` imports Vue / Vue-Router / Pinia from `esm.sh` (absolute URLs baked in at build — see below).

Why: the ~200 KB bundle never touches the device's flash or heap. On no-PSRAM boards, serving a large file over HTTP/WS used to spike heap and 503. The shell is small enough for a single TCP send.

HTTP shell loading HTTPS CDN scripts is allowed by browsers (secure upgrade), so there's no mixed-content block.

### Rollup `paths`, not importmap

`web/vite.config.ts` sets `output.paths` to rewrite bare `vue`/`pinia`/`vue-router` imports into absolute `https://esm.sh/...` URLs **inside `app.js`**. Import maps are **not** reliably applied to cross-origin dynamic `import()` across browsers, so baking the URLs is the robust path.

### jsdelivr immutable-tag trap

jsdelivr caches a `@version` tag → commit resolution **permanently**. Consequences:

- **Reusing or force-moving a tag serves stale content forever.** Always bump to a brand-new version number.
- Purging a file (`purge.jsdelivr.net/...`) clears the file cache but **not** the tag→commit resolution.
- `rawcdn.githack.com` serves git content near-real-time — it's the reliable fallback while jsdelivr's tag listing catches up (minutes for a fresh tag).

## Release pipeline

`.github/workflows/release.yml` triggers on a push that changes `platformio.ini` (where `custom_spa_version` lives):

1. Build the web bundle with `SPA_VERSION` from `custom_spa_version`.
2. Commit `web/dist/` + regenerated `service_shell.h` to `master`.
3. Force the version tag onto that commit; recreate the GitHub release.
4. Purge jsdelivr for `app.js`.

`scripts/gen_shell_h.py` regenerates `service_shell.h` from `web/shell.html` on every `pio run` (reads `custom_spa_version`, the repo from `git remote`). `scripts/build_web.py` runs the full `npm run build` only on `buildfs`/`uploadfs`.

## WS-first communication

Config init and saves go over WebSocket, not HTTP:

- A bundled `init_ack` (macros + widgets + settings + profiles + display) is one WS message → one mailbox slot. The prebuilt esp-httpd UDP control mailbox holds only 6 packets; sending many separate frames after the service-mode broadcasts silently drops the tail.
- On no-PSRAM, `/api/init` over HTTP can 503 under heap pressure; WS avoids the large response alloc.
- Device telemetry (`get:diag` → `diag_ack`) and the dashboard reuse the same WS request/reply pattern.

`web/src/api/init.ts` guards against the failure mode where a `state_changed` refresh racing an in-flight init overwrote the macros store with an empty array: it tracks `_initRunning` and never replaces a non-empty store with `[]`.

## Service mode

Editing happens in **service mode**, claimed by a WS client via `enter_service_mode` (token in `sessionStorage` survives reloads within a grace window). Saves are domain-isolated in `api.cpp::onSave` — saving `settings` never touches `macros`/`widgets`. Macros/widgets persist to LittleFS (`Cards::Macros::set`) with an in-memory PSRAM/DRAM cache that `init_ack` reads from.

Settings apply through a **pending-flag pattern** in `server_base.cpp::loop()`: the WS task sets `_pending_*` flags; the LVGL-owning main loop applies them under the LVGL mutex, avoiding cross-task LVGL races.

## Memory: the no-PSRAM ESP32 constraint

Plain ESP32 (e.g. `esp32-2432S028Rv2`) has ~124 KB usable `dram0_0_seg` and no PSRAM. Everything competes for internal DRAM. Key decisions:

### LVGL9 shadow cache — disabled

LVGL9 consolidated global state into one `lv_global` struct. With `LV_DRAW_SW_SHADOW_CACHE_SIZE=256` it contained a static `uint8_t[256*256] = 64 KB` blur cache — the **single largest DRAM consumer** and the reason NimBLE no longer fit (NimBLE + ESPAsyncWebServer + LVGL8 worked historically; the LVGL8→9 upgrade ate the headroom, not the HTTP-server swap).

`include/lv_conf.h` sets `LV_DRAW_SW_SHADOW_CACHE_SIZE 0` — shadows still render (`LV_DRAW_SW_COMPLEX=1`), only the precomputed-blur cache is gone (a one-off recompute on redraw of mostly-static cards). Frees 64 KB on **all** boards.

### Draw buffer

`esp32-2432S028Rv2` uses a 1/10-screen single draw buffer (`MALLOC_CAP_INTERNAL`). Smaller than the default keeps a larger contiguous free block (measured: largest free block 5 KB → 12 KB at 1/8 → 1/10), which matters because LVGL/DMA flush needs contiguous DRAM.

### NimBLE + service mode

NimBLE's controller is ~24 KB resident DRAM. On no-PSRAM, that plus the service-mode burst (TCP sockets + WS + LVGL) drops the largest free block to ~1.5 KB → panic. Fix: **BLE is deinited on service-mode enter (all boards) and reinited on exit** (`server_base.cpp`). The keyboard is only used in normal mode, so freeing its 24 KB while the web UI is open is safe. This — together with the shadow-cache and draw-buffer wins — lets NimBLE run on the no-PSRAM Rv2.

### PSRAM vs no-PSRAM split

`lv_conf.h` and `platformio.ini` branch on `BOARD_HAS_PSRAM`: PSRAM boards back the LVGL allocator with SPIRAM (`LV_STDLIB_BUILTIN` + PSRAM pool); no-PSRAM uses the system heap (`LV_STDLIB_CLIB`, no reserved static pool). WiFi/lwIP buffer counts and LVGL draw-buffer size are also tuned per board.
