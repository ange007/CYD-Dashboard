// server_psychic.cpp — PsychicHttp backend for WebServerBase.
// Compiled only when USE_PSYCHIC_HTTP is defined in build_flags.
#ifdef USE_PSYCHIC_HTTP

#include "server_psychic.h"
#include "api.h"
#include "helpers.h"

#include <Arduino.h>
#include <cJSON.h>
#include <map>
#include <vector>

#ifdef ESP32
  #include <WiFi.h>
#endif
#include <LittleFS.h>

#ifdef OTA_ENABLED
  #include <Update.h>
#endif

#include <lwip/sockets.h>
#include "../../constants.h"
#include "../../ui/ui.h"
#include "../../utils/memory.h"
#include "../../utils/ui.h"
#include "../../utils/json.h"
#include "../../utils/heap_probe.h"
#include "net_diag.h"
#include "../../utils/settings.h"
#include "../../utils/lvfs_littlefs.h"
#include "../../utils/sd.h"
#include "../../utils/icons.h"
#include "../../utils/backgrounds.h"
#include "../../utils/lvfs_sd.h"

#include "../cards/widgets.h"
#include "../cards/macros.h"
#include "../cards/profiles.h"
#include "proxy_page.h"
#include "manifest_inline.h"
#include "proxy_sw.h"
#include "service_shell.h"
#include "../hid/keyboard.h"

#ifdef PSYCHIC_WS_RX_STATIC_BUFFER
// Pre-alloc function defined in PsychicWebSocket.cpp (extern "C" linkage).
extern "C" void psychic_ws_preinit_rx_buf();
#endif

