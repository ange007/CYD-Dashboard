#include "server_async.h"
#include "api.h"
#include "asset_streamer.h"
#include "helpers.h"
#include "net_diag.h"

#include <Arduino.h>
#include <cJSON.h>
#include <map>
#include <vector>

#ifdef ESP32
  #include <WiFi.h>
  #include <AsyncTCP.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#ifdef OTA_ENABLED
  #include <Update.h>
#endif

#include "../../constants.h"
#include "../../ui/ui.h"
#include "../../utils/memory.h"
#include "../../utils/ui.h"
#include "../../utils/json.h"
#include "../../utils/heap_probe.h"
#include "../../utils/settings.h"
#include "../../utils/lvfs_littlefs.h"
#include "../../utils/sd.h"
#include "../../utils/icons.h"
#include "../../utils/backgrounds.h"
#include "../../utils/lvfs_sd.h"

#include "../cards/widgets.h"
#include "../cards/macros.h"
#include "../cards/profiles.h"
#include "../hid/keyboard.h"

namespace WiFiModule
{

// ── Heap guard macros ─────────────────────────────────────────────────────────
// Threshold calibrated for NimBLE PSRAM enabled:
//   Stable idle heap with BLE connected ≈ 10 KB → must be below that.
//   During BLE setup transient (~6 KB), browser gets 503 and retries.
static constexpr uint32_t HEAP_GUARD_THRESHOLD = 10000;
#define HEAP_GUARD(req)                                                                                     \
    if (ESP.getFreeHeap() < HEAP_GUARD_THRESHOLD)                                                           \
    {                                                                                                       \
        NetDiag::http503HeapGuard++;                                                                        \
        AsyncWebServerResponse *_r = (req)->beginResponse(503, "application/json", "{\"error\":\"busy\"}"); \
        _r->addHeader("Connection", "close");                                                               \
        _r->addHeader("Retry-After", "1");                                                                  \
        (req)->send(_r);                                                                                    \
        return;                                                                                             \
    }

static inline void sendJsonResponse(AsyncWebServerRequest* req, cJsonPtr&& root)
{
    cJsonStrPtr s = cJsonPrint(root.get());
    if (!s) { req->send(503, "application/json", "{\"error\":\"oom\"}"); return; }
    AsyncWebServerResponse* r = req->beginResponse(200, "application/json", s.get());
    if (!r) { req->send(503, "application/json", "{\"error\":\"oom\"}"); return; }
    r->addHeader("Connection", "close");
    NetDiag::httpOk++;
    req->send(r);
}

// ── Static-file sender ────────────────────────────────────────────────────────
void AsyncWebServerImpl::sendStatic(AsyncWebServerRequest *req, const String &fsPath)
{
    _webServeTimestamp = millis();
    req->client()->setRxTimeout(30);

    bool isGz = fsPath.endsWith(".gz");
    AsyncWebServerResponse *resp =
        req->beginResponse(LittleFS, fsPath, mimeFor(fsPath));

    if (!resp)
    {
        ESP_LOGE("HTTP.FS", "beginResponse failed for %s", fsPath.c_str());
        req->send(500, "text/plain", "File open error");
        return;
    }

    if (isGz)
        resp->addHeader("Content-Encoding", "gzip");
    if (fsPath.indexOf("/assets/") >= 0)
        resp->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    else
        resp->addHeader("Cache-Control", "no-cache");
    resp->addHeader("Connection", "close");
    req->send(resp);
}

// ── WS queue check ────────────────────────────────────────────────────────────
bool AsyncWebServerImpl::wsClientQueueFull(uint32_t clientId)
{
    AsyncWebSocketClient *c = _ws.client(clientId);
    return c && c->queueIsFull();
}

// ── Multi-frame WS reassembly ─────────────────────────────────────────────────
void AsyncWebServerImpl::handleWsData(AsyncWebSocketClient *client,
                                      void *arg, uint8_t *data, size_t len)
{
    AwsFrameInfo *info     = (AwsFrameInfo *)arg;
    if (info->opcode != WS_TEXT) return;

    uint32_t clientId = client->id();

    // Fast path — single frame
    if (info->final && info->index == 0 && info->len == len)
    {
        char *msg = new (std::nothrow) char[len + 1];
        if (!msg) return;
        memcpy(msg, data, len);
        msg[len] = '\0';
        ESP_LOGI("WiFi.WSS", "Message content: %.512s%s", msg,
                 len > 512 ? "...(truncated in log)" : "");
        processWsMessage(clientId, msg);
        delete[] msg;
        return;
    }

    // Multi-frame reassembly
    if (info->index == 0)
    {
        if (info->len > WS_MSG_MAX)
        {
            ESP_LOGW("WiFi.WSS", "WS #%u message too large (%u B > %u B) — dropping",
                     clientId, (unsigned)info->len, (unsigned)WS_MSG_MAX);
            _wsFragBuf.erase(clientId);
            return;
        }
        _wsFragBuf[clientId].clear();
        _wsFragBuf[clientId].reserve((size_t)info->len);
    }

    auto it = _wsFragBuf.find(clientId);
    if (it == _wsFragBuf.end()) return;

    it->second.insert(it->second.end(), data, data + len);

    if (!info->final || (info->index + len) < info->len) return;

    std::vector<uint8_t> &buf = it->second;
    buf.push_back('\0');
    char *msg = reinterpret_cast<char *>(buf.data());
    ESP_LOGI("WiFi.WSS", "Message content (reassembled %u B): %.512s%s",
             (unsigned)(buf.size() - 1), msg,
             buf.size() > 513 ? "...(truncated in log)" : "");
    processWsMessage(clientId, msg);
    _wsFragBuf.erase(clientId);
}

// ── WS event handler ──────────────────────────────────────────────────────────
void AsyncWebServerImpl::onWsEvent(AsyncWebSocket * /*server*/,
                                   AsyncWebSocketClient *client,
                                   AwsEventType type, void *arg,
                                   uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        ESP_LOGI("WiFi.WSS", "WS #%u connected from %s  heap=%u  ws_total=%u",
                 client->id(), client->remoteIP().toString().c_str(),
                 ESP.getFreeHeap(), (unsigned)_ws.count());
        if (_ws.count() > WS_MAX_CLIENTS) {
            ESP_LOGW("WiFi.WSS", "WS #%u rejected — client cap (%u) reached",
                     client->id(), (unsigned)WS_MAX_CLIENTS);
            client->close(1013, "too many clients");
            return;
        }
        if (_proxyClientId == 0)
        {
            _proxyClientId         = client->id();
            _pending_widgets_refresh = true;
        }
        updateConnectionIcon();
        break;

    case WS_EVT_DISCONNECT:
    {
        const char *role = (client->id() == _companionClientId) ? "companion"
                           : (client->id() == _proxyClientId)  ? "proxy"
                                                                : "unknown";
        ESP_LOGW("WiFi.WSS", "WS #%u (%s) disconnected  heap=%u  ws_total=%u",
                 client->id(), role, ESP.getFreeHeap(), (unsigned)_ws.count());
        _wsFragBuf.erase(client->id());
        if (client->id() == _proxyClientId)   { _proxyClientId = 0; API::clearProxyInflight(); }
        if (client->id() == _companionClientId) {
            _companionClientId = 0;
            HidKeyboard::setCompanionHidReady(false, 0);
        }
        AssetStreamer::onClientClose(client->id());
        updateConnectionIcon();
        break;
    }

    case WS_EVT_DATA:
        handleWsData(client, arg, data, len);
        break;

    case WS_EVT_ERROR:
        ESP_LOGE("WiFi.WSS", "WS #%u error %u: %s",
                 client->id(),
                 arg ? *static_cast<uint16_t *>(arg) : 0,
                 data ? reinterpret_cast<char *>(data) : "");
        break;

    case WS_EVT_PONG:
        break;
    }
}

