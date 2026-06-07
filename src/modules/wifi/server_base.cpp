#include "server_base.h"
#include "api.h"
#include "helpers.h"
#include "net_diag.h"
#include "net_selfheal.h"
#include "ui.h"

#include <Arduino.h>
#include <cJSON.h>
#include <esp_heap_caps.h>

#include "../../constants.h"
#include "../../ui/ui.h"
#include "../../utils/memory.h"
#include "../../utils/ui.h"
#include "../../utils/settings.h"
#include "../../utils/heap_probe.h"
#include "../../utils/sd.h"
#include "../../utils/icons.h"

#include "../cards/widgets.h"
#include "../cards/macros.h"
#include "../cards/profiles.h"
#include "../hid/keyboard.h"
#include "../../modules/timers.h"


namespace WiFiModule
{

// ── Static member definitions ─────────────────────────────────────────────────
uint8_t WebServerBase::_sConnIconMode = 0;

// ── LVGL connection icon (must be a plain C-style function pointer) ───────────
// Mode encoding (packed into _sConnIconMode):
//   bits 0..1 = transport: 0=disconnected (red), 1=proxy (orange), 2=companion (blue)
//   bit  7    = service-mode active (swaps icon glyph to SETTINGS gear)
// Two axes so both states are visible at once: color still tells the user which
// remote is attached (proxy/companion/none) while the glyph flags service mode.
void WebServerBase::_connIconUpdateCb(void * /*user_data*/)
{
    static constexpr uint32_t colors[] = {
        0xFF0000,  // 0 — disconnected
        0xFF8C00,  // 1 — browser proxy
        0x2196F3,  // 2 — companion app
    };
    uint8_t m         = _sConnIconMode;
    uint8_t transport = m & 0x03;
    bool    service   = (m & 0x80) != 0;
    if (transport > 2) transport = 0;

    lv_obj_set_style_text_color(
        ui_lblCompanion, lv_color_hex(colors[transport]),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_lblCompanion,
        service ? LV_SYMBOL_SETTINGS : LV_SYMBOL_CALL);
}

void WebServerBase::updateConnectionIcon()
{
    uint8_t transport = 0;
    if (_companionClientId > 0)      transport = 2;
    else if (_proxyClientId > 0)     transport = 1;
    _sConnIconMode = transport | (_serviceMode ? 0x80 : 0);
    lv_async_call(_connIconUpdateCb, nullptr);
}

// ── MIME type helper ──────────────────────────────────────────────────────────
String WebServerBase::mimeFor(const String &path)
{
    String p = path.endsWith(".gz") ? path.substring(0, path.length() - 3) : path;
    if (p.endsWith(".html"))        return "text/html";
    if (p.endsWith(".js"))          return "application/javascript";
    if (p.endsWith(".css"))         return "text/css";
    if (p.endsWith(".json"))        return "application/json";
    if (p.endsWith(".webmanifest")) return "application/manifest+json";
    if (p.endsWith(".svg"))         return "image/svg+xml";
    if (p.endsWith(".png"))         return "image/png";
    if (p.endsWith(".ico"))         return "image/x-icon";
    if (p.endsWith(".txt"))         return "text/plain";
    return "application/octet-stream";
}

// ── Settings apply result → pending flags ────────────────────────────────────
void WebServerBase::applySettingsResult(const uSettings::SettingsApplyResult& r)
{
    if (r.screen_rotate)  { _pending_screen_rotate_ui_v = r.screen_rotate_v; _pending_screen_rotate_ui = true; }
    if (r.theme)          { _pending_theme_v = r.theme_v; _pending_theme = true; }
    if (r.brightness)     { _pending_brightness_ui_v = r.brightness_v; _pending_brightness_ui = true; }
    if (r.btn_size)       { _pending_btn_size_v = r.btn_size_v; _pending_btn_size = true; }
    if (r.mdns_restart)     _pending_mdns_restart = true;
    if (r.tab_pos)        { _pending_tab_pos_v = r.tab_pos_v; _pending_tab_pos = true; }
    if (r.status_bar_pos) { _pending_status_bar_pos_v = r.status_bar_pos_v; _pending_status_bar_pos = true; }
    // autohide flag/timeout take effect on next loop tick — no pending needed,
    // uSettings has already cached the values via setStatusBar*.
    if (r.macro_rerender)   _pending_macro_cols = true;
    if (r.masonry)          _pending_masonry = true;
    if (r.bt_en)            { HidKeyboard::applyBluetoothEnabled(r.bt_en_v);
                              _pending_bt_en_ui_v = r.bt_en_v; _pending_bt_en_ui = true; }
    if (r.keyboard_tab)     { _pending_keyboard_tab_v = r.keyboard_tab_v; _pending_keyboard_tab = true; }
}

void WebServerBase::triggerSceneBgReload()
{
    _pending_scene_bg_reload = true;
    _pending_macro_cols      = true;
    _pending_masonry         = true;
}

void WebServerBase::stageWifiConnect(const char* ssid, const char* pass)
{
    if (!ssid || !pass) return;
    strncpy(_pending_wifi_ssid, ssid, sizeof(_pending_wifi_ssid) - 1);
    _pending_wifi_ssid[sizeof(_pending_wifi_ssid) - 1] = '\0';
    strncpy(_pending_wifi_pass, pass, sizeof(_pending_wifi_pass) - 1);
    _pending_wifi_pass[sizeof(_pending_wifi_pass) - 1] = '\0';
    _pending_wifi_connect = true;
}

// ── Settings JSON builder (shared by /api/init, /api/settings, /api/export) ──
cJsonPtr WebServerBase::buildSettingsJson()
{
    cJsonPtr s = cJsonCreateObject();
    uMemory::read(MEMORY_SETTINGS_KEY, [s = s.get()](Preferences prefs) {
        cJSON_AddNumberToObject(s, "screen_rotate",      prefs.getInt("screen_rotate", 0));
        cJSON_AddNumberToObject(s, "theme",              prefs.getInt("theme",         SETTINGS_DEFAULT_THEME));
        cJSON_AddNumberToObject(s, "brightness",         prefs.getInt("brightness",    SETTINGS_DEFAULT_BRIGHTNESS));
        cJSON_AddNumberToObject(s, "btn_size",           prefs.getInt("btn_size",      SETTINGS_DEFAULT_BTN_SIZE));
        cJSON_AddStringToObject(s, "mdns_host",          prefs.getString("mdns_host",  SERVER_NAME).c_str());
        cJSON_AddNumberToObject(s, "startup_tab",        prefs.getInt("startup_tab",   SETTINGS_DEFAULT_STARTUP_TAB));
        cJSON_AddNumberToObject(s, "tab_pos",            prefs.getInt("tab_pos",       SETTINGS_DEFAULT_TAB_POS));
        cJSON_AddNumberToObject(s, "status_bar_pos",     prefs.getInt("sb_pos",        SETTINGS_DEFAULT_STATUS_BAR_POS));
        cJSON_AddBoolToObject  (s, "status_bar_autohide",prefs.getBool("sb_autohide",  SETTINGS_DEFAULT_STATUS_BAR_AUTOHIDE != 0));
        cJSON_AddNumberToObject(s, "status_bar_idle_s",  prefs.getInt("sb_idle_s",     SETTINGS_DEFAULT_STATUS_BAR_IDLE_S));
        cJSON_AddNumberToObject(s, "status_bar_faded_opa",prefs.getInt("sb_faded_opa", SETTINGS_DEFAULT_STATUS_BAR_FADED_OPA));
        cJSON_AddNumberToObject(s, "sleep_dim_timeout",  prefs.getInt("sleep_dim_t",   SETTINGS_DEFAULT_SLEEP_DIM_TIMEOUT));
        cJSON_AddNumberToObject(s, "sleep_dim_level",    prefs.getInt("sleep_dim_l",   SETTINGS_DEFAULT_SLEEP_DIM_LEVEL));
        cJSON_AddNumberToObject(s, "sleep_off_timeout",  prefs.getInt("sleep_off_t",   SETTINGS_DEFAULT_SLEEP_OFF_TIMEOUT));
        cJSON_AddNumberToObject(s, "widget_masonry",     prefs.getInt("widget_masonry", SETTINGS_DEFAULT_WIDGET_MASONRY));
        cJSON_AddNumberToObject(s, "widget_columns",     prefs.getInt("widget_columns", SETTINGS_DEFAULT_WIDGET_COLUMNS));
        cJSON_AddNumberToObject(s, "macro_cols",         prefs.getInt("macro_cols",     SETTINGS_DEFAULT_MACRO_COLS));
        cJSON_AddNumberToObject(s, "def_macro_bg",       prefs.getInt("def_m_bg",      SETTINGS_DEFAULT_MACRO_BG));
        cJSON_AddNumberToObject(s, "def_macro_icon_clr", prefs.getInt("def_m_ic",      SETTINGS_DEFAULT_MACRO_ICON_CLR));
        cJSON_AddNumberToObject(s, "def_macro_icon_sz",  prefs.getInt("def_m_isz",     SETTINGS_DEFAULT_MACRO_ICON_SZ));
        cJSON_AddNumberToObject(s, "def_macro_image_sz", prefs.getInt("def_m_imsz",    SETTINGS_DEFAULT_MACRO_IMAGE_SZ));
        cJSON_AddNumberToObject(s, "def_widget_bg",      prefs.getInt("def_w_bg",      SETTINGS_DEFAULT_WIDGET_BG));
        cJSON_AddNumberToObject(s, "macro_radius",       prefs.getInt("macro_radius",  SETTINGS_DEFAULT_MACRO_RADIUS));
        cJSON_AddNumberToObject(s, "macro_bg_opa",       prefs.getInt("macro_bg_opa",  SETTINGS_DEFAULT_MACRO_BG_OPA));
        cJSON_AddNumberToObject(s, "widget_bg_opa",      prefs.getInt("widget_bg_opa", SETTINGS_DEFAULT_WIDGET_BG_OPA));
        cJSON_AddNumberToObject(s, "macro_title_pos",    prefs.getInt("macro_title",   SETTINGS_DEFAULT_MACRO_TITLE_POS));
        cJSON_AddBoolToObject  (s, "macro_shadow",       prefs.getBool("macro_shadow", SETTINGS_DEFAULT_MACRO_SHADOW != 0));
        cJSON_AddNumberToObject(s, "macro_brd_clr",   prefs.getInt("macro_brd_clr", SETTINGS_DEFAULT_MACRO_BORDER_CLR));
        cJSON_AddBoolToObject  (s, "bt_en",            prefs.getBool("bt_en", true));
        cJSON_AddBoolToObject  (s, "keyboard_tab",     prefs.getBool("keyboard_tab", false));
    });
#ifdef OTA_ENABLED
    cJSON_AddBoolToObject(s.get(), "ota_enabled", true);
#else
    cJSON_AddBoolToObject(s.get(), "ota_enabled", false);
#endif
    return s;
}

// ── Shared upload helpers ─────────────────────────────────────────────────────
bool WebServerBase::handleIconUploadChunk(const String &filename, size_t index,
                                          uint8_t *data, size_t len, bool final)
{
    if (index == 0) {
        _iconUploadOk = false;
        if (!uSD::isAvailable()) return false;
        if (filename.indexOf('/') >= 0 || filename.indexOf("..") >= 0) return false;
        String lower = filename; lower.toLowerCase();
        if (!lower.endsWith(".bin")) {
            ESP_LOGE("ICON", "rejected non-.bin upload: %s", filename.c_str());
            return false;
        }
        if (!uSD::getFS().exists("/icons")) uSD::getFS().mkdir("/icons");
        _iconUploadFile = uSD::getFS().open("/icons/" + filename, FILE_WRITE);
        if (!_iconUploadFile) return false;
        // Pause widget HTTP workers — multi-chunk SD write competes with
        // widget fetches for DRAM + SPI bus. Without this, a large upload
        // can starve lwIP heap and trigger a WiFi disconnect mid-write.
        uSD::setSdServing(true);
    }
    if (!_iconUploadFile) { uSD::setSdServing(false); return false; }
    // Cap at 200 KB. ARGB8888 (4 B/px) icon at btn_size=200 is 200·200·4 = 160 KB,
    // +header + safety = 200 KB. Small icons (45×45 to 96×96 typical) well under.
    if (index + len > 200 * 1024) {
        _iconUploadFile.close();
        uSD::setSdServing(false);
        ESP_LOGE("ICON", "upload rejected: size %u exceeds 200 KB cap", (unsigned)(index + len));
        return false;
    }
    _iconUploadFile.write(data, len);
    if (final) {
        _iconUploadFile.flush();
        _iconUploadFile.close();
        _iconUploadOk = true;
        uSD::setSdServing(false);
        uIconManager::cacheAdd(filename);
    }
    return true;
}

bool WebServerBase::handleIconSrcUploadChunk(const String &filename, size_t index,
                                             uint8_t *data, size_t len, bool final)
{
    if (index == 0) {
        _iconSrcUploadOk = false;
        if (!uSD::isAvailable()) return false;
        if (filename.indexOf('/') >= 0 || filename.indexOf("..") >= 0) return false;
        // Source icons are stored in their original format (PNG/JPG/SVG/WEBP
        // or LVGL .bin). Browser decodes + re-encodes on demand when the
        // user changes btn_size, so keeping the compressed original saves
        // ~8× on SD space (33 KB PNG vs 262 KB ARGB8888 .bin for 256×256).
        String lower = filename; lower.toLowerCase();
        bool okExt = lower.endsWith(".png")  || lower.endsWith(".jpg") ||
                     lower.endsWith(".jpeg") || lower.endsWith(".svg") ||
                     lower.endsWith(".webp") || lower.endsWith(".gif") ||
                     lower.endsWith(".bin");   // legacy: accept old .bin sources too
        if (!okExt) {
            ESP_LOGE("ICON", "src rejected: unsupported extension %s", filename.c_str());
            return false;
        }
        if (!uSD::getFS().exists("/icons/src")) {
            uSD::getFS().mkdir("/icons");
            uSD::getFS().mkdir("/icons/src");
        }
        _iconSrcUploadFile = uSD::getFS().open("/icons/src/" + filename, FILE_WRITE);
        if (!_iconSrcUploadFile) return false;
        uSD::setSdServing(true);  // pause widgets for the duration of the write
    }
    if (!_iconSrcUploadFile) { uSD::setSdServing(false); return false; }
    // Cap at 320 KB. SPA's iconToLvglBin emits ARGB8888 (4 B/px), so a
    // 256×256 source is 256·256·4 + 12 B header = 262 156 B — just 12 B over
    // a flat 256 KB limit. 320 KB also covers 280×280 sources for safety.
    if (index + len > 320 * 1024) {
        _iconSrcUploadFile.close();
        uSD::setSdServing(false);
        ESP_LOGE("ICON", "src upload rejected: size %u exceeds 320 KB cap", (unsigned)(index + len));
        return false;
    }
    _iconSrcUploadFile.write(data, len);
    if (final) {
        _iconSrcUploadFile.flush();
        _iconSrcUploadFile.close();
        _iconSrcUploadOk = true;
        uSD::setSdServing(false);
    }
    return true;
}

bool WebServerBase::handleBgUploadChunk(const String &filename, size_t index,
                                        uint8_t *data, size_t len, bool final)
{
    if (index == 0) {
        _bgUploadOk = false;
        if (_bgUploadFile) _bgUploadFile.close();
        if (!uSD::isAvailable()) { ESP_LOGE("BG", "SD not available"); return false; }
        if (filename.isEmpty() || filename.indexOf('/') >= 0 || filename.indexOf("..") >= 0) {
            ESP_LOGE("BG", "invalid filename: '%s'", filename.c_str()); return false;
        }
        String lower = filename; lower.toLowerCase();
        if (!lower.endsWith(".jpg") && !lower.endsWith(".jpeg")) {
            ESP_LOGE("BG", "rejected non-JPEG upload: %s", filename.c_str()); return false;
        }
        if (!uSD::getFS().exists("/backgrounds")) {
            if (!uSD::getFS().mkdir("/backgrounds")) {
                ESP_LOGE("BG", "mkdir /backgrounds failed"); return false;
            }
        }
        _bgUploadFile = uSD::getFS().open("/backgrounds/" + filename, FILE_WRITE);
        if (!_bgUploadFile) { ESP_LOGE("BG", "open failed: %s", filename.c_str()); return false; }
        ESP_LOGI("BG", "upload start: %s", filename.c_str());
        uSD::setSdServing(true);  // pause widget HTTP fetches during write
    }
    if (!_bgUploadFile) { uSD::setSdServing(false); return false; }
    if (index + len > 512000) {
        ESP_LOGE("BG", "file too large at %u+%u", (unsigned)index, (unsigned)len);
        _bgUploadFile.close();
        uSD::setSdServing(false);
        return false;
    }
    _bgUploadFile.write(data, len);
    if (final) {
        _bgUploadFile.flush();
        _bgUploadFile.close(); _bgUploadOk = true;
        uSD::setSdServing(false);
        ESP_LOGI("BG", "upload done: %u bytes", (unsigned)(index + len));
    }
    return true;
}

// ── Shared WS message processor ──────────────────────────────────────────────
void WebServerBase::processWsMessage(uint32_t clientId, const char *msg)
{
    // Update activity timestamp for service mode owner (keeps activity timeout alive)
    if (_serviceMode && clientId == _serviceModeClientId)
        _serviceModeLastMsg = millis();

    API::handleMessage(clientId, msg);
}

// ── Log ring buffer ───────────────────────────────────────────────────────────
void WebServerBase::pushLogEvent(cJsonPtr&& json)
{
    if (!json) return;
    cJsonStrPtr s = cJsonPrint(json.get());
    if (!s || !_logBuf) return;

    portENTER_CRITICAL(&_logMux);
    int idx = (_logHead + _logCnt) % LOG_CAP;
    if (_logCnt == LOG_CAP)
        _logHead = (_logHead + 1) % LOG_CAP;
    else
        _logCnt++;
    _logBuf[idx].seq = ++_logSeq;
    _logBuf[idx].ts  = millis();
    strncpy(_logBuf[idx].payload, s.get(), sizeof(_logBuf[idx].payload) - 1);
    _logBuf[idx].payload[sizeof(_logBuf[idx].payload) - 1] = '\0';
    portEXIT_CRITICAL(&_logMux);
}

void WebServerBase::pushLogEvent(const char* jsonStr)
{
    if (!jsonStr || !_logBuf) return;
    portENTER_CRITICAL(&_logMux);
    int idx = (_logHead + _logCnt) % LOG_CAP;
    if (_logCnt == LOG_CAP)
        _logHead = (_logHead + 1) % LOG_CAP;
    else
        _logCnt++;
    _logBuf[idx].seq = ++_logSeq;
    _logBuf[idx].ts  = millis();
    strncpy(_logBuf[idx].payload, jsonStr, sizeof(_logBuf[idx].payload) - 1);
    _logBuf[idx].payload[sizeof(_logBuf[idx].payload) - 1] = '\0';
    portEXIT_CRITICAL(&_logMux);
}

// ── WebSocket messaging ───────────────────────────────────────────────────────
bool WebServerBase::sendBinaryToClient(uint32_t clientId, const uint8_t* buf, size_t len)
{
    return wsBinary(clientId, buf, len);
}

bool WebServerBase::sendCommandToClient(uint32_t clientId, const char *jsonStr)
{
    if (!jsonStr) return false;
    if (clientId > 0)
    {
        if (wsClientQueueFull(clientId))
        {
            ESP_LOGW("WiFi.WSS", "WS #%u queue full — dropping message", clientId);
            return false;
        }
        wsText(clientId, jsonStr);
    }
    else
    {
        wsTextAll(jsonStr);
    }
    return true;
}

bool WebServerBase::sendCommandToClient(uint32_t clientId, cJsonPtr&& json)
{
    if (!json) return false;
    cJsonStrPtr s = cJsonPrint(json.get());
    if (!s)
    {
        ESP_LOGE("WiFi.WSS", "cJSON_PrintUnformatted OOM — dropping message");
        return false;
    }
    return sendCommandToClient(clientId, s.get());
}

bool WebServerBase::sendCommand(const char *jsonStr)
{
    return sendCommandToClient(0, jsonStr);
}

bool WebServerBase::sendCommand(cJsonPtr&& json)
{
    if (!json) return false;
    cJsonStrPtr s = cJsonPrint(json.get());
    if (!s) return false;
    return sendCommand(s.get());
}

bool WebServerBase::sendCommandToProxy(cJsonPtr&& json)
{
    if (!json) return false;
    cJsonStrPtr s = cJsonPrint(json.get());
    if (!s) return false;
    uint32_t id = _proxyClientId.load();
    return sendCommandToClient(id, s.get());
}

void WebServerBase::broadcastSettingInt(const char *key, int value)
{
    cJsonPtr upd = cJsonCreateObject();
    cJSON_AddStringToObject(upd.get(), "action", "settings_update");
    cJSON_AddNumberToObject(upd.get(), key, value);
    sendCommand(std::move(upd));
}

// ── State queries ─────────────────────────────────────────────────────────────
bool WebServerBase::isWebServing()
{
    uint32_t t = _webServeTimestamp;
    return t != 0 && (millis() - t) < 10000;
}

uint32_t WebServerBase::getProxyClientId()    { return _proxyClientId; }
void     WebServerBase::clearProxyClientId()  { _proxyClientId = 0; }
uint32_t WebServerBase::getCompanionClientId() { return _companionClientId; }

void WebServerBase::setCompanionClientId(uint32_t id)
{
    _companionClientId = id;
    updateConnectionIcon();
}

void WebServerBase::triggerWidgetsRefresh()
{
    _pending_widgets_refresh = true;
}

void WebServerBase::requestProfileSwitch(const char *profileId)
{
    if (!profileId) profileId = "";
    strncpy(_pending_profile_id, profileId, sizeof(_pending_profile_id) - 1);
    _pending_profile_id[sizeof(_pending_profile_id) - 1] = '\0';
    _pending_profile_apply = true;
}

// ── Service mode ──────────────────────────────────────────────────────────────
bool WebServerBase::isServiceMode()              { return _serviceMode; }
uint32_t WebServerBase::getServiceModeClientId() { return _serviceModeClientId; }
const char* WebServerBase::getServiceModeToken() { return _serviceModeToken; }

void WebServerBase::markWidgetsDirty()           { _widgets_dirty_in_svc = true; }

bool WebServerBase::enterServiceMode(uint32_t clientId, const char* token)
{
    HeapProbe::snapshot("svc.enter.begin");
    // ── Re-claim path ──────────────────────────────────────────────────────
    // Caller provides an existing token that matches the current session.
    // Happens when the SPA WS drops and the browser reconnects inside the
    // grace window. We keep service mode active, just update the owner fd.
    if (_serviceMode && token && *token && _serviceModeToken[0] != 0 &&
        strncmp(_serviceModeToken, token, 16) == 0)
    {
        _serviceModeClientId   = clientId;
        _serviceModeLastMsg    = millis();
        _serviceModeGraceStart = 0;
        ESP_LOGI("SVC", "Service mode RE-CLAIMED by client %u (token match)", clientId);
        updateConnectionIcon();
        return true;
    }

    // ── Fresh claim — refuse if another owner holds it ────────────────────
    if (_serviceMode && _serviceModeClientId != 0 && _serviceModeClientId != clientId)
        return false;

    // New session: generate a 16-hex token so the client can survive reconnects.
    if (!_serviceMode) {
        static const char hex[] = "0123456789abcdef";
        for (int i = 0; i < 16; i++)
            _serviceModeToken[i] = hex[esp_random() & 0xF];
        _serviceModeToken[16] = 0;
    }

    _serviceMode           = true;
    _serviceModeClientId   = clientId;
    _serviceModeLastMsg    = millis();
    _serviceModeGraceStart = 0;
    _widgets_dirty_in_svc  = false;  // fresh session starts clean

    // Schedule widget timer stop and BLE deinit on the main task.
    // Direct calls here (from WS task) would race with mTimers::loop() and
    // block the WS response until BLE teardown (~200ms+) completes.
    //
    // BLE deinit on ALL boards (was PSRAM-only): on no-PSRAM NimBLE's ~24 KB
    // resident controller + the service-mode burst (2-3 TCP sockets + WS frames
    // + LVGL rebuild) collapses largest_free_block to ~1.5 KB → panic. Web is
    // now delivered as a ~1 KB CDN shell (not 200 KB of WS binary frames as when
    // the old skip-deinit reasoning was written), so service sessions are light
    // and freeing BLE's 24 KB for the duration is both safe and necessary. BLE
    // is reinited on service-mode exit; the keyboard is only used in normal mode.
    _pending_stop_widgets    = true;
    _pending_ble_deinit      = true;
    _bleWasDeinitedForSvc    = true;
    _pending_svc_ready       = true;

    {
        cJsonPtr msg = cJsonCreateObject();
        cJSON_AddStringToObject(msg.get(), "action", "service_mode_changed");
        cJSON_AddBoolToObject(msg.get(), "active", true);
        cJSON_AddStringToObject(msg.get(), "token",  _serviceModeToken);
        sendCommand(std::move(msg));
    }

    updateConnectionIcon();
    ESP_LOGI("SVC", "Service mode ENTERED by client %u  token=%.8s…", clientId, _serviceModeToken);
    HeapProbe::snapshot("svc.enter.end");
    return true;
}

void WebServerBase::exitServiceMode()
{
    if (!_serviceMode) return;  // guard: already in normal mode
    HeapProbe::snapshot("svc.exit.begin");

    // Snapshot the SPA (service-mode owner) BEFORE clearing state.
    // This is the WS client that loaded the full Vue app — we must close it
    // so the browser reconnects cleanly to the lightweight proxy page.
    // NOTE: do NOT use _companionClientId here — that refers to the desktop
    // companion app (which sends `companion_hello`), not the browser SPA.
    uint32_t prevSpaClient = _serviceModeClientId;

    _serviceMode           = false;
    _serviceModeClientId   = 0;
    _serviceModeLastMsg    = 0;
    _serviceModeGraceStart = 0;
    _serviceModeToken[0]   = 0;  // invalidate token — next session will generate a new one

    // Cancel any still-pending enter-side deferred operations so they don't
    // fire AFTER we've already restored the normal state.
    _pending_ble_deinit    = false;  // don't deinit BLE we're about to reinit
    _pending_stop_widgets  = false;  // don't clear widget timers we're about to create
    _pending_svc_ready     = false;  // NEW — cancel pending ready broadcast (no one to receive it)

    // Notify all clients that service mode ended, then close the SPA WS.
    // Closing after broadcast ensures the SPA receives service_mode_changed
    // before its connection drops, so it can navigate cleanly.
    {
        cJsonPtr msg = cJsonCreateObject();
        cJSON_AddStringToObject(msg.get(), "action", "service_mode_changed");
        cJSON_AddBoolToObject(msg.get(), "active", false);
        sendCommand(std::move(msg));
    }

    // Close the SPA's WS immediately so the browser reconnects as the
    // lightweight proxy-page client (ws=1).  The desktop companion (if any)
    // stays connected — it's tracked separately as _companionClientId.
    if (prevSpaClient != 0)
        wsCloseClient(prevSpaClient);

    // Only reinit BLE if we deinited it on service mode entry.
    // No-PSRAM boards skip deinit to prevent heap fragmentation, so no reinit needed.
    if (_bleWasDeinitedForSvc) {
        HidKeyboard::init();
        _bleWasDeinitedForSvc = false;
    }
    // Conditional widget rebuild: only do the expensive lv_obj_clean + recreate
    // path if the user actually edited macros/widgets during this service-mode
    // session. Otherwise the on-screen LVGL tree is still valid (entry only
    // stopped timers, didn't destroy objects) so we can fast-path through
    // armTimersOnly() in updateList(). Saves ~3 KB of largest_free_block per
    // cycle on no-PSRAM, which is the dominant cross-cycle fragmentation
    // source.
    if (_widgets_dirty_in_svc) {
        _pending_widgets_refresh = true;
        _widgets_dirty_in_svc    = false;
        ESP_LOGI("SVC", "Widgets edited during svc — full rebuild scheduled");
    } else {
        // Fast path: only restart widget timers, leave LVGL objects intact.
        _pending_widgets_timer_resume = true;
        ESP_LOGI("SVC", "No widget edits — fast-path timer resume");
    }

    uIconManager::buildCache();  // TODO orphan-report

    updateConnectionIcon();
    ESP_LOGI("SVC", "Service mode EXITED");
    HeapProbe::snapshot("svc.exit.end");
}

// ── loop() — shared pending-flag processing ───────────────────────────────────
void WebServerBase::loop()
{
    uint32_t now = millis();

    if ((now - _wifiReadTimestamp) >= WIFI_READ_TIMEOUT)
    {
        _wifiReadTimestamp = now;
        wsCleanup();
    }

    static uint32_t _heap_log_ts = 0;
    if (now - _heap_log_ts >= 30000)
    {
        _heap_log_ts = now;
        multi_heap_info_t _hi;
        heap_caps_get_info(&_hi, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_LOGI("HEALTH", "heap=%u  min=%u  largest=%u  psram=%u  ws=%u  companion=%u  proxy=%u",
                 ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                 (unsigned)_hi.largest_free_block,
                 ESP.getFreePsram(),
                 (unsigned)wsClientCount(), _companionClientId.load(), _proxyClientId.load());
    }

    static uint32_t _netdiag_ts = 0;  // first tick at boot+30s; ping guarded by WL_CONNECTED
    if (now - _netdiag_ts >= 30000)
    {
        _netdiag_ts = now;
        NetDiag::tick();
        SelfHeal::check();
        WiFiModule::UI::updateRSSI((int)NetDiag::lastRssi.load());
    }

    // ── Stop widget timers (set from enterServiceMode / health tier 0.5) ────────
    // Must be processed here (main task) to avoid racing with mTimers::loop().
    if (_pending_stop_widgets)
    {
        _pending_stop_widgets = false;
        mTimers::clearByType("widget");
    }

    // ── BLE deinit (deferred from enterServiceMode to avoid blocking WS task) ──
    if (_pending_ble_deinit)
    {
        _pending_ble_deinit = false;
        HidKeyboard::deinit();
    }

    // ── Service mode ready broadcast ──────────────────────────────────────────
    // Fires AFTER widgets+BLE deinit have run. Guarantees browser does not
    // reload until DRAM is free, preventing parallel SPA asset storm while
    // BLE is still alive.
    if (_pending_svc_ready && _serviceMode && _serviceModeClientId != 0)
    {
        _pending_svc_ready  = false;
        _serviceModeLastMsg = now;  // refresh from main task — avoids cross-task stale read
        cJsonPtr msg = cJsonCreateObject();
        cJSON_AddStringToObject(msg.get(), "action", "service_mode_ready");
        sendCommand(std::move(msg));
        {
            multi_heap_info_t _hi;
            heap_caps_get_info(&_hi, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            ESP_LOGI("SVC", "Service mode READY — heap=%u  largest=%u",
                     ESP.getFreeHeap(), (unsigned)_hi.largest_free_block);
        }
    }

    // ── Service mode pending exit (set from WS onClose callback) ─────────────
    if (_pending_exit_svc_mode)
    {
        _pending_exit_svc_mode = false;
        exitServiceMode();
    }

    // ── Service mode timeouts ─────────────────────────────────────────────────
    // Use millis() directly (not stale `now`) because _serviceModeGraceStart is
    // set by the httpd task mid-loop-iteration; if it was set AFTER `now` was
    // captured, `now - graceStart` underflows (uint32_t) to ~4 billion, firing
    // the 60 s check instantly.
    if (_serviceMode)
    {
        uint32_t svcNow = millis();
        if (_serviceModeClientId == 0 && _serviceModeGraceStart != 0)
        {
            // Grace period: owner disconnected, waiting for Vue app to reconnect
            // with its session token (enter_service_mode + token → re-claim).
            // 60 s covers slow page loads and momentary WiFi hiccups.
            if (svcNow - _serviceModeGraceStart > 60000)
            {
                ESP_LOGW("SVC", "Grace period expired (60 s) — no re-claim, exiting service mode");
                exitServiceMode();
            }
        }
        else if (_serviceModeClientId != 0 && _serviceModeLastMsg != 0)
        {
            // Activity timeout: owner hasn't sent any WS message for 2 minutes
            if (svcNow - _serviceModeLastMsg > 120000)
            {
                ESP_LOGW("SVC", "Service mode owner inactive for 120 s — auto-exit");
                exitServiceMode();
            }
        }
    }

    // Health guard (thresholds calibrated for NimBLE PSRAM enabled):
    //   Tier 0.5: heap < 7 KB  for 10 s → pause widget timers (auto-resume)
    //             Normal idle with 1 WS + 1 HTTP ≈ 8-9 KB; 7 KB fires only when
    //             heap is genuinely constrained (e.g. 3 simultaneous connections).
    //   Tier 1:   heap < 5 KB  for 30 s → close all WS clients (recoverable)
    //   Tier 2:   heap < 2 KB  for 60 s → ESP.restart() (last resort)
    static uint32_t _heapWarnSince     = 0;
    static bool     _widgetsPaused     = false;
    static uint32_t _heapCriticalSince = 0;
    static uint32_t _heapEmergencySince = 0;
    uint32_t freeHeap = ESP.getFreeHeap();

    if (freeHeap < 2000)
    {
        if (!_heapEmergencySince)
            _heapEmergencySince = now;
        else if (now - _heapEmergencySince > 60000)
        {
            ESP_LOGE("HEALTH", "EMERGENCY: heap below 2 KB for 60 s — restarting ESP");
            delay(100);
            ESP.restart();
        }
    }
    else
    {
        _heapEmergencySince = 0;
    }

    if (freeHeap < 5000)
    {
        if (!_heapCriticalSince)
            _heapCriticalSince = now;
        else if (now - _heapCriticalSince > 30000)
        {
            ESP_LOGE("HEALTH", "Heap stuck below 5 KB for 30 s — closing all WS clients");
            wsCloseAll();
            _heapCriticalSince = 0;
        }
    }
    else
    {
        _heapCriticalSince = 0;
    }

    // Tier 0 (INSTANT): pause widgets immediately at 5 KB to prevent LVGL DMA
    // OOM panic. During WiFi reconnect storms, heap can drop from 10 KB to
    // OOM faster than the 10 s tier-0.5 window. LVGL flush then panics in
    // esp_lcd_panel_draw_bitmap → abort. Instant pause at 5 KB catches the
    // rapid-drop case before the display driver allocates.
    if (freeHeap < 5000 && !_widgetsPaused)
    {
        ESP_LOGW("HEALTH", "Heap CRITICAL (%u B) — pausing widgets instantly", freeHeap);
        _pending_stop_widgets = true;
        _widgetsPaused = true;
        if (!_heapWarnSince) _heapWarnSince = now;
    }
    // Tier 0.5: warn + pause widget timers when heap is low for 10s
    // Threshold: 7 KB — normal idle with 1 WS + 1 HTTP ≈ 8-9 KB.
    else if (freeHeap < 7000)
    {
        if (!_heapWarnSince) _heapWarnSince = now;
        else if (!_widgetsPaused && (now - _heapWarnSince) > 10000)
        {
            ESP_LOGW("HEALTH", "Heap below 7 KB for 10 s — pausing widget timers");
            _pending_stop_widgets = true;
            _widgetsPaused = true;
        }
    }
    else
    {
        if (_widgetsPaused)
        {
            ESP_LOGI("HEALTH", "Heap recovered above 10 KB — requesting widget refresh");
            _pending_widgets_refresh = true;
            _widgetsPaused = false;
        }
        _heapWarnSince = 0;
    }

    // Stall guard
    if (_webServeTimestamp && (now - _webServeTimestamp) > 60000)
    {
        ESP_LOGW("HEALTH", "webServeTimestamp stuck for 60 s — clearing");
        _webServeTimestamp = 0;
    }

    if (_pending_btn_size)
    {
        _pending_btn_size = false;
        // Rebuild macros so the new button size applies immediately.
        // Use rerender() which reads the in-memory PSRAM cache (always current).
        // The previous implementation read from NVS "macros"/"items" — a legacy
        // key that is no longer updated on save (macros live in LittleFS now),
        // so it returned stale or empty data and wiped the macro display.
        _ui_enable_mutex(1);
        Cards::Macros::rerender();
        _ui_disable_mutex(1);
    }

    if (_pending_scene_bg_reload)
    {
        _pending_scene_bg_reload = false;
        uSettings::reloadSceneBgs();  // safe NVS read — runs on main task, no LVGL mutex held
    }

    if (_pending_macro_cols)
    {
        _pending_macro_cols = false;
        // Sync on-device dropdown with the new column count (web UI may have
        // changed it while we're not looking).
        // LVGL mutex required — rerender and dropdown writes touch the LVGL
        // object tree concurrently with lv_timer_handler on loopTask (Core 1).
        _ui_enable_mutex(1);
        int cols = uSettings::getMacroCols();
        int cidx = cols - 2;
        if (cidx < 0) cidx = 0;
        if (cidx > 4) cidx = 4;
        lv_dropdown_set_selected(ui_dropdownGridStyle, (uint16_t)cidx);
        Cards::Macros::rerender();
        _ui_disable_mutex(1);
    }

    if (_pending_theme)
    {
        _pending_theme = false;
        uSettings::applyTheme(_pending_theme_v);
        lv_dropdown_set_selected(ui_swReverseColor, (uint16_t)_pending_theme_v);
        _ui_enable_mutex(1);
        Cards::Macros::rerender();
        Cards::Widgets::rerender();
        _ui_disable_mutex(1);
    }

    if (_pending_screen_rotate_ui)
    {
        _pending_screen_rotate_ui = false;
        lv_dropdown_set_selected(ui_dropdownScreenRotate, (uint16_t)_pending_screen_rotate_ui_v);
        uSettings::applyProfileLabelVisibility(_pending_screen_rotate_ui_v);
        // Re-run autosizer so landscape mode re-evaluates the available width
        // after rotation (the display dimensions change, center col width changes).
        uSettings::installProfileLabelAutoSizer();
    }

    if (_pending_brightness_ui)
    {
        _pending_brightness_ui = false;
        lv_slider_set_value(ui_sliderBrightness, _pending_brightness_ui_v, LV_ANIM_OFF);
    }

    if (_pending_bt_en_ui)
    {
        _pending_bt_en_ui = false;
        // Sync on-device LVGL switch with web-changed value. ui_event_swBluetooth
        // skips the value_changed event because cache already matches new value.
        if (_pending_bt_en_ui_v) lv_obj_add_state(ui_swBluetooth, LV_STATE_CHECKED);
        else                     lv_obj_clear_state(ui_swBluetooth, LV_STATE_CHECKED);
    }

    if (_pending_keyboard_tab)
    {
        _pending_keyboard_tab = false;
        _ui_enable_mutex(1);
        uSettings::applyKeyboardTabEnabled(_pending_keyboard_tab_v);
        _ui_disable_mutex(1);
        if (_pending_keyboard_tab_v) lv_obj_add_state(ui_swKeyboardTab, LV_STATE_CHECKED);
        else                         lv_obj_clear_state(ui_swKeyboardTab, LV_STATE_CHECKED);
    }

    if (_pending_mdns_restart)
    {
        _pending_mdns_restart = false;
        Helper::restartMDNS();
    }

    if (_pending_tab_pos)
    {
        _pending_tab_pos = false;
        _ui_enable_mutex(1);
        uSettings::applyTabPosition(_pending_tab_pos_v);
        _ui_disable_mutex(1);
    }

    if (_pending_status_bar_pos)
    {
        _pending_status_bar_pos = false;
        _ui_enable_mutex(1);
        uSettings::applyStatusBarPosition(_pending_status_bar_pos_v);
        _ui_disable_mutex(1);
    }

    if (_pending_masonry)
    {
        _pending_masonry = false;
        _ui_enable_mutex(1);
        Cards::Widgets::rerender();
        _ui_disable_mutex(1);
    }

    if (_pending_profile_apply)
    {
        _pending_profile_apply = false;
        Cards::Profiles::apply(_pending_profile_id);
    }

    // Active-profile change detector
    {
        static bool _profileTrackInit = false;
        static std::string _lastProfileId;
        std::string curId = Cards::Profiles::getActiveId();
        if (!_profileTrackInit)
        {
            _lastProfileId   = curId;
            _profileTrackInit = true;
        }
        else if (curId != _lastProfileId && wsClientCount() > 0)
        {
            _lastProfileId = curId;
            cJsonPtr notif = cJsonCreateObject();
            cJSON_AddStringToObject(notif.get(), "action", "profile_changed");
            cJSON_AddStringToObject(notif.get(), "id", curId.c_str());
            cJsonStrPtr s = cJsonPrint(notif.get());
            if (s)
            {
                wsTextAll(s.get());
                ESP_LOGI("WiFi.WSS", "profile_changed broadcast: %s", curId.c_str());
            }
        }
    }

    if (_pending_wifi_connect)
    {
        _pending_wifi_connect = false;
        Helper::connect(_pending_wifi_ssid, _pending_wifi_pass);
    }

    if (Helper::checkAndHandleReconnect()) {
        _pending_widgets_refresh = true;
        _pending_server_start    = true;
    }

    if (_pending_server_start)
    {
        _pending_server_start = false;
        startHttpServer();
    }

    if (_pending_widgets_refresh)
    {
        _pending_widgets_refresh = false;
        _pending_widgets_timer_resume = false;  // full rebuild supersedes timer-only
        Cards::Widgets::updateList();
    }
    else if (_pending_widgets_timer_resume)
    {
        _pending_widgets_timer_resume = false;
        Cards::Widgets::armTimersOnly();
    }

#ifdef ENABLE_SCREENSHOT_ENDPOINT
    // Lazy-init screenshot semaphores on first loop() call (main task context).
    if (!_screenshotCtx.trigger) {
        _screenshotCtx.trigger = xSemaphoreCreateBinary();
        _screenshotCtx.done    = xSemaphoreCreateBinary();
    }
    // If HTTP handler queued a snapshot request, render it here (LVGL task context).
    if (_screenshotCtx.trigger && xSemaphoreTake(_screenshotCtx.trigger, 0) == pdTRUE) {
        lv_obj_t* scr = lv_screen_active();
        lv_draw_buf_t db;
        lv_draw_buf_init(&db, _screenshotCtx.w, _screenshotCtx.h,
                         LV_COLOR_FORMAT_RGB565, _screenshotCtx.stride,
                         _screenshotCtx.pixBuf, _screenshotCtx.stride * _screenshotCtx.h);
        _screenshotCtx.ok = (lv_snapshot_take_to_draw_buf(scr, LV_COLOR_FORMAT_RGB565, &db) == LV_RESULT_OK);
        xSemaphoreGive(_screenshotCtx.done);
    }
#endif
}

} // namespace WiFiModule