namespace WiFiModule
{

// ── Static error literals (pre-built, zero allocation) ────────────────────────
static const char ERR_OOM[]       = "{\"error\":\"oom\"}";
static const char ERR_TOO_LARGE[] = "{\"error\":\"file too large\"}";
static const char ERR_BUSY[]      = "{\"error\":\"busy\"}";

// Stack-allocate a small error JSON for dynamic messages (no heap needed).
static inline void makeErrorBuf(char *buf, size_t bufLen, const char *msg) {
    snprintf(buf, bufLen, "{\"error\":\"%s\"}", msg ? msg : "unknown");
}

// ── Heap guard / JSON-send macros ─────────────────────────────────────────────
// PsychicHttp handlers have signature: (PsychicRequest* req, PsychicResponse* res) -> esp_err_t
// Threshold calibrated for NimBLE PSRAM enabled:
//   Stable idle heap with BLE connected ≈ 10 KB → threshold must be below that.
//   During BLE connection setup, heap dips transiently to ~6 KB → 6 KB blocks
//   serving during that window (browser retries on 503, transient lasts ~2 s).
//   API responses need ~1-2 lwIP pbufs (1460 B each, DRAM) + small cJSON alloc;
//   6 KB leaves ~4 KB headroom which is sufficient for those allocations.
static constexpr uint32_t HEAP_GUARD_THRESHOLD = 10000;

#define HEAP_GUARD_P(res)                                                       \
    if (ESP.getFreeHeap() < HEAP_GUARD_THRESHOLD) {                             \
        NetDiag::http503HeapGuard++;                                            \
        return (res)->send(503, "application/json", ERR_BUSY);                  \
    }

// Service-mode guard — blocks device-state-mutating API endpoints when the
// dashboard is in normal (proxy) mode.  In normal mode the device should
// only relay WS proxy traffic (get_url / return_url_response); all UI
// editing (macros, widgets, settings, uploads, OTA) must be gated behind
// an explicit "enter service mode" handshake.  Without this, anyone on the
// LAN can PUT/POST/DELETE device state without the user's consent.
//
// Returns HTTP 403 with a JSON error so the SPA can surface a clear
// message ("Enter service mode first") instead of a silent failure.
static const char ERR_NOT_IN_SERVICE[] =
    "{\"error\":\"not_in_service_mode\",\"message\":\"enter service mode first\"}";

#define SERVICE_MODE_GUARD_P(res)                                               \
    if (!_serviceMode) {                                                        \
        return (res)->send(403, "application/json", ERR_NOT_IN_SERVICE);        \
    }

static inline esp_err_t sendJsonResponse(PsychicResponse* res, cJsonPtr&& root)
{
    cJsonStrPtr s = cJsonPrint(root.get());
    if (!s) return res->send(503, "application/json", ERR_OOM);
    esp_err_t e = res->send(200, "application/json", s.get());
    NetDiag::httpOk++;
    return e;
    // root + s freed on scope exit
}

// Serialize cJSON to a buffer using cJSON_PrintPreallocated.
// Prefers PSRAM to avoid DRAM spikes for large payloads; falls back to DRAM
// on boards without PSRAM (heap_caps_malloc(SPIRAM) returns NULL there).
static char *printJsonPrealloc(cJSON *root, size_t maxSize) {
    char *buf = (char *)heap_caps_malloc(maxSize, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (char *)malloc(maxSize);  // no-PSRAM fallback
    if (!buf) return nullptr;
    if (!cJSON_PrintPreallocated(root, buf, (int)maxSize, 0 /*unformatted*/)) {
        free(buf);
        return nullptr;
    }
    return buf;
}

// Like sendJsonResponse but serializes into a pre-allocated buffer to reduce
// peak heap (uses PSRAM when available, DRAM otherwise).
// Use for large responses (/api/init, /api/export).
static inline esp_err_t sendJsonResponsePsram(PsychicResponse* res, cJsonPtr&& root, size_t estSize)
{
    char* ps = printJsonPrealloc(root.get(), estSize);
    if (!ps) return res->send(503, "application/json", ERR_OOM);
    esp_err_t e = res->send(200, "application/json", ps);
    free(ps);
    NetDiag::httpOk++;
    return e;
    // root freed on scope exit
}

// ── WebSocket helpers ─────────────────────────────────────────────────────────
// ── Slot table helpers ────────────────────────────────────────────────────────
PsychicHttpImpl::WsSlot* PsychicHttpImpl::_findSlot(uint32_t id)
{
    if (id == 0) return nullptr;
    for (auto& s : _wsSlots) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

PsychicHttpImpl::WsSlot* PsychicHttpImpl::_claimSlot(uint32_t id, PsychicWebSocketClient* c)
{
    for (auto& s : _wsSlots) {
        if (s.id == 0) {
            s.id = id;
            s.client = c;
            return &s;
        }
    }
    return nullptr;  // table full
}

void PsychicHttpImpl::_releaseSlot(uint32_t id)
{
    if (id == 0) return;
    for (auto& s : _wsSlots) {
        if (s.id == id) {
            s.id = 0;
            s.client = nullptr;
            return;
        }
    }
}

bool PsychicHttpImpl::hasClients()
{
    for (const auto& s : _wsSlots) if (s.id != 0) return true;
    return false;
}

size_t PsychicHttpImpl::wsClientCount()
{
    size_t n = 0;
    for (const auto& s : _wsSlots) if (s.id != 0) ++n;
    return n;
}

void PsychicHttpImpl::wsText(uint32_t clientId, const char *msg)
{
    WsSlot* slot = _findSlot(clientId);
    if (!slot || !slot->client) return;
    esp_err_t err = slot->client->sendMessage(msg);
    if (err != ESP_OK) {
        ESP_LOGW("WiFi.WSS", "wsText: sendMessage #%u err=%s",
                 (unsigned)clientId, esp_err_to_name(err));
    }
}

void PsychicHttpImpl::wsCloseAll()
{
    // Snapshot ids first because close() will trigger onClose which mutates
    // the slot table.
    uint32_t ids[WS_MAX_CLIENTS] = {};
    size_t n = 0;
    for (const auto& s : _wsSlots) if (s.id != 0) ids[n++] = s.id;
    for (size_t i = 0; i < n; i++) {
        WsSlot* slot = _findSlot(ids[i]);
        if (slot && slot->client) slot->client->close();
    }
}

void PsychicHttpImpl::wsCloseClient(uint32_t id)
{
    WsSlot* slot = _findSlot(id);
    if (slot && slot->client) slot->client->close();
}

// ── Zero-copy WS binary send ──────────────────────────────────────────────────
// buf[0..WS_HDR_PREFIX-1] is the WS transport header built by sendBinaryToClient.
// We build the WS framing into buf[0..3] and call send() once from the httpd task
// (via httpd_queue_work) — zero per-frame heap allocation, no race with httpd socket.

static struct {
    int fd;
    const uint8_t* buf;
    size_t total;        // WS header bytes + len
    volatile bool done;
    volatile bool ok;
} _wsBin = { .done = true, .ok = false };

static void _wsBinWorker(void* /*arg*/)
{
    // Blocking send with 200 ms socket timeout. On local WiFi TCP ACKs arrive
    // in < 5 ms, so blocking is rare. The loop handles partial sends (which can
    // occur when SO_SNDTIMEO fires mid-buffer) without corrupting the WS frame.
    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
    lwip_setsockopt(_wsBin.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    const uint8_t* p   = _wsBin.buf;
    size_t         rem = _wsBin.total;
    bool ok = true;
    while (rem > 0) {
        ssize_t r = lwip_send(_wsBin.fd, p, rem, 0);
        if (r <= 0) { ok = false; break; }
        p   += (size_t)r;
        rem -= (size_t)r;
    }

    struct timeval zero = {};
    lwip_setsockopt(_wsBin.fd, SOL_SOCKET, SO_SNDTIMEO, &zero, sizeof(zero));

    _wsBin.ok   = ok;
    _wsBin.done = true;
}

bool PsychicHttpImpl::wsBinary(uint32_t clientId, const uint8_t* buf, size_t len)
{
    if (!_wsBin.done) return false;

    // Build WS binary frame header into the reserved prefix bytes.
    // len > 125 always holds (PAYLOAD_SIZE 1021 + APP_HDR_SIZE 3 = 1024 >> 125).
    uint8_t* frame   = const_cast<uint8_t*>(buf);
    frame[0] = 0x82;                   // FIN=1, opcode=2 (binary)
    frame[1] = 0x7E;                   // payload length uses 16-bit extended form
    frame[2] = (uint8_t)(len >> 8);
    frame[3] = (uint8_t)(len & 0xFF);

    _wsBin.fd    = (int)clientId;
    _wsBin.buf   = buf;
    _wsBin.total = 4 + len;
    _wsBin.ok    = false;
    _wsBin.done  = false;

    if (httpd_queue_work(_server.server, _wsBinWorker, nullptr) != ESP_OK) {
        _wsBin.done = true;
        return false;
    }

    // Worker completes in ≤ 200 ms (SO_SNDTIMEO). 500 ms here is a pure safety
    // net that can never fire before the worker finishes (no race condition).
    uint32_t t0 = millis();
    while (!_wsBin.done) {
        if (millis() - t0 > 500) { _wsBin.done = true; return false; }
        vTaskDelay(1);
    }
    return _wsBin.ok;
}

// (sendStatic removed — using PsychicHttp's built-in serveStatic() instead)

// ── start() — register all routes ────────────────────────────────────────────
void PsychicHttpImpl::start()
{
    Serial.printf("\n[HTTP] PsychicHttpImpl::start()  heap=%u  psram=%u\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());

    // LittleFS must be mounted before Macros/Widgets init so they can read
    // their JSON config files from flash storage.
    if (!LittleFS.begin(true))
        Serial.println("[LittleFS] Mount FAILED even after format");
    else
        Serial.printf("[LittleFS] Mounted OK — total=%lu  used=%lu\n",
                      (unsigned long)LittleFS.totalBytes(),
                      (unsigned long)LittleFS.usedBytes());

    lvfs_littlefs_init();

    if (!LittleFS.exists("/icons"))       LittleFS.mkdir("/icons");
    if (!LittleFS.exists("/backgrounds")) LittleFS.mkdir("/backgrounds");

#ifdef BOARD_HAS_TF
    uSD::init();
    if (uSD::isAvailable()) lvfs_sd_init();
#endif
    uIconManager::buildCache();

    Cards::Macros::init();
    Cards::Widgets::init();

    // Allocate log ring buffer (prefer PSRAM)
    _logBuf = static_cast<LogEntry *>(
        ESP.getFreePsram() > 0
            ? ps_malloc(LOG_CAP * sizeof(LogEntry))
            : malloc(LOG_CAP * sizeof(LogEntry)));
    if (_logBuf) memset(_logBuf, 0, LOG_CAP * sizeof(LogEntry));
    Serial.printf("[HTTP] Log buf alloc: %s\n", _logBuf ? "OK" : "FAILED");

    // ── TCP connection tracking + heap guard ─────────────────────────────
    _server.onOpen([](PsychicClient *client) {
        uint32_t heap = ESP.getFreeHeap();
        ESP_LOGI("HTTP", "TCP open   fd=%d  from=%s:%u  heap=%u",
                 client->socket(),
                 client->remoteIP().toString().c_str(),
                 client->remotePort(), heap);
        NetDiag::tcpOpens++;          // includes rejected connections
        if (heap < 4000) {
            NetDiag::tcpRejectsLowHeap++;
            ESP_LOGE("HTTP", "Heap too low (%u) — rejecting connection fd=%d",
                     heap, client->socket());
            client->close();
        }
    });
    _server.onClose([](PsychicClient *client) {
        NetDiag::tcpCloses++;
        ESP_LOGI("HTTP", "TCP close  fd=%d  from=%s:%u  heap=%u",
                 client->socket(),
                 client->remoteIP().toString().c_str(),
                 client->remotePort(),
                 ESP.getFreeHeap());
    });

    // ── Global middleware: CORS + Connection: close + logging ───────────
    // Runs for EVERY request.  CORS headers are needed for dev-mode
    // (localhost:5173 → device IP).  Connection: close frees TCP slots
    // immediately, leaving room for the persistent WebSocket connection.
    _server.addMiddleware([](PsychicRequest *req, PsychicResponse *res,
                              PsychicMiddlewareNext next) -> esp_err_t {
        NetDiag::httpEnter++;
        uint32_t t0 = millis();
        ESP_LOGI("HTTP", "→ %s %s  from=%s  heap=%u",
                 req->methodStr().c_str(),
                 req->uri().c_str(),
                 req->client()->remoteIP().toString().c_str(),
                 ESP.getFreeHeap());

        // CORS — allow any origin (device is on a trusted LAN)
        res->addHeader("Access-Control-Allow-Origin", "*");

        bool isWs = req->uri().startsWith("/ws");
        // Hint browser to close TCP after response (skip for WebSocket)
        if (!isWs)
            res->addHeader("Connection", "close");

        esp_err_t err = next();

        // Slot reclaim strategy (no force-close — it races with response flush):
        //   1. "Connection: close" header  → browser closes its side after response
        //   2. lru_purge_enable = true      → server evicts oldest idle socket on demand
        //   3. send_wait_timeout = 5        → dead connections time out in 5 s
        // Together these free TCP slots without risking httpd_sess_trigger_close()
        // closing the socket before the response is fully transmitted.

        ESP_LOGI("HTTP", "← %s %s  code=%d  dt=%lums  err=0x%x",
                 req->methodStr().c_str(),
                 req->path().c_str(),
                 res->getCode(),
                 (unsigned long)(millis() - t0),
                 (unsigned)err);
        return err;
    });

    // ── WebSocket ─────────────────────────────────────────────────────────
    _wsHandler.onOpen([this](PsychicWebSocketClient *client) {
        uint32_t id = (uint32_t)client->socket();
        HeapProbe::snapshot("ws.open.before");
        WsSlot* slot = _claimSlot(id, client);
        ESP_LOGI("WiFi.WSS", "WS #%u connected  heap=%u  ws_total=%u",
                 id, ESP.getFreeHeap(), (unsigned)wsClientCount());
        if (!slot) {
            ESP_LOGW("WiFi.WSS", "WS #%u rejected — client cap (%u) reached",
                     id, (unsigned)WS_MAX_CLIENTS);
            client->close();
            return;
        }

        // NOTE: no auto-transfer of service mode ownership to new WS clients.
        // Previously we transferred ownership during the grace window so the
        // Vue SPA could reconnect across a page reload. That fired for ANY
        // new WS client — including a companion re-connecting in parallel —
        // which then claimed the slot, kept service mode alive indefinitely,
        // and blocked widget refresh. Grace timeout in loop() will exit
        // service mode after 30 s of no owner.

        if (_proxyClientId == 0)
        {
            _proxyClientId = id;
            // If exitServiceMode() already set the fast-path flag (no widget
            // changes in svc mode → just re-arm timers), respect it. Overriding
            // with _pending_widgets_refresh causes a full updateList() that may
            // trigger cJSON allocs on a freshly-fragmented heap for no benefit.
            if (!_pending_widgets_timer_resume)
                _pending_widgets_refresh = true;
        }
        updateConnectionIcon();
    });

    _wsHandler.onClose([this](PsychicWebSocketClient *client) {
        uint32_t id = (uint32_t)client->socket();
        const char *role = (id == _companionClientId) ? "companion"
                           : (id == _proxyClientId)   ? "proxy"
                                                      : "unknown";
        ESP_LOGW("WiFi.WSS", "WS #%u (%s) disconnected  heap=%u",
                 id, role, ESP.getFreeHeap());
        HeapProbe::snapshot("ws.close.before");
        _releaseSlot(id);
        _wsFragBuf.erase(id);
        if (id == _proxyClientId)     { _proxyClientId = 0; API::clearProxyInflight(); }
        if (id == _companionClientId) {
            _companionClientId = 0;
            HidKeyboard::setCompanionHidReady(false, 0);
        }
        // Service mode: owner disconnected — start grace period for page navigation
        if (id == _serviceModeClientId)
        {
            _serviceModeClientId   = 0;
            _serviceModeGraceStart = millis();
            ESP_LOGW("SVC", "Service mode owner #%u disconnected — grace period started", id);
        }

        updateConnectionIcon();
    });

    _wsHandler.onFrame([this](PsychicWebSocketRequest *request,
                               httpd_ws_frame *frame) -> esp_err_t {
        if (!frame || frame->type != HTTPD_WS_TYPE_TEXT) return ESP_OK;
        if (!frame->payload || frame->len == 0) return ESP_OK;

        uint32_t id = (uint32_t)request->client()->socket();

        if (frame->final)
        {
            // Check if this completes a fragmented message
            auto fragIt = _wsFragBuf.find(id);
            if (fragIt != _wsFragBuf.end() && !fragIt->second.empty())
            {
                auto &buf = fragIt->second;
                if (buf.size() + frame->len > WS_MSG_MAX)
                {
                    _wsFragBuf.erase(fragIt);
                    ESP_LOGW("WiFi.WSS", "WS #%u fragment overflow — dropped", id);
                    return ESP_OK;
                }
                buf.insert(buf.end(), frame->payload, frame->payload + frame->len);
                buf.push_back('\0');
                ESP_LOGI("WiFi.WSS", "WS #%u reassembled msg (%u B): %.512s%s",
                         id, (unsigned)(buf.size() - 1),
                         reinterpret_cast<char *>(buf.data()),
                         buf.size() > 513 ? "...(truncated)" : "");
                processWsMessage(id, reinterpret_cast<char *>(buf.data()));
                _wsFragBuf.erase(fragIt);
            }
            else
            {
                // Single complete frame.
#ifdef PSYCHIC_WS_RX_STATIC_BUFFER
                // frame->payload IS s_ws_rx_buf (static, mutex held for duration of
                // this callback). Buffer was memset to 0 before recv, so
                // payload[frame->len] == '\0' — null-terminated, no copy needed.
                char *msg = (char*)frame->payload;
#else
                char *msg = new (std::nothrow) char[frame->len + 1];
                if (!msg) return ESP_ERR_NO_MEM;
                memcpy(msg, frame->payload, frame->len);
                msg[frame->len] = '\0';
#endif
                ESP_LOGI("WiFi.WSS", "WS #%u msg: %.512s%s", id, msg,
                         frame->len > 512 ? "...(truncated)" : "");
                processWsMessage(id, msg);
#ifndef PSYCHIC_WS_RX_STATIC_BUFFER
                delete[] msg;
#endif
            }
        }
        else
        {
            // Non-final fragment — accumulate
            auto &buf = _wsFragBuf[id];
            if (buf.size() + frame->len > WS_MSG_MAX)
            {
                _wsFragBuf.erase(id);
                ESP_LOGW("WiFi.WSS", "WS #%u fragment overflow — dropped", id);
                return ESP_OK;
            }
            buf.insert(buf.end(), frame->payload, frame->payload + frame->len);
        }
        return ESP_OK;
    });

    _server.on("/ws", &_wsHandler);

    // ── /api/_diag ── network diagnostics snapshot (no heap guard, no auth) ─────
    // No HEAP_GUARD_P: endpoint must work under heap pressure — that's its purpose.
    // No httpOk++: self-queries would inflate the counter and skew enter/ok ratio
    //   analysis. httpEnter still increments via middleware, so /api/_diag requests
    //   are visible there; they are simply excluded from the "successful responses" tally.
    _server.on("/api/_diag", HTTP_GET, [](PsychicRequest* req, PsychicResponse* res) -> esp_err_t {
        String body = NetDiag::toJson();  // ~12 B stack; reserve(640) heap-allocates
        return res->send(200, "application/json", body.c_str());
    });

    // ── /api/health ── ?act=ping | service_mode | (default=status) ──────
    _server.on("/api/health", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        String act = req->hasParam("act") ? req->getParam("act")->value() : "";
        if (act == "ping") {
            NetDiag::httpOk++; return res->send(200, "application/json", "{\"ok\":true}");
        }
        if (act == "service_mode") {
            HEAP_GUARD_P(res);
            cJsonPtr json = cJsonCreateObject();
            if (!json) return res->send(503, "application/json", ERR_OOM);
            cJSON_AddBoolToObject(json.get(), "active", _serviceMode);
            if (_serviceMode)
                cJSON_AddNumberToObject(json.get(), "owner", (double)_serviceModeClientId);
            return sendJsonResponse(res, std::move(json));
        }
        // default: health status
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"heap\":%u,\"min_heap\":%u,\"psram\":%u,"
            "\"uptime\":%lu,\"ws_clients\":%u}",
            ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram(),
            millis() / 1000UL, (unsigned)wsClientCount());
        NetDiag::httpOk++; return res->send(200, "application/json", buf);
    });

#ifdef ENABLE_SCREENSHOT_ENDPOINT
    // ── /api/screenshot ── capture the active LVGL screen as a 24-bit BMP ──────
    // Temporary tooling for docs screenshots. Enabled only with
    // -D ENABLE_SCREENSHOT_ENDPOINT (also flips LV_USE_SNAPSHOT in lv_conf.h).
    // Pixel buffer allocated in PSRAM (W*H*2 ≈ 255 KB for 480×272) — DRAM is too
    // small. lv_draw_buf_init + lv_snapshot_take_to_draw_buf avoids lv_malloc.
    _server.on("/api/screenshot", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        // Screen dimensions: read under LVGL lock (brief).
        _ui_enable_mutex(1);
        const uint32_t w = (uint32_t)lv_obj_get_width(lv_screen_active());
        const uint32_t h = (uint32_t)lv_obj_get_height(lv_screen_active());
        _ui_disable_mutex(1);

        const uint32_t srcStride = (w * 2u + 3u) & ~3u;
        const uint32_t pixBytes  = srcStride * h;

        // Allocate pixel buffer in PSRAM (480×272×2 ≈ 255 KB — won't fit in DRAM).
        uint8_t* pixBuf = (uint8_t*)heap_caps_malloc(pixBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!pixBuf) {
            ESP_LOGE("HTTP", "screenshot: PSRAM alloc failed (%u bytes)", (unsigned)pixBytes);
            return res->send(507, "application/json", "{\"error\":\"snapshot failed\"}");
        }

        // lv_snapshot_take_to_draw_buf must run in the LVGL task (main loop).
        // We post a request via semaphore and wait — loop() picks it up each tick.
        while (!_screenshotCtx.trigger) vTaskDelay(pdMS_TO_TICKS(10));  // wait for lazy init
        _screenshotCtx.pixBuf  = pixBuf;
        _screenshotCtx.w       = w;
        _screenshotCtx.h       = h;
        _screenshotCtx.stride  = srcStride;
        _screenshotCtx.ok      = false;
        xSemaphoreGive(_screenshotCtx.trigger);

        if (xSemaphoreTake(_screenshotCtx.done, pdMS_TO_TICKS(5000)) != pdTRUE || !_screenshotCtx.ok) {
            heap_caps_free(pixBuf);
            ESP_LOGE("HTTP", "screenshot: snapshot timed out or failed");
            return res->send(507, "application/json", "{\"error\":\"snapshot failed\"}");
        }

        const uint32_t rowSize   = (w * 3u + 3u) & ~3u;       // BMP rows padded to 4 bytes
        const uint32_t imgSize   = rowSize * h;
        const uint32_t fileSize  = 54u + imgSize;

        // 14-byte BITMAPFILEHEADER + 40-byte BITMAPINFOHEADER, little-endian.
        uint8_t hdr[54] = {0};
        hdr[0] = 'B'; hdr[1] = 'M';
        hdr[2]  = fileSize;       hdr[3]  = fileSize >> 8;  hdr[4]  = fileSize >> 16; hdr[5]  = fileSize >> 24;
        hdr[10] = 54;             // pixel data offset
        hdr[14] = 40;             // DIB header size
        hdr[18] = w;  hdr[19] = w >> 8;  hdr[20] = w >> 16; hdr[21] = w >> 24;
        hdr[22] = h;  hdr[23] = h >> 8;  hdr[24] = h >> 16; hdr[25] = h >> 24;
        hdr[26] = 1;              // color planes
        hdr[28] = 24;             // bits per pixel
        hdr[34] = imgSize; hdr[35] = imgSize >> 8; hdr[36] = imgSize >> 16; hdr[37] = imgSize >> 24;

        // One padded BMP row (DRAM, small: e.g. 480*3 ≈ 1.5 KB).
        uint8_t* row = (uint8_t*)malloc(rowSize);
        if (!row) {
            heap_caps_free(pixBuf);
            return res->send(507, "application/json", "{\"error\":\"row OOM\"}");
        }
        memset(row, 0, rowSize);

        res->setCode(200);
        res->setContentType("image/bmp");
        res->addHeader("Content-Disposition", "inline; filename=\"cyd-screenshot.bmp\"");
        res->sendHeaders();

        esp_err_t err = res->sendChunk(hdr, sizeof(hdr));

        // BMP scanlines are stored bottom-up — emit rows from last to first.
        for (int32_t y = (int32_t)h - 1; y >= 0 && err == ESP_OK; y--) {
            const uint8_t* src = pixBuf + (uint32_t)y * srcStride;
            uint8_t* dst = row;
            for (uint32_t x = 0; x < w; x++) {
                uint16_t px = (uint16_t)src[0] | ((uint16_t)src[1] << 8);  // RGB565, little-endian
                src += 2;
                uint8_t r5 = (px >> 11) & 0x1F;
                uint8_t g6 = (px >> 5)  & 0x3F;
                uint8_t b5 =  px        & 0x1F;
                // Expand to 8-bit with bit replication; BMP pixel order is BGR.
                *dst++ = (b5 << 3) | (b5 >> 2);
                *dst++ = (g6 << 2) | (g6 >> 4);
                *dst++ = (r5 << 3) | (r5 >> 2);
            }
            err = res->sendChunk(row, rowSize);
        }
        if (err == ESP_OK) err = res->finishChunking();

        free(row);
        heap_caps_free(pixBuf);
        if (err == ESP_OK) NetDiag::httpOk++;
        return err;
    });
#endif // ENABLE_SCREENSHOT_ENDPOINT

    // ── /api/init ─────────────────────────────────────────────────────────
    _server.on("/api/init", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        HEAP_GUARD_P(res);
        char etag[16];
        snprintf(etag, sizeof(etag), "\"%u\"", (unsigned)uSettings::getStateVersion());

        if (req->hasHeader("If-None-Match")) {
            String inm = req->header("If-None-Match");
            if (inm == etag) {
                NetDiag::httpOk++;
                res->addHeader("ETag", etag);
                res->setCode(304);
                return res->send();
            }
        }

        _webServeTimestamp = millis(); // pause widget HTTP workers during init
        cJsonPtr out = cJsonCreateObject();
        if (!out) return res->send(503, "application/json", ERR_OOM);
        cJSON* outRaw = out.get();
        {
            std::string snap = Cards::Macros::getCachedJsonCopy();
            cJsonPtr arr = cJsonParse(snap.c_str());
            cJSON_AddItemToObject(outRaw, "macros", arr ? arr.release() : cJSON_CreateArray());
        }
        {
            std::string snap = Cards::Widgets::getCachedJsonCopy();
            cJsonPtr arr = cJsonParse(snap.c_str());
            cJSON_AddItemToObject(outRaw, "widgets", arr ? arr.release() : cJSON_CreateArray());
        }
        { cJsonPtr settingsPtr = buildSettingsJson(); cJSON_AddItemToObject(outRaw, "settings", settingsPtr ? settingsPtr.release() : cJSON_CreateObject()); }
        cJsonPtr pList(Cards::Profiles::getList(), cJSON_Delete);
        cJSON_AddItemToObject(outRaw, "profiles", pList ? pList.release() : cJSON_CreateArray());
        cJSON_AddStringToObject(outRaw, "active_profile", Cards::Profiles::getActiveId().c_str());
        cJSON_AddBoolToObject(outRaw, "sd_available", uSD::isAvailable());
        // Display dimensions (compile-time, from board definition) — used by web UI
        // to resize uploaded backgrounds to exact screen size, sparing LVGL the scale work.
        {
            // cJSON_AddObjectToObject creates+adopts child, returns BORROWED pointer — stays raw
            cJSON *disp = cJSON_AddObjectToObject(outRaw, "display");
#if defined(DISPLAY_WIDTH) && defined(DISPLAY_HEIGHT)
            cJSON_AddNumberToObject(disp, "w", DISPLAY_WIDTH);
            cJSON_AddNumberToObject(disp, "h", DISPLAY_HEIGHT);
#else
            cJSON_AddNumberToObject(disp, "w", 320);
            cJSON_AddNumberToObject(disp, "h", 240);
#endif
        }
        res->addHeader("ETag", etag);
#ifdef BOARD_HAS_PSRAM
        return sendJsonResponsePsram(res, std::move(out), 16384);
#else
        // No-PSRAM: prealloc 16 KB from DRAM often fails when heap is
        // fragmented (largest_free_block < 16 KB even with 50+ KB total free).
        // sendJsonResponse uses cJSON_Print which grows incrementally —
        // peak alloc equals final output size (~1 KB for typical empty
        // configs, ~4-8 KB with many widgets). Survives fragmentation.
        return sendJsonResponse(res, std::move(out));
#endif
    });

    // ── /api/log ──────────────────────────────────────────────────────────
    _server.on("/api/log", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        uint32_t since = 0;
        if (req->hasParam("since"))
            since = (uint32_t)req->getParam("since")->value().toInt();
        if (!_logBuf)
            { NetDiag::httpOk++; return res->send(200, "application/json", "{\"entries\":[]}"); }
        struct Snap { uint32_t seq, ts; char p[192]; };
        Snap *snaps = (Snap *)heap_caps_malloc(LOG_CAP * sizeof(Snap),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!snaps) snaps = (Snap *)malloc(LOG_CAP * sizeof(Snap));
        if (!snaps) { NetDiag::http503Busy++; return res->send(503, "application/json", ERR_BUSY); }
        int nc = 0;
        portENTER_CRITICAL(&_logMux);
        for (int i = 0; i < _logCnt && nc < LOG_CAP; i++) {
            int idx = (_logHead + i) % LOG_CAP;
            if (_logBuf[idx].seq > since) {
                snaps[nc].seq = _logBuf[idx].seq;
                snaps[nc].ts  = _logBuf[idx].ts;
                memcpy(snaps[nc].p, _logBuf[idx].payload, sizeof(snaps[nc].p));
                nc++;
            }
        }
        portEXIT_CRITICAL(&_logMux);
        String out; out.reserve(64 + nc * 210);
        out = "{\"entries\":[";
        for (int i = 0; i < nc; i++) {
            if (i) out += ',';
            out += "{\"seq\":"; out += snaps[i].seq;
            out += ",\"ts\":";  out += snaps[i].ts;
            out += ",\"data\":"; out += snaps[i].p;
            out += '}';
        }
        out += "]}";
        free(snaps);
        NetDiag::httpOk++; return res->send(200, "application/json", out.c_str());
    });