// ── start() — register all routes ────────────────────────────────────────────
void AsyncWebServerImpl::start()
{
    // Register LVGL filesystem drivers and build icon cache BEFORE Macros::init()
    // so that cacheContains() returns correct results during initial button render.
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

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Allocate log ring buffer (prefer PSRAM)
    _logBuf = static_cast<LogEntry *>(
        ESP.getFreePsram() > 0
            ? ps_malloc(LOG_CAP * sizeof(LogEntry))
            : malloc(LOG_CAP * sizeof(LogEntry)));
    if (_logBuf) memset(_logBuf, 0, LOG_CAP * sizeof(LogEntry));

    _ws.onEvent([this](AsyncWebSocket *s, AsyncWebSocketClient *c,
                       AwsEventType t, void *a, uint8_t *d, size_t l) {
        onWsEvent(s, c, t, a, d, l);
    });
    _server.addHandler(new HttpLoggingHandler(this));
    _server.addHandler(&_ws);

    // ── /api/health ── ?act=ping | (default=status) ────────────────────
    _server.on("/api/health", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        String act = req->hasParam("act") ? req->getParam("act")->value() : "";
        if (act == "ping") { req->send(200, "application/json", "{\"ok\":true}"); return; }
        // default: health status
        char buf[256];
        uint32_t crashCount = 0, lastReason = 0;
        { Preferences p; p.begin("crash_log", true);
          crashCount = p.getUInt("count", 0);
          lastReason = p.getUInt("last_reason", 0);
          p.end(); }
        snprintf(buf, sizeof(buf),
            "{\"heap\":%u,\"min_heap\":%u,\"psram\":%u,"
            "\"uptime\":%lu,\"ws_clients\":%u,"
            "\"crash_count\":%u,\"last_crash_reason\":%u}",
            ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram(),
            millis() / 1000UL, (unsigned)wsClientCount(),
            crashCount, lastReason);
        req->send(200, "application/json", buf); });

    // ── /api/init ─────────────────────────────────────────────────────────
    _server.on("/api/init", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        HEAP_GUARD(req);
        cJsonPtr out = cJsonCreateObject();
        if (!out) { req->send(503, "application/json", "{\"error\":\"oom\"}"); return; }
        cJSON* outRaw = out.get();
        uMemory::read("macros", [outRaw](Preferences prefs) {
            String s = prefs.getString("items", "[]");
            cJsonPtr arr = cJsonParse(s.c_str());
            cJSON_AddItemToObject(outRaw, "macros", arr ? arr.release() : cJSON_CreateArray()); });
        uMemory::read("widgets", [outRaw](Preferences prefs) {
            String s = prefs.getString("items", "[]");
            cJsonPtr arr = cJsonParse(s.c_str());
            cJSON_AddItemToObject(outRaw, "widgets", arr ? arr.release() : cJSON_CreateArray()); });
        cJsonPtr settings = cJsonCreateObject();
        if (!settings) { req->send(503, "application/json", "{\"error\":\"oom\"}"); return; }
        cJSON* settingsRaw = settings.get();
        uMemory::read(MEMORY_SETTINGS_KEY, [settingsRaw](Preferences prefs) {
            cJSON_AddNumberToObject(settingsRaw, "screen_rotate", prefs.getInt("screen_rotate", 0));
            cJSON_AddNumberToObject(settingsRaw, "theme",        prefs.getInt("theme",        SETTINGS_DEFAULT_THEME));
            cJSON_AddNumberToObject(settingsRaw, "brightness",   prefs.getInt("brightness",   SETTINGS_DEFAULT_BRIGHTNESS));
            cJSON_AddNumberToObject(settingsRaw, "btn_size",     prefs.getInt("btn_size",     SETTINGS_DEFAULT_BTN_SIZE));
            cJSON_AddStringToObject(settingsRaw, "mdns_host",    prefs.getString("mdns_host", SERVER_NAME).c_str());
            cJSON_AddNumberToObject(settingsRaw, "startup_tab",  prefs.getInt("startup_tab",  SETTINGS_DEFAULT_STARTUP_TAB));
            cJSON_AddNumberToObject(settingsRaw, "tab_pos",      prefs.getInt("tab_pos",      SETTINGS_DEFAULT_TAB_POS));
            cJSON_AddNumberToObject(settingsRaw, "status_bar_pos",     prefs.getInt("sb_pos",       SETTINGS_DEFAULT_STATUS_BAR_POS));
            cJSON_AddBoolToObject  (settingsRaw, "status_bar_autohide",prefs.getBool("sb_autohide", SETTINGS_DEFAULT_STATUS_BAR_AUTOHIDE != 0));
            cJSON_AddNumberToObject(settingsRaw, "status_bar_idle_s",  prefs.getInt("sb_idle_s",    SETTINGS_DEFAULT_STATUS_BAR_IDLE_S));
            cJSON_AddNumberToObject(settingsRaw, "status_bar_faded_opa",prefs.getInt("sb_faded_opa",SETTINGS_DEFAULT_STATUS_BAR_FADED_OPA));
            cJSON_AddNumberToObject(settingsRaw, "sleep_dim_timeout", prefs.getInt("sleep_dim_t", SETTINGS_DEFAULT_SLEEP_DIM_TIMEOUT));
            cJSON_AddNumberToObject(settingsRaw, "sleep_dim_level",   prefs.getInt("sleep_dim_l", SETTINGS_DEFAULT_SLEEP_DIM_LEVEL));
            cJSON_AddNumberToObject(settingsRaw, "sleep_off_timeout", prefs.getInt("sleep_off_t", SETTINGS_DEFAULT_SLEEP_OFF_TIMEOUT));
            cJSON_AddNumberToObject(settingsRaw, "widget_masonry",    prefs.getInt("widget_masonry", SETTINGS_DEFAULT_WIDGET_MASONRY));
            cJSON_AddNumberToObject(settingsRaw, "widget_columns",    prefs.getInt("widget_columns", SETTINGS_DEFAULT_WIDGET_COLUMNS));
            cJSON_AddNumberToObject(settingsRaw, "macro_cols",        prefs.getInt("macro_cols",    SETTINGS_DEFAULT_MACRO_COLS));
            cJSON_AddNumberToObject(settingsRaw, "def_macro_bg",      prefs.getInt("def_m_bg",  SETTINGS_DEFAULT_MACRO_BG));
            cJSON_AddNumberToObject(settingsRaw, "def_macro_icon_clr", prefs.getInt("def_m_ic", SETTINGS_DEFAULT_MACRO_ICON_CLR));
            cJSON_AddNumberToObject(settingsRaw, "def_macro_icon_sz", prefs.getInt("def_m_isz", SETTINGS_DEFAULT_MACRO_ICON_SZ));
            cJSON_AddNumberToObject(settingsRaw, "def_macro_image_sz", prefs.getInt("def_m_imsz", SETTINGS_DEFAULT_MACRO_IMAGE_SZ));
            cJSON_AddNumberToObject(settingsRaw, "def_widget_bg",     prefs.getInt("def_w_bg",  SETTINGS_DEFAULT_WIDGET_BG));
            cJSON_AddNumberToObject(settingsRaw, "macro_radius",      prefs.getInt("macro_radius", SETTINGS_DEFAULT_MACRO_RADIUS));
            cJSON_AddNumberToObject(settingsRaw, "macro_bg_opa",      prefs.getInt("macro_bg_opa", SETTINGS_DEFAULT_MACRO_BG_OPA));
            cJSON_AddNumberToObject(settingsRaw, "widget_bg_opa",     prefs.getInt("widget_bg_opa", SETTINGS_DEFAULT_WIDGET_BG_OPA));
            cJSON_AddNumberToObject(settingsRaw, "macro_title_pos",   prefs.getInt("macro_title",  SETTINGS_DEFAULT_MACRO_TITLE_POS)); });
#ifdef OTA_ENABLED
        cJSON_AddBoolToObject(settingsRaw, "ota_enabled", true);
#else
        cJSON_AddBoolToObject(settingsRaw, "ota_enabled", false);
#endif
        cJSON_AddItemToObject(outRaw, "settings", settings.release());
        cJsonPtr pList(Cards::Profiles::getList(), cJSON_Delete);
        cJSON_AddItemToObject(outRaw, "profiles", pList ? pList.release() : cJSON_CreateArray());
        cJSON_AddStringToObject(outRaw, "active_profile", Cards::Profiles::getActiveId().c_str());
        cJSON_AddBoolToObject(outRaw, "sd_available", uSD::isAvailable());
        sendJsonResponse(req, std::move(out)); });

    // ── /api/log ──────────────────────────────────────────────────────────
    _server.on("/api/log", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        uint32_t since = 0;
        if (req->hasParam("since"))
            since = (uint32_t)req->getParam("since")->value().toInt();
        if (!_logBuf) { req->send(200, "application/json", "{\"entries\":[]}"); return; }
        struct Snap { uint32_t seq, ts; char p[192]; };
        Snap *snaps = (Snap*)heap_caps_malloc(LOG_CAP * sizeof(Snap), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!snaps) snaps = (Snap*)malloc(LOG_CAP * sizeof(Snap));
        if (!snaps) { req->send(503, "application/json", "{\"error\":\"busy\"}"); return; }
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
        req->send(200, "application/json", out); });

    // ── /api/macros GET ───────────────────────────────────────────────────
    _server.on("/api/macros", HTTP_GET, [](AsyncWebServerRequest *req)
               {
        HEAP_GUARD(req);
        // Use in-memory cache (populated from LittleFS at boot) — NVS is legacy
        // and no longer written on save, so reading it here returns stale data that
        // would overwrite LittleFS (with icons etc.) the next time the browser saves.
        std::string snap = Cards::Macros::getCachedJsonCopy();
        req->send(200, "application/json", snap.empty() ? "[]" : snap.c_str()); });

    // ── /api/widgets GET ──────────────────────────────────────────────────
    _server.on("/api/widgets", HTTP_GET, [](AsyncWebServerRequest *req)
               {
        HEAP_GUARD(req);
        // Use in-memory cache (populated from LittleFS at boot) — same NVS/LittleFS
        // split as macros: NVS is legacy and returns stale data after first LittleFS save.
        std::string snap = Cards::Widgets::getCachedJsonCopy();
        req->send(200, "application/json", snap.empty() ? "[]" : snap.c_str()); });

    // ── /api/wifi GET ── ?act=scan | status ──────────────────────────────
    _server.on("/api/wifi", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        String act = req->hasParam("act") ? req->getParam("act")->value() : "";
        if (act == "scan") {
            int n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING) { req->send(202, "application/json", "{\"scanning\":true}"); return; }
            if (n == WIFI_SCAN_FAILED || n < 0) {
                WiFi.scanNetworks(true);
                req->send(202, "application/json", "{\"scanning\":true}"); return;
            }
            cJsonPtr arr = cJsonCreateArray();
            if (!arr) { req->send(503, "application/json", "{\"error\":\"oom\"}"); return; }
            for (int i = 0; i < n; i++) {
                cJsonPtr net = cJsonCreateObject();
                if (!net) continue;
                cJSON_AddStringToObject(net.get(), "ssid", WiFi.SSID(i).c_str());
                cJSON_AddNumberToObject(net.get(), "rssi", WiFi.RSSI(i));
                cJSON_AddBoolToObject(net.get(), "open", WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
                cJSON_AddItemToArray(arr.get(), net.release());
            }
            WiFi.scanDelete();
            sendJsonResponse(req, std::move(arr));
            return;
        }
        if (act == "status") {
            cJsonPtr json = cJsonCreateObject();
            if (!json) { req->send(503, "application/json", "{\"error\":\"oom\"}"); return; }
            cJSON_AddNumberToObject(json.get(), "state", (int)Helper::getState());
            if (Helper::getState() == State::Connected) {
                cJSON_AddStringToObject(json.get(), "ip",   WiFi.localIP().toString().c_str());
                cJSON_AddStringToObject(json.get(), "ssid", WiFi.SSID().c_str());
            }
            cJSON_AddStringToObject(json.get(), "ap_ip", Helper::getAPIP().c_str());
            sendJsonResponse(req, std::move(json));
            return;
        }
        req->send(400, "application/json", "{\"error\":\"act required (scan|status)\"}"); });

    // ── /api/settings GET ── ?act=export | (default=get) ─────────────────
    _server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        HEAP_GUARD(req);
        String act = req->hasParam("act") ? req->getParam("act")->value() : "";
        if (act == "export") {
            cJsonPtr out = cJsonCreateObject();
            if (!out) { req->send(503, "application/json", "{\"error\":\"oom\"}"); return; }
            cJSON* outRaw = out.get();
            cJSON_AddNumberToObject(outRaw, "version", 1);
            uMemory::read("macros", [outRaw](Preferences prefs) {
                String s = prefs.getString("items", "[]");
                cJsonPtr arr = cJsonParse(s.c_str());
                cJSON_AddItemToObject(outRaw, "macros", arr ? arr.release() : cJSON_CreateArray()); });
            uMemory::read("widgets", [outRaw](Preferences prefs) {
                String s = prefs.getString("items", "[]");
                cJsonPtr arr = cJsonParse(s.c_str());
                cJSON_AddItemToObject(outRaw, "widgets", arr ? arr.release() : cJSON_CreateArray()); });
            cJsonPtr settings = cJsonCreateObject();
            if (!settings) { req->send(503, "application/json", "{\"error\":\"oom\"}"); return; }
            cJSON* settingsRaw = settings.get();
            uMemory::read(MEMORY_SETTINGS_KEY, [settingsRaw](Preferences prefs) {
                cJSON_AddNumberToObject(settingsRaw, "screen_rotate", prefs.getInt("screen_rotate", 0));
                cJSON_AddNumberToObject(settingsRaw, "theme",        prefs.getInt("theme",        SETTINGS_DEFAULT_THEME));
                cJSON_AddNumberToObject(settingsRaw, "brightness",   prefs.getInt("brightness",   SETTINGS_DEFAULT_BRIGHTNESS));
                cJSON_AddNumberToObject(settingsRaw, "btn_size",     prefs.getInt("btn_size",     SETTINGS_DEFAULT_BTN_SIZE));
                cJSON_AddStringToObject(settingsRaw, "mdns_host",    prefs.getString("mdns_host", SERVER_NAME).c_str());
                cJSON_AddNumberToObject(settingsRaw, "startup_tab",  prefs.getInt("startup_tab",  SETTINGS_DEFAULT_STARTUP_TAB));
                cJSON_AddNumberToObject(settingsRaw, "tab_pos",      prefs.getInt("tab_pos",      SETTINGS_DEFAULT_TAB_POS));
            cJSON_AddNumberToObject(settingsRaw, "status_bar_pos",     prefs.getInt("sb_pos",       SETTINGS_DEFAULT_STATUS_BAR_POS));
            cJSON_AddBoolToObject  (settingsRaw, "status_bar_autohide",prefs.getBool("sb_autohide", SETTINGS_DEFAULT_STATUS_BAR_AUTOHIDE != 0));
            cJSON_AddNumberToObject(settingsRaw, "status_bar_idle_s",  prefs.getInt("sb_idle_s",    SETTINGS_DEFAULT_STATUS_BAR_IDLE_S));
            cJSON_AddNumberToObject(settingsRaw, "status_bar_faded_opa",prefs.getInt("sb_faded_opa",SETTINGS_DEFAULT_STATUS_BAR_FADED_OPA));
                cJSON_AddNumberToObject(settingsRaw, "sleep_dim_timeout", prefs.getInt("sleep_dim_t", SETTINGS_DEFAULT_SLEEP_DIM_TIMEOUT));
                cJSON_AddNumberToObject(settingsRaw, "sleep_dim_level",   prefs.getInt("sleep_dim_l", SETTINGS_DEFAULT_SLEEP_DIM_LEVEL));
                cJSON_AddNumberToObject(settingsRaw, "sleep_off_timeout", prefs.getInt("sleep_off_t", SETTINGS_DEFAULT_SLEEP_OFF_TIMEOUT));
                cJSON_AddNumberToObject(settingsRaw, "widget_masonry",    prefs.getInt("widget_masonry", SETTINGS_DEFAULT_WIDGET_MASONRY));
                cJSON_AddNumberToObject(settingsRaw, "widget_columns",    prefs.getInt("widget_columns", SETTINGS_DEFAULT_WIDGET_COLUMNS));
                cJSON_AddNumberToObject(settingsRaw, "macro_cols",        prefs.getInt("macro_cols",    SETTINGS_DEFAULT_MACRO_COLS)); });
            cJSON_AddItemToObject(outRaw, "settings", settings.release());
            cJsonPtr pList(Cards::Profiles::getList(), cJSON_Delete);
            cJSON_AddItemToObject(outRaw, "profiles", pList ? pList.release() : cJSON_CreateArray());
            cJSON_AddStringToObject(outRaw, "active_profile", Cards::Profiles::getActiveId().c_str());
            sendJsonResponse(req, std::move(out));
            return;
        }
        // default: get settings
        cJsonPtr json = cJsonCreateObject();
        if (!json) { req->send(503, "application/json", "{\"error\":\"oom\"}"); return; }
        cJSON* jsonRaw = json.get();
        uMemory::read(MEMORY_SETTINGS_KEY, [jsonRaw](Preferences prefs) {
            cJSON_AddNumberToObject(jsonRaw, "screen_rotate", prefs.getInt("screen_rotate", 0));
            cJSON_AddNumberToObject(jsonRaw, "theme",        prefs.getInt("theme",        SETTINGS_DEFAULT_THEME));
            cJSON_AddNumberToObject(jsonRaw, "brightness",   prefs.getInt("brightness",   SETTINGS_DEFAULT_BRIGHTNESS));
            cJSON_AddNumberToObject(jsonRaw, "btn_size",     prefs.getInt("btn_size",     SETTINGS_DEFAULT_BTN_SIZE));
            cJSON_AddStringToObject(jsonRaw, "mdns_host",    prefs.getString("mdns_host", SERVER_NAME).c_str());
            cJSON_AddNumberToObject(jsonRaw, "startup_tab",  prefs.getInt("startup_tab",  SETTINGS_DEFAULT_STARTUP_TAB));
            cJSON_AddNumberToObject(jsonRaw, "tab_pos",     prefs.getInt("tab_pos",     SETTINGS_DEFAULT_TAB_POS));
            cJSON_AddNumberToObject(jsonRaw, "status_bar_pos",     prefs.getInt("sb_pos",       SETTINGS_DEFAULT_STATUS_BAR_POS));
            cJSON_AddBoolToObject  (jsonRaw, "status_bar_autohide",prefs.getBool("sb_autohide", SETTINGS_DEFAULT_STATUS_BAR_AUTOHIDE != 0));
            cJSON_AddNumberToObject(jsonRaw, "status_bar_idle_s",  prefs.getInt("sb_idle_s",    SETTINGS_DEFAULT_STATUS_BAR_IDLE_S));
            cJSON_AddNumberToObject(jsonRaw, "status_bar_faded_opa",prefs.getInt("sb_faded_opa",SETTINGS_DEFAULT_STATUS_BAR_FADED_OPA));
            cJSON_AddNumberToObject(jsonRaw, "sleep_dim_timeout", prefs.getInt("sleep_dim_t", SETTINGS_DEFAULT_SLEEP_DIM_TIMEOUT));
            cJSON_AddNumberToObject(jsonRaw, "sleep_dim_level",   prefs.getInt("sleep_dim_l", SETTINGS_DEFAULT_SLEEP_DIM_LEVEL));
            cJSON_AddNumberToObject(jsonRaw, "sleep_off_timeout", prefs.getInt("sleep_off_t", SETTINGS_DEFAULT_SLEEP_OFF_TIMEOUT));
            cJSON_AddNumberToObject(jsonRaw, "widget_masonry",    prefs.getInt("widget_masonry",  SETTINGS_DEFAULT_WIDGET_MASONRY));
            cJSON_AddNumberToObject(jsonRaw, "widget_columns",    prefs.getInt("widget_columns",  SETTINGS_DEFAULT_WIDGET_COLUMNS));
            cJSON_AddNumberToObject(jsonRaw, "macro_cols",        prefs.getInt("macro_cols",     SETTINGS_DEFAULT_MACRO_COLS));
            cJSON_AddNumberToObject(jsonRaw, "def_macro_bg",      prefs.getInt("def_m_bg",  SETTINGS_DEFAULT_MACRO_BG));
            cJSON_AddNumberToObject(jsonRaw, "def_macro_icon_clr", prefs.getInt("def_m_ic", SETTINGS_DEFAULT_MACRO_ICON_CLR));
            cJSON_AddNumberToObject(jsonRaw, "def_macro_icon_sz", prefs.getInt("def_m_isz", SETTINGS_DEFAULT_MACRO_ICON_SZ));
            cJSON_AddNumberToObject(jsonRaw, "def_macro_image_sz", prefs.getInt("def_m_imsz", SETTINGS_DEFAULT_MACRO_IMAGE_SZ));
            cJSON_AddNumberToObject(jsonRaw, "def_widget_bg",     prefs.getInt("def_w_bg",  SETTINGS_DEFAULT_WIDGET_BG));
            cJSON_AddNumberToObject(jsonRaw, "macro_radius",      prefs.getInt("macro_radius", SETTINGS_DEFAULT_MACRO_RADIUS));
            cJSON_AddNumberToObject(jsonRaw, "macro_bg_opa",      prefs.getInt("macro_bg_opa", SETTINGS_DEFAULT_MACRO_BG_OPA));
            cJSON_AddNumberToObject(jsonRaw, "widget_bg_opa",     prefs.getInt("widget_bg_opa", SETTINGS_DEFAULT_WIDGET_BG_OPA));
            cJSON_AddNumberToObject(jsonRaw, "macro_title_pos",   prefs.getInt("macro_title",  SETTINGS_DEFAULT_MACRO_TITLE_POS)); });