    // ── /api/macros GET ───────────────────────────────────────────────────
    _server.on("/api/macros", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        HEAP_GUARD_P(res);
        // Serve from in-memory cache (always current, avoids NVS size limits).
        std::string snap = Cards::Macros::getCachedJsonCopy();
        NetDiag::httpOk++; return res->send(200, "application/json", snap.c_str());
    });

    // ── /api/widgets GET ──────────────────────────────────────────────────
    _server.on("/api/widgets", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        HEAP_GUARD_P(res);
        std::string snap = Cards::Widgets::getCachedJsonCopy();
        NetDiag::httpOk++; return res->send(200, "application/json", snap.c_str());
    });

    // ── /api/wifi GET ── ?act=scan | status ──────────────────────────────
    _server.on("/api/wifi", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        String act = req->hasParam("act") ? req->getParam("act")->value() : "";
        if (act == "scan") {
            int n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING)
                return res->send(202, "application/json", "{\"scanning\":true}");
            if (n == WIFI_SCAN_FAILED || n < 0) {
                WiFi.scanNetworks(true);
                return res->send(202, "application/json", "{\"scanning\":true}");
            }
            cJsonPtr arr = cJsonCreateArray();
            if (!arr) return res->send(503, "application/json", ERR_OOM);
            for (int i = 0; i < n; i++) {
                cJsonPtr net = cJsonCreateObject();
                if (!net) continue;
                cJSON_AddStringToObject(net.get(), "ssid", WiFi.SSID(i).c_str());
                cJSON_AddNumberToObject(net.get(), "rssi", WiFi.RSSI(i));
                cJSON_AddBoolToObject(net.get(), "open", WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
                cJSON_AddItemToArray(arr.get(), net.release());
            }
            WiFi.scanDelete();
            return sendJsonResponse(res, std::move(arr));
        }
        if (act == "status") {
            cJsonPtr json = cJsonCreateObject();
            if (!json) return res->send(503, "application/json", ERR_OOM);
            cJSON_AddNumberToObject(json.get(), "state", (int)Helper::getState());
            if (Helper::getState() == State::Connected) {
                cJSON_AddStringToObject(json.get(), "ip",   WiFi.localIP().toString().c_str());
                cJSON_AddStringToObject(json.get(), "ssid", WiFi.SSID().c_str());
            }
            cJSON_AddStringToObject(json.get(), "ap_ip", Helper::getAPIP().c_str());
            return sendJsonResponse(res, std::move(json));
        }
        return res->send(400, "application/json", "{\"error\":\"act required (scan|status)\"}");
    });

    // ── /api/settings GET ── ?act=export | (default=get) ─────────────────
    _server.on("/api/settings", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        HEAP_GUARD_P(res);
        String act = req->hasParam("act") ? req->getParam("act")->value() : "";
        if (act == "export") {
            cJsonPtr out = cJsonCreateObject();
            if (!out) return res->send(503, "application/json", ERR_OOM);
            cJSON* outRaw = out.get();
            cJSON_AddNumberToObject(outRaw, "version", 1);
            {
                std::string snap = Cards::Macros::getCachedJsonCopy();
                cJsonPtr arr = cJsonParse(snap.c_str());
                cJSON_AddItemToObject(outRaw, "macros", arr ? arr.release() : cJSON_CreateArray());
            }
            {
                std::string snap = Cards::Widgets::getCachedJsonCopy();
                cJsonPtr arr = cJsonParse(snap.c_str());
                cJSON_AddItemToObject(outRaw, "widgets", arr ? arr.release() : cJSON_CreateArray());
            }
            { cJsonPtr settingsPtr = buildSettingsJson(); cJSON_AddItemToObject(outRaw, "settings", settingsPtr ? settingsPtr.release() : cJSON_CreateObject()); }
            cJsonPtr pList(Cards::Profiles::getList(), cJSON_Delete);
            cJSON_AddItemToObject(outRaw, "profiles", pList ? pList.release() : cJSON_CreateArray());
            cJSON_AddStringToObject(outRaw, "active_profile", Cards::Profiles::getActiveId().c_str());
            return sendJsonResponsePsram(res, std::move(out), 16384);
        }
        // default: get settings
        cJsonPtr json = buildSettingsJson();
        return sendJsonResponse(res, std::move(json));
    });

    // ── Icons ─────────────────────────────────────────────────────────────
#ifdef BOARD_HAS_TF
    _server.on("/api/icons", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        HEAP_GUARD_P(res);
        NetDiag::httpOk++; return res->send(200, "application/json", uIconManager::listJson().c_str());
    });

    _server.on("/api/icons", HTTP_DELETE,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        SERVICE_MODE_GUARD_P(res);
        HEAP_GUARD_P(res);
        if (!req->hasParam("name"))
            return res->send(400, "application/json", "{\"error\":\"name required\"}");
        String name = req->getParam("name")->value();
        String err;
        if (!uIconManager::deleteIcon(name, err))
            { char _eb[160]; makeErrorBuf(_eb, sizeof(_eb), err.c_str()); return res->send(400, "application/json", _eb); }
        NetDiag::httpOk++; return res->send(200, "application/json", "{\"ok\":true}");
    });

    // ── /api/icons POST ── ?act=src (source upload) | (default=upload) ──
    {
        PsychicUploadHandler *iconUploadHandler = new PsychicUploadHandler();
        iconUploadHandler->onUpload([this](PsychicRequest *req, const String &filename,
                                           uint64_t index, uint8_t *data, size_t len,
                                           bool final) -> esp_err_t {
            if (!_serviceMode) return ESP_FAIL;
            bool isSrc = req->hasParam("act") && req->getParam("act")->value() == "src";
            if (isSrc)
                return handleIconSrcUploadChunk(filename, (size_t)index, data, len, final) ? ESP_OK : ESP_FAIL;
            return handleIconUploadChunk(filename, (size_t)index, data, len, final) ? ESP_OK : ESP_FAIL;
        });
        iconUploadHandler->onRequest([this](PsychicRequest *req,
                                            PsychicResponse *res) -> esp_err_t {
            SERVICE_MODE_GUARD_P(res);
            bool isSrc = req->hasParam("act") && req->getParam("act")->value() == "src";
            if (isSrc) {
                if (_iconSrcUploadOk) { _iconSrcUploadOk = false; NetDiag::httpOk++; return res->send(200, "application/json", "{\"ok\":true}"); }
                return res->send(500, "application/json", "{\"error\":\"upload failed\"}");
            }
            if (_iconUploadOk) { _iconUploadOk = false; NetDiag::httpOk++; return res->send(200, "application/json", "{\"ok\":true}"); }
            return res->send(500, "application/json", "{\"error\":\"upload failed\"}");
        });
        _server.on("/api/icons", HTTP_POST, iconUploadHandler);
    }

    // Single "/icons/*" handler that serves both /icons/<name> (display
    // variants) and /icons/src/<name> (compressed source). PsychicHttp
    // wildcard routes match first-registered-wins, and /icons/* would
    // swallow /icons/src/* if registered first — merge into one to avoid
    // the ordering trap entirely.
    _server.on("/icons/*", HTTP_GET,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        // PWA icons are small LittleFS files, not SD-card display variants.
        // Serve them directly before the SD-card path and the heap guard.
        const String& url0 = req->path();
        if (url0 == "/icons/icon-192.png" || url0 == "/icons/icon-512.png") {
            PsychicFileResponse fileResp(res, LittleFS, "/www" + url0);
            fileResp.addHeader("Cache-Control", "public, max-age=86400");
            return fileResp.send();
        }
        HEAP_GUARD_P(res);
        if (!uSD::isAvailable()) { NetDiag::http503Busy++; return res->send(503); }
        String url = req->path();
        if (url.indexOf("..") >= 0) return res->send(400);

        // Branch: source file request?
        bool isSrc = url.startsWith("/icons/src/");
        String name = url.substring(url.lastIndexOf('/') + 1);
        String path;
        if (isSrc) {
            // SPA references sources as <base>.bin (legacy compat key) but we
            // now store the original compressed file (PNG/JPG/etc). Prefer
            // compressed variants FIRST so browser <img> decodes natively —
            // fall back to the exact .bin name only as a last resort (legacy
            // installs with old ARGB8888 .bin sources still on SD).
            int dot = name.lastIndexOf('.');
            String base = (dot > 0) ? name.substring(0, dot) : name;
            const char* exts[] = { ".png", ".jpg", ".jpeg", ".svg", ".webp", ".gif" };
            bool found = false;
            for (const char* ext : exts) {
                String alt = "/icons/src/" + base + ext;
                if (uSD::getFS().exists(alt)) {
                    path = alt;
                    name = base + ext;
                    found = true;
                    break;
                }
            }
            if (!found) {
                path = "/icons/src/" + name;
                if (!uSD::getFS().exists(path)) return res->send(404);
            }
        } else {
            path = "/icons/" + name;
            if (!uSD::getFS().exists(path)) return res->send(404);
            // Only LVGL .bin display variants here. Legacy .png returns 415
            // so stale references surface instead of silently 200-rendering.
            String lower = name; lower.toLowerCase();
            if (!lower.endsWith(".bin")) return res->send(415);
        }

        File f = uSD::getFS().open(path, FILE_READ);
        if (!f) return res->send(500);
        size_t sz = f.size();
        // 200 KB cap matches handleIconUploadChunk; src path uses the
        // larger 320 KB cap implicitly via SD read (no re-check needed).
        if (!isSrc && sz > 200 * 1024) { f.close(); return res->send(413); }

        uSD::setSdServing(true);
        // MIME: image/* for source files so <img> decodes natively,
        // octet-stream for .bin variants.
        String lower = name; lower.toLowerCase();
        const char *mime = "application/octet-stream";
        if      (lower.endsWith(".png"))  mime = "image/png";
        else if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) mime = "image/jpeg";
        else if (lower.endsWith(".svg"))  mime = "image/svg+xml";
        else if (lower.endsWith(".webp")) mime = "image/webp";
        else if (lower.endsWith(".gif"))  mime = "image/gif";
        PsychicFileResponse fileRes(res, f, name, mime);
        esp_err_t err = fileRes.send();
        uSD::setSdServing(false);
        return err;
    });