#ifdef OTA_ENABLED
        cJSON_AddBoolToObject(jsonRaw, "ota_enabled", true);
#else
        cJSON_AddBoolToObject(jsonRaw, "ota_enabled", false);
#endif
        sendJsonResponse(req, std::move(json)); });

    // ── Icons ─────────────────────────────────────────────────────────────
#ifdef BOARD_HAS_TF
    _server.on("/api/icons", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        HEAP_GUARD(req);
        req->send(200, "application/json", uIconManager::listJson()); });

    _server.on("/api/icons", HTTP_DELETE, [this](AsyncWebServerRequest *req)
               {
        HEAP_GUARD(req);
        if (!req->hasParam("name")) { req->send(400, "application/json", "{\"error\":\"name required\"}"); return; }
        String name = req->getParam("name")->value();
        String err;
        if (!uIconManager::deleteIcon(name, err)) { req->send(400, "application/json", "{\"error\":\"" + err + "\"}"); return; }
        req->send(200, "application/json", "{\"ok\":true}"); });

    // ── /api/icons POST ── ?act=src (source upload) | (default=upload) ──
    _server.on("/api/icons", HTTP_POST,
               [this](AsyncWebServerRequest *req) {
                   bool isSrc = req->hasParam("act") && req->getParam("act")->value() == "src";
                   if (isSrc) {
                       req->send(_iconSrcUploadOk ? 200 : 500, "application/json",
                                 _iconSrcUploadOk ? "{\"ok\":true}" : "{\"error\":\"upload failed\"}");
                       _iconSrcUploadOk = false;
                   } else {
                       req->send(_iconUploadOk ? 200 : 500, "application/json",
                                 _iconUploadOk ? "{\"ok\":true}" : "{\"error\":\"upload failed\"}");
                       _iconUploadOk = false;
                   }
               },
               [this](AsyncWebServerRequest *req, const String &filename,
                      size_t index, uint8_t *data, size_t len, bool final) {
                   bool isSrc = req->hasParam("act") && req->getParam("act")->value() == "src";
                   if (isSrc)
                       handleIconSrcUploadChunk(filename, index, data, len, final);
                   else
                       handleIconUploadChunk(filename, index, data, len, final);
               });

    _server.on("/icons/*", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        if (!uSD::isAvailable()) { req->send(503); return; }
        String url  = req->url();
        String name = url.substring(url.lastIndexOf('/') + 1);
        if (name.indexOf("..") >= 0) { req->send(400); return; }
        String path = "/icons/" + name;
        if (!uSD::getFS().exists(path)) { req->send(404); return; }
        String mime = "image/png"; String lower = name; lower.toLowerCase();
        if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) mime = "image/jpeg";
        else if (lower.endsWith(".bmp")) mime = "image/bmp";
        req->send(uSD::getFS(), path, mime); });

    // Serve source icon files for browser re-optimization
    _server.on("/icons/src/*", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        if (!uSD::isAvailable()) { req->send(503); return; }
        String url  = req->url();
        String name = url.substring(url.lastIndexOf('/') + 1);
        if (name.indexOf("..") >= 0) { req->send(400); return; }
        // Prefer compressed source variants so browser <img> decodes natively.
        int dot = name.lastIndexOf('.');
        String base = (dot > 0) ? name.substring(0, dot) : name;
        const char* imgExts[] = { ".png", ".jpg", ".jpeg", ".svg", ".webp", ".gif" };
        const char* imgMimes[] = { "image/png", "image/jpeg", "image/jpeg", "image/svg+xml", "image/webp", "image/gif" };
        for (int i = 0; i < 6; i++) {
            String alt = "/icons/src/" + base + imgExts[i];
            if (uSD::getFS().exists(alt)) {
                req->send(uSD::getFS(), alt, imgMimes[i]);
                return;
            }
        }
        // Fallback: exact filename (legacy .bin source)
        String path = "/icons/src/" + name;
        if (!uSD::getFS().exists(path)) { req->send(404); return; }
        req->send(uSD::getFS(), path, "application/octet-stream"); });