#else
    _server.on("/api/icons", HTTP_GET,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        NetDiag::httpOk++; return res->send(200, "application/json", "[]"); });
    _server.on("/api/icons", HTTP_DELETE,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        NetDiag::http503Busy++; return res->send(503, "application/json", "{\"error\":\"no SD card\"}"); });
    _server.on("/api/icons", HTTP_POST,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        NetDiag::http503Busy++; return res->send(503, "application/json", "{\"error\":\"no SD card\"}"); });
#endif

    // ── Backgrounds ───────────────────────────────────────────────────────
#ifdef BOARD_HAS_TF
    // ── /api/backgrounds GET ── ?act=scene-bg | (default=list) ──────────
    _server.on("/api/backgrounds", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        HEAP_GUARD_P(res);
        String act = req->hasParam("act") ? req->getParam("act")->value() : "";
        if (act == "scene-bg")
            { NetDiag::httpOk++; return res->send(200, "application/json", uBackgroundManager::getSceneBgsJson().c_str()); }
        NetDiag::httpOk++; return res->send(200, "application/json", uBackgroundManager::listJson().c_str());
    });

    _server.on("/api/backgrounds", HTTP_DELETE,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        SERVICE_MODE_GUARD_P(res);
        HEAP_GUARD_P(res);
        if (!req->hasParam("name"))
            return res->send(400, "application/json", "{\"error\":\"name required\"}");
        String name = req->getParam("name")->value();
        String err;
        if (!uBackgroundManager::deleteBackground(name, err))
            { char _eb[160]; makeErrorBuf(_eb, sizeof(_eb), err.c_str()); return res->send(400, "application/json", _eb); }
        NetDiag::httpOk++; return res->send(200, "application/json", "{\"ok\":true}");
    });

    {
        PsychicUploadHandler *bgUploadHandler = new PsychicUploadHandler();
        bgUploadHandler->onUpload([this](PsychicRequest *req, const String &filename,
                                         uint64_t index, uint8_t *data, size_t len,
                                         bool final) -> esp_err_t {
            if (!_serviceMode) return ESP_FAIL;
            return handleBgUploadChunk(filename, (size_t)index, data, len, final) ? ESP_OK : ESP_FAIL;
        });
        bgUploadHandler->onRequest([this](PsychicRequest *req,
                                          PsychicResponse *res) -> esp_err_t {
            SERVICE_MODE_GUARD_P(res);
            // default: file upload result
            if (_bgUploadOk) {
                _bgUploadOk = false;
                NetDiag::httpOk++; return res->send(200, "application/json", "{\"ok\":true}");
            }
            ESP_LOGE("BG", "onRequest: upload not OK (_bgUploadOk=false)");
            return res->send(500, "application/json", "{\"error\":\"upload failed\"}");
        });
        _server.on("/api/backgrounds", HTTP_POST, bgUploadHandler);
    }

    _server.on("/backgrounds/*", HTTP_GET,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        HEAP_GUARD_P(res);
        if (!uSD::isAvailable()) { NetDiag::http503Busy++; return res->send(503); }
        String url  = req->path();
        String name = url.substring(url.lastIndexOf('/') + 1);
        if (name.indexOf("..") >= 0) return res->send(400);
        String path = "/backgrounds/" + name;
        if (!uSD::getFS().exists(path)) return res->send(404);
        // Only JPEG backgrounds are served. Legacy .png returns 415 to surface
        // stale references instead of silently failing in the LVGL decoder.
        String lower = name; lower.toLowerCase();
        const char *mime = "image/jpeg";
        if (!lower.endsWith(".jpg") && !lower.endsWith(".jpeg")) return res->send(415);
        File f = uSD::getFS().open(path, FILE_READ);
        if (!f) return res->send(500);
        size_t sz = f.size();
        if (sz > 200000) { f.close(); return res->send(413); }
        // Stream file in FILE_CHUNK_SIZE (1024 B) chunks — avoids OOM on large backgrounds.
        // Suspend widget HTTP fetches to preserve heap for the TCP stack during transfer.
        uSD::setSdServing(true);
        PsychicFileResponse fileRes(res, f, name, mime);
        fileRes.addHeader("Cache-Control", "public, max-age=86400");
        esp_err_t err = fileRes.send();
        uSD::setSdServing(false);
        return err;
    });

#else
    _server.on("/api/backgrounds", HTTP_GET,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        if (req->hasParam("act") && req->getParam("act")->value() == "scene-bg")
            { NetDiag::httpOk++; return res->send(200, "application/json", "{}"); }
        NetDiag::httpOk++; return res->send(200, "application/json", "[]"); });
    _server.on("/api/backgrounds", HTTP_DELETE,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        NetDiag::http503Busy++; return res->send(503, "application/json", "{\"error\":\"no SD card\"}"); });
    _server.on("/api/backgrounds", HTTP_POST,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        NetDiag::http503Busy++; return res->send(503, "application/json", "{\"error\":\"no SD card\"}"); });
#endif

    // ── /api/profiles GET ─────────────────────────────────────────────────
    _server.on("/api/profiles", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        HEAP_GUARD_P(res);
        cJsonPtr out = cJsonCreateObject();
        if (!out) return res->send(503, "application/json", ERR_OOM);
        cJsonPtr list(Cards::Profiles::getList(), cJSON_Delete);
        cJSON_AddItemToObject(out.get(), "list", list ? list.release() : cJSON_CreateArray());
        cJSON_AddStringToObject(out.get(), "active_id", Cards::Profiles::getActiveId().c_str());
        return sendJsonResponse(res, std::move(out));
    });

#ifdef OTA_ENABLED
    // ── /api/ota ──────────────────────────────────────────────────────────
    {
        PsychicUploadHandler *otaHandler = new PsychicUploadHandler();
        otaHandler->onUpload([this](PsychicRequest * /*req*/, const String & /*filename*/,
                                uint64_t index, uint8_t *data, size_t len,
                                bool final) -> esp_err_t {
            // Refuse OTA payload outside service mode — abort silently so
            // the uploader sees 403 on onRequest completion.
            if (!_serviceMode) return ESP_FAIL;
            if (index == 0) {
                ESP_LOGI("WiFi.OTA", "OTA begin");
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
                    ESP_LOGE("WiFi.OTA", "Update.begin failed");
            }
            if (Update.isRunning()) {
                if (Update.write(data, len) != len)
                    ESP_LOGE("WiFi.OTA", "Update.write error");
            }
            if (final) {
                if (!Update.end(true)) ESP_LOGE("WiFi.OTA", "Update.end failed");
                else                   ESP_LOGI("WiFi.OTA", "OTA complete");
            }
            return ESP_OK;
        });
        otaHandler->onRequest([this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
            SERVICE_MODE_GUARD_P(res);
            if (!Update.hasError()) {
                esp_err_t e = res->send(200, "application/json", "{\"ok\":true}");
                if (e == ESP_OK) NetDiag::httpOk++;
                delay(300);
                esp_restart();
                return e;
            }
            return res->send(500, "application/json", "{\"error\":\"OTA update failed\"}");
        });
        _server.on("/api/ota", HTTP_POST, otaHandler);
    }