#else
    _server.on("/api/icons", HTTP_GET,    [](AsyncWebServerRequest *req) { req->send(200, "application/json", "[]"); });
    _server.on("/api/icons", HTTP_DELETE, [](AsyncWebServerRequest *req) { req->send(503, "application/json", "{\"error\":\"no SD card\"}"); });
    _server.on("/api/icons", HTTP_POST,   [](AsyncWebServerRequest *req) { req->send(503, "application/json", "{\"error\":\"no SD card\"}"); }, nullptr);
#endif

    // ── Backgrounds ───────────────────────────────────────────────────────
#ifdef BOARD_HAS_TF
    // ── /api/backgrounds GET ── ?act=scene-bg | (default=list) ──────────
    _server.on("/api/backgrounds", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        HEAP_GUARD(req);
        String act = req->hasParam("act") ? req->getParam("act")->value() : "";
        if (act == "scene-bg") { req->send(200, "application/json", uBackgroundManager::getSceneBgsJson()); return; }
        req->send(200, "application/json", uBackgroundManager::listJson()); });

    _server.on("/api/backgrounds", HTTP_DELETE, [this](AsyncWebServerRequest *req)
               {
        HEAP_GUARD(req);
        if (!req->hasParam("name")) { req->send(400, "application/json", "{\"error\":\"name required\"}"); return; }
        String name = req->getParam("name")->value();
        String err;
        if (!uBackgroundManager::deleteBackground(name, err)) { req->send(400, "application/json", "{\"error\":\"" + err + "\"}"); return; }
        req->send(200, "application/json", "{\"ok\":true}"); });

    _server.on("/api/backgrounds", HTTP_POST,
               [this](AsyncWebServerRequest *req) {
                   // default: file upload result
                   if (_bgUploadOk) {
                       req->send(200, "application/json", "{\"ok\":true}");
                   } else {
                       ESP_LOGE("BG", "onRequest: upload not OK (_bgUploadOk=false)");
                       req->send(500, "application/json", "{\"error\":\"upload failed\"}");
                   }
                   _bgUploadOk = false;
               },
               [this](AsyncWebServerRequest * /*req*/, const String &filename,
                      size_t index, uint8_t *data, size_t len, bool final) {
                   handleBgUploadChunk(filename, index, data, len, final);
               },
               nullptr);  // no body accumulation needed (upload only)

    _server.on("/backgrounds/*", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        if (!uSD::isAvailable()) { req->send(503); return; }
        String url  = req->url();
        String name = url.substring(url.lastIndexOf('/') + 1);
        if (name.indexOf("..") >= 0) { req->send(400); return; }
        String path = "/backgrounds/" + name;
        if (!uSD::getFS().exists(path)) { req->send(404); return; }
        String mime = "image/png"; String lower = name; lower.toLowerCase();
        if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) mime = "image/jpeg";
        else if (lower.endsWith(".bmp")) mime = "image/bmp";
        req->send(uSD::getFS(), path, mime); });

#else
    _server.on("/api/backgrounds", HTTP_GET,    [](AsyncWebServerRequest *req) {
        if (req->hasParam("act") && req->getParam("act")->value() == "scene-bg")
            req->send(200, "application/json", "{}");
        else
            req->send(200, "application/json", "[]"); });
    _server.on("/api/backgrounds", HTTP_DELETE, [](AsyncWebServerRequest *req) { req->send(503, "application/json", "{\"error\":\"no SD card\"}"); });
    _server.on("/api/backgrounds", HTTP_POST,   [](AsyncWebServerRequest *req) { req->send(503, "application/json", "{\"error\":\"no SD card\"}"); }, nullptr);
#endif

    // ── /api/profiles GET ─────────────────────────────────────────────────
    _server.on("/api/profiles", HTTP_GET, [this](AsyncWebServerRequest *req)
               {
        HEAP_GUARD(req);
        cJsonPtr out = cJsonCreateObject();
        if (!out) { req->send(503, "application/json", "{\"error\":\"oom\"}"); return; }
        cJsonPtr list(Cards::Profiles::getList(), cJSON_Delete);
        cJSON_AddItemToObject(out.get(), "list", list ? list.release() : cJSON_CreateArray());
        cJSON_AddStringToObject(out.get(), "active_id", Cards::Profiles::getActiveId().c_str());
        sendJsonResponse(req, std::move(out)); });