#endif

    // ── OPTIONS (CORS preflight) ──────────────────────────────────────────
    // CORS Allow-Origin and Connection: close added by global middleware.
    _server.on("/*", HTTP_OPTIONS,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        res->addHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
        res->addHeader("Access-Control-Allow-Headers", "Content-Type");
        return res->send(204);
    });

    // ── Static file serving ───────────────────────────────────────────────────
    //
    // Problem: PsychicStaticFileHandler uses PsychicFileResponse which calls
    // LittleFS File::readBytes() inside the chunk-send loop.  On ESP32 with
    // BLE coexistence, that LittleFS read can stall for 1-2 s at arbitrary
    // offsets (likely a FreeRTOS scheduling/FS lock interaction), causing the
    // entire chunked transfer to block and eventually the send_wait_timeout
    // to fire, returning a truncated file to the browser.
    //
    // Fix: PsramStaticHandler reads the ENTIRE file into PSRAM in one shot
    // (LittleFS I/O is done before the TCP send loop starts), then streams
    // from the PSRAM buffer.  PSRAM is 8 MB; all web assets are ~200 KB, so
    // there is always room.  DRAM is unaffected.
    //
    // PsramStaticHandler is registered FIRST so it takes priority for
    // /assets/* requests.  The built-in serveStatic("/", …) still handles
    // index.html, manifest, sw.js, etc. (small files, no stall risk).

    class PsramStaticHandler : public PsychicHandler {
        volatile uint32_t *_tsPtr;
    public:
        explicit PsramStaticHandler(volatile uint32_t *tsPtr) : _tsPtr(tsPtr) {}

        bool canHandle(PsychicRequest *req) override {
            if (req->method() != HTTP_GET) return false;
            const String &uri = req->path();
            // Never intercept API or WebSocket paths.
            if (uri.startsWith("/api/") || uri.startsWith("/ws")) return false;
            // Icons are small PNGs (~900 B). heap_caps_malloc(SPIRAM) fails on
            // no-PSRAM boards; serveStatic handles these fine without chunking.
            if (uri.startsWith("/icons/")) return false;
            // Fast path — assets always go through PSRAM streaming.
            if (uri.startsWith("/assets/")) return true;
            // Any other path — only claim it if the file actually exists in /www/.
            String base = "/www" + uri;
            return LittleFS.exists(base) || LittleFS.exists(base + ".gz");
        }

        esp_err_t handleRequest(PsychicRequest *req, PsychicResponse *res) override {
            // Keep isWebServing() true while assets are transferring so widget
            // HTTP workers don't compete for DRAM during page load.
            if (_tsPtr) *_tsPtr = millis();

            String uri = req->path();
            if (uri == "/") uri = "/index.html";

            String fsBase  = "/www" + uri;
            String fsGz    = fsBase + ".gz";
            bool   useGz   = LittleFS.exists(fsGz);
            String fsPath  = useGz ? fsGz : fsBase;

            if (!LittleFS.exists(fsPath)) {
                ESP_LOGW("HTTP", "PSRAM asset 404: %s", uri.c_str());
                return res->send(404);
            }

            File f = LittleFS.open(fsPath, "r");
            if (!f) return res->send(500);

            size_t fileSize = f.size();

            // Heap-aware gate: small assets (<4 KB) pass through with modest heap;
            // large assets need generous headroom to avoid a sendChunk stall that
            // would lock the single httpd task permanently (observed: failed 47 KB
            // stream with heap dropping to 4.8 KB → httpd accepts no new sockets
            // for 90+ s, lwIP 'create ping task failed' errors). Reject fast —
            // browser will retry after DNS cache or widget backoff frees heap.
            uint32_t freeH = ESP.getFreeHeap();
            uint32_t needHeap = (fileSize < 4096) ? 6000 : 15000;
            if (freeH < needHeap) {
                f.close();
                NetDiag::http503HeapGuard++;
                ESP_LOGW("HTTP", "503 heap: %s size=%u heap=%u need=%u",
                         uri.c_str(), (unsigned)fileSize, freeH, needHeap);
                return res->send(503, "application/json", ERR_BUSY);
            }
            String etag     = String(fileSize);

            // ── 304 check ────────────────────────────────────────────────
            if (req->hasHeader("If-None-Match") &&
                req->header("If-None-Match").equals(etag)) {
                f.close();
                res->addHeader("Cache-Control", "public, max-age=31536000, immutable");
                res->addHeader("ETag", etag.c_str());
                res->setCode(304);
                return res->send();
            }

            // ── MIME type ─────────────────────────────────────────────────
            const char *mime = "application/octet-stream";
            if      (uri.endsWith(".js"))   mime = "application/javascript";
            else if (uri.endsWith(".css"))  mime = "text/css";
            else if (uri.endsWith(".html")) mime = "text/html";
            else if (uri.endsWith(".svg"))  mime = "image/svg+xml";
            else if (uri.endsWith(".png"))  mime = "image/png";
            else if (uri.endsWith(".ico"))  mime = "image/x-icon";
            else if (uri.endsWith(".json"))        mime = "application/json";
            else if (uri.endsWith(".webmanifest")) mime = "application/manifest+json";
            else if (uri.endsWith(".woff2"))mime = "font/woff2";
            else if (uri.endsWith(".woff")) mime = "font/woff";
            else if (uri.endsWith(".ttf"))  mime = "font/ttf";

            // ── Small-file DRAM one-shot path ─────────────────────────────
            // Files <= 4 KB (sw.js.gz, manifest.gz, favicon.svg.gz, index.html.gz)
            // fit in one TCP send. Read whole file into DRAM, send once — no
            // chunking, no PSRAM. Allows no-PSRAM boards to serve root assets
            // without 503. Threshold = 4 KB matches the heap-gate small-file tier.
            if (fileSize <= 4096) {
                uint8_t *dram = (uint8_t *)malloc(fileSize);
                if (!dram) {
                    f.close();
                    ESP_LOGE("HTTP", "DRAM small-buf OOM for %s (size=%u)", fsPath.c_str(), (unsigned)fileSize);
                    return res->send(503, "application/json", ERR_OOM);
                }
                size_t nRead = f.readBytes((char *)dram, fileSize);
                f.close();
                if (nRead != fileSize) {
                    free(dram);
                    ESP_LOGE("HTTP", "Short read %u/%u for %s", (unsigned)nRead, (unsigned)fileSize, fsPath.c_str());
                    return res->send(500);
                }
                if (useGz) res->addHeader("Content-Encoding", "gzip");
                res->addHeader("Cache-Control", "public, max-age=31536000, immutable");
                res->addHeader("ETag", etag.c_str());
                esp_err_t err = res->send(200, mime, dram, fileSize);
                free(dram);
                NetDiag::httpOk++;
                return err;
            }

            // ── True chunked streaming ────────────────────────────────────
            // Per-request 8 KB PSRAM chunk buffer — no full-file pre-alloc.
            // Unlimited file size: loop read→sendChunk until EOF or 30 s timeout.
            static constexpr size_t CHUNK_BUF = 8u * 1024u;
            // PSRAM only — DRAM fallback would pull 8 KB from already-stressed heap.
            // On no-PSRAM boards (Rv2/Rv3/R) large files (>4 KB) fail here → 503.
            // Small files (<= 4 KB) handled by DRAM one-shot path above.
            uint8_t *buf = (uint8_t *)heap_caps_malloc(CHUNK_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buf) {
                f.close();
                ESP_LOGE("HTTP", "PSRAM chunk buf OOM for %s", fsPath.c_str());
                return res->send(503, "application/json", ERR_OOM);
            }

            ESP_LOGI("HTTP", "Streaming %s  size=%u  heap=%u",
                     uri.c_str(), (unsigned)fileSize, ESP.getFreeHeap());

            res->setCode(200);
            res->setContentType(mime);
            if (useGz) res->addHeader("Content-Encoding", "gzip");
            res->addHeader("Cache-Control", "public, max-age=31536000, immutable");
            res->addHeader("ETag", etag.c_str());
            res->sendHeaders();

            esp_err_t err = ESP_OK;
            // 8 s deadline — shorter than 30 s prevents httpd task from being
            // wedged by a stuck client for too long. Large assets that fit
            // <8 s at link speed (typical: 47 KB / ~3 KB/s = 16 s under load)
            // may hit deadline; on failure the sendChunk returns non-OK and
            // loop exits immediately.
            uint32_t deadline = millis() + 8000;
            while (f.available() && err == ESP_OK) {
                if (millis() > deadline) {
                    ESP_LOGW("HTTP", "Stream timeout for %s", fsPath.c_str());
                    break;
                }
                if (_tsPtr) *_tsPtr = millis();
                size_t n = f.readBytes((char *)buf, CHUNK_BUF);
                if (n == 0) break;
                err = res->sendChunk(buf, n);
            }
            if (err == ESP_OK) err = res->finishChunking();
            free(buf);
            f.close();

            if (err != ESP_OK) {
                ESP_LOGE("HTTP", "Stream send failed: %s  err=0x%x", uri.c_str(), (unsigned)err);
            }
            return err;
        }
    };

    // Heap-allocated so PsychicHttp can delete it on server shutdown.
    _server.addHandler(new PsramStaticHandler(&_webServeTimestamp));

    // ── PWA manifest & service worker ─────────────────────────────────────────
    _server.on("/manifest.json", HTTP_GET,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        res->addHeader("Cache-Control", "public, max-age=86400");
        return res->send(200, "application/manifest+json", MANIFEST_JSON);
    });
    _server.on("/proxy-sw.js", HTTP_GET,
               [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        res->addHeader("Cache-Control", "no-cache");
        res->addHeader("Service-Worker-Allowed", "/");
        return res->send(200, "application/javascript", PROXY_SW_JS);
    });

    // ── Root handler: proxy page (normal) or Vue SPA (service mode) ──────────
    // Must be registered BEFORE serveStatic("/", ...) so it takes priority for "/".
    _server.on("/", HTTP_GET,
               [this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        if (_serviceMode) {
            _webServeTimestamp = millis();
            // Shell HTML: device serves a ~2 KB gzip page; the shell JS then
            // loads app.js from CDN (jsdelivr/statically/githack) and falls
            // back to /assets/app.js on this device. Works on both PSRAM and
            // no-PSRAM; CDN is the primary delivery path on no-PSRAM.
            res->addHeader("Content-Encoding", "gzip");
            res->addHeader("Cache-Control", "no-cache");
#ifndef BOARD_HAS_PSRAM
            return res->send(200, "text/html",
                             SERVICE_SHELL_HTML_GZ,
                             SERVICE_SHELL_HTML_GZ_LEN);
#else
            // PSRAM: CDN shell as well (consistent behaviour across boards).
            return res->send(200, "text/html",
                             SERVICE_SHELL_HTML_GZ,
                             SERVICE_SHELL_HTML_GZ_LEN);
#endif
        }
        NetDiag::httpOk++;
        res->addHeader("Content-Encoding", "gzip");
        return res->send(200, "text/html", PROXY_PAGE_HTML_GZ, PROXY_PAGE_HTML_GZ_LEN);
    });

    // Built-in serveStatic handles root-level files (manifest, sw.js, favicon …)
    // and acts as fallback for any /assets/ misses not caught by psramHandler.
    _server.serveStatic("/assets/", LittleFS, "/www/assets/")
           ->setCacheControl("public, max-age=31536000, immutable");
    _server.serveStatic("/", LittleFS, "/www/")
           ->setCacheControl("no-cache");

    // ── SPA fallback / catch-all ──────────────────────────────────────────
    // Called when NO registered handler (including serveStatic) matched the URI.
    _server.onNotFound([this](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
        const String &url = req->path();
        ESP_LOGW("HTTP", "onNotFound: %s %s  heap=%u",
                 req->methodStr().c_str(), url.c_str(), ESP.getFreeHeap());

        if (req->method() == HTTP_GET) {
            // SPA route (no dot → not a static file)
            if (url.indexOf('.') < 0) {
                if (!_serviceMode) {
                    // Normal mode: any SPA-like path returns the lightweight proxy page
                    NetDiag::httpOk++;
                    res->addHeader("Content-Encoding", "gzip");
                    return res->send(200, "text/html", PROXY_PAGE_HTML_GZ, PROXY_PAGE_HTML_GZ_LEN);
                }
                _webServeTimestamp = millis();
                res->addHeader("Content-Encoding", "gzip");
                res->addHeader("Cache-Control", "no-cache");
                return res->send(200, "text/html",
                                 SERVICE_SHELL_HTML_GZ,
                                 SERVICE_SHELL_HTML_GZ_LEN);
            }
            // File with extension but not served by serveStatic — check LittleFS directly
            String fsPath = "/www" + url;
            bool exists   = LittleFS.exists(fsPath);
            bool existsGz = LittleFS.exists(fsPath + ".gz");
            ESP_LOGW("HTTP", "Static miss: %s  fs=%d  fs.gz=%d", fsPath.c_str(), exists, existsGz);
        }

        // Web UI not uploaded at all?
        if (!LittleFS.exists("/www/index.html") && !LittleFS.exists("/www/index.html.gz")) {
            ESP_LOGE("HTTP", "No /www/index.html — run 'pio run -t uploadfs'");
            NetDiag::httpOk++; return res->send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<title>CYD Dashboard</title></head><body>"
                "<h2>CYD Dashboard — Web UI not uploaded yet</h2>"
                "<p>Run: <code>pio run -t uploadfs</code></p>"
                "</body></html>");
        }
        return res->send(404, "application/json", "{\"error\":\"not found\"}");
    });

    // ── httpd task configuration — must be set before start() ────────────
    _server.config.stack_size        = 8192;  // deepest path: /api/init → buildSettingsJson(Preferences) → cJSON_Parse × 2 → cJSON_PrintPreallocated uses ~7-8 KB stack
    _server.config.max_uri_handlers  = 32;    // ~25 handlers after consolidation (BOARD_HAS_TF + OTA)
    // DRAM budget: TCP_SND_BUF + TCP_WND = 1460+1460 = 2920 B per socket (1×MSS).
    //   1×MSS is safe because PsramStaticHandler reads entire files into PSRAM before
    //   the TCP send loop, so sendChunk() is never blocked by LittleFS I/O.  Each
    //   1024-byte chunk leaves 436 B free in SND_BUF — enough for finishChunking()
    //   even with one unACKed chunk in-flight.
    //
    //   PSRAM boards (e.g. JC4827W543C): DRAM idle ~100 KB → 4 slots × 2920 = 11680 B.
    //   Allows one extra concurrent request (e.g. WS + index.html + JS bundle + API),
    //   cutting the parallel-burst queue depth under browser page load.
    //
    //   No-PSRAM boards (e.g. 2432S028Rv2): DRAM idle ~17 KB → 3 slots × 2920 = 8760 B.
    //   Bumping to 4 on these boards risks heap exhaustion + FreeRTOS priority-
    //   inheritance mutex asserts (crash #270+ history). 3 is the safe ceiling.
    //   lru_purge_enable handles the occasional Connection: close lag gracefully.