#ifdef OTA_ENABLED
    // ── /api/ota ──────────────────────────────────────────────────────────
    _server.on("/api/ota", HTTP_POST,
               [](AsyncWebServerRequest *req) {
                   bool ok = !Update.hasError();
                   if (ok) {
                       req->send(200, "application/json", "{\"ok\":true}");
                       delay(300);
                       esp_restart();
                   } else {
                       req->send(500, "application/json", "{\"error\":\"OTA update failed\"}");
                   }
               },
               nullptr,
               [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
                   if (index == 0) {
                       ESP_LOGI("WiFi.OTA", "OTA begin, total=%u", (unsigned)total);
                       if (!Update.begin(total, U_FLASH))
                           ESP_LOGE("WiFi.OTA", "Update.begin failed");
                   }
                   if (Update.isRunning()) {
                       if (Update.write(data, len) != len)
                           ESP_LOGE("WiFi.OTA", "Update.write error");
                   }
                   if (index + len == total) {
                       if (!Update.end(true))
                           ESP_LOGE("WiFi.OTA", "Update.end failed");
                       else
                           ESP_LOGI("WiFi.OTA", "OTA complete");
                   }
               });
#endif

    // ── /api/_diag ── network diagnostics snapshot ────────────────────────
    // No HEAP_GUARD: endpoint must work under heap pressure — that's its purpose.
    // No httpOk++: self-queries would inflate the counter and skew enter/ok ratio.
    _server.on("/api/_diag", HTTP_GET, [](AsyncWebServerRequest* req) {
        String body = NetDiag::toJson();
        req->send(200, "application/json", body);
    });

    // ── 404 / SPA fallback ────────────────────────────────────────────────
    _server.onNotFound([this](AsyncWebServerRequest *req) {
        if (req->method() == HTTP_OPTIONS) { req->send(200); return; }
        if (req->method() == HTTP_GET) {
            String url = req->url();
            int q = url.indexOf('?');
            if (q >= 0) url = url.substring(0, q);
            if (url == "/" || url.isEmpty()) url = "/index.html";
            String base     = "/www" + url;
            String gz       = base + ".gz";
            bool gzExists   = LittleFS.exists(gz);
            bool baseExists = !gzExists && LittleFS.exists(base);
            ESP_LOGI("HTTP.FS", "url=%s  gz=%d  base=%d  heap=%u",
                     url.c_str(), (int)gzExists, (int)baseExists, ESP.getFreeHeap());
            if (gzExists)   { sendStatic(req, gz);   return; }
            if (baseExists) { sendStatic(req, base); return; }
            if (url.indexOf('.') < 0) {
                if (LittleFS.exists("/www/index.html.gz")) { sendStatic(req, "/www/index.html.gz"); return; }
                if (LittleFS.exists("/www/index.html"))    { sendStatic(req, "/www/index.html");    return; }
            }
        }
        if (!LittleFS.exists("/www/index.html.gz") && !LittleFS.exists("/www/index.html")) {
            req->send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<title>CYD Dashboard</title>"
                "<style>body{font-family:sans-serif;max-width:600px;margin:60px auto;padding:0 20px}"
                "code{background:#f3f4f6;padding:2px 6px;border-radius:4px}"
                "pre{background:#1e293b;color:#7ee787;padding:16px;border-radius:6px}</style></head><body>"
                "<h2>CYD Dashboard — Web UI not uploaded yet</h2>"
                "<p>The filesystem image has not been flashed. Run these commands once:</p>"
                "<pre>pio run -t uploadfs -e JC4827W543C</pre>"
                "<p>After upload, open this page again. "
                "The REST API at <code>/api/*</code> is already working.</p>"
                "</body></html>");
            return;
        }
        req->send(404, "application/json", "{\"error\":\"not found\"}");
    });

    Serial.printf("[HTTP.ASYNC] calling _server.begin()  heap=%u\n", ESP.getFreeHeap());
    _server.begin();
    Serial.printf("[HTTP.ASYNC] Server RUNNING  heap=%u\n", ESP.getFreeHeap());
    Serial.printf("[HTTP.ASYNC]   AP  http://%s/\n",
                  WiFiModule::WiFiClient.softAPIP().toString().c_str());
    Serial.printf("[HTTP.ASYNC]   STA http://%s/\n",
                  WiFiModule::WiFiClient.localIP().toString().c_str());
}

} // namespace WiFiModule