#ifdef BOARD_HAS_PSRAM
    _server.config.max_open_sockets  = 4;     // 4 × 2920 = 11680 B DRAM (PSRAM board)
#else
    _server.config.max_open_sockets  = 3;     // 3 × 2920 = 8760 B DRAM (no-PSRAM board)
#endif
    _server.config.backlog_conn      = 2;
    _server.config.lru_purge_enable  = true;  // evict idle sockets when limit is hit
    _server.config.send_wait_timeout = 5;     // 5 s — reclaims stuck worker slots
                                              // without race-closing active sends
    _server.config.recv_wait_timeout = 10;    // symmetrical with send timeout
    // maxRequestBodySize: safety cap for non-upload POST bodies. JSON writes
    // moved to WebSocket; remaining POST bodies are small status payloads.
    // 128 KB retained to not constrain future needs.
    _server.maxRequestBodySize       = 128 * 1024;
    // maxUploadSize: global cap for PsychicUploadHandler (checked before any
    // endpoint-specific handler runs). Must be >= the largest per-endpoint
    // limit (icon src = 256 KB, bg = unlimited, OTA = firmware size). Use
    // 2 MB so the global check never rejects before our chunk handlers
    // apply their own size guards.
    _server.maxUploadSize            = 2 * 1024 * 1024;

#ifdef PSYCHIC_WS_RX_STATIC_BUFFER
    psychic_ws_preinit_rx_buf();  // allocate WS RX buffer now while heap is fresh
#endif
    Serial.printf("[HTTP] calling _server.start()  heap=%u\n", ESP.getFreeHeap());
    esp_err_t startErr = _server.start();
    if (startErr != ESP_OK) {
        Serial.printf("[HTTP] _server.start() FAILED  err=0x%x (%s)\n",
                      (unsigned)startErr, esp_err_to_name(startErr));
    } else {
        Serial.printf("[HTTP] Server RUNNING\n");
        Serial.printf("[HTTP]   AP  http://%s/\n",
                      WiFiModule::WiFiClient.softAPIP().toString().c_str());
        Serial.printf("[HTTP]   STA http://%s/  (0.0.0.0 = not connected)\n",
                      WiFiModule::WiFiClient.localIP().toString().c_str());
        Serial.printf("[HTTP]   max_open_sockets=%d  send_wait=%ds  heap=%u\n",
                      (int)_server.config.max_open_sockets,
                      (int)_server.config.send_wait_timeout,
                      ESP.getFreeHeap());
    }
}

void PsychicHttpImpl::startHttpServer()
{
    if (_server.isRunning()) return;
    Serial.printf("[HTTP] Retrying server start  heap=%u\n", ESP.getFreeHeap());
    esp_err_t err = _server.start();
    if (err != ESP_OK) {
        Serial.printf("[HTTP] Retry failed  err=0x%x (%s)\n",
                      (unsigned)err, esp_err_to_name(err));
    } else {
        Serial.printf("[HTTP] Server RUNNING (deferred start)  heap=%u\n", ESP.getFreeHeap());
    }
}

} // namespace WiFiModule

#endif // USE_PSYCHIC_HTTP
