#pragma once
#ifndef __WIFI_SERVER_BASE_H__
#define __WIFI_SERVER_BASE_H__

#include <Arduino.h>
#include <cJSON.h>
#include "../../utils/json.h"
#include <FS.h>
#include <atomic>
#include <map>
#include <string>
#include <vector>
#include <freertos/portmacro.h>

#include "../../utils/settings.h"

namespace WiFiModule
{

// Abstract base class — holds all shared state and business logic.
// Library-specific I/O operations are pure virtual and implemented by
// AsyncWebServerImpl (ESPAsyncWebServer) or PsychicHttpImpl (PsychicHttp).
class WebServerBase
{
public:
    virtual ~WebServerBase() = default;

    // ── Lifecycle ──────────────────────────────────────────────────────────────
    virtual void start() = 0;
    void         loop();

    // ── WebSocket messaging ────────────────────────────────────────────────────
    bool sendCommandToClient(uint32_t clientId, const char *jsonStr);
    bool sendCommandToClient(uint32_t clientId, cJsonPtr&& json);
    bool sendCommand(const char *json);
    bool sendCommand(cJsonPtr&& json);
    bool sendCommandToProxy(cJsonPtr&& json);
    void broadcastSettingInt(const char *key, int value);
    bool sendBinaryToClient(uint32_t clientId, const uint8_t* buf, size_t len);

    // ── State queries ──────────────────────────────────────────────────────────
    virtual bool hasClients()  = 0;
    bool         isWebServing();
    uint32_t     getProxyClientId();
    void         clearProxyClientId();
    uint32_t     getCompanionClientId();
    void         setCompanionClientId(uint32_t id);
    void         triggerWidgetsRefresh();
    void         requestProfileSwitch(const char *profileId);

    // ── Service mode ───────────────────────────────────────────────────────────
    bool     isServiceMode();
    // token: optional; pass existing token on WS reconnect to re-claim ownership
    // without exiting service mode. NULL or empty = fresh claim (only allowed
    // when there is no active owner).
    bool        enterServiceMode(uint32_t clientId, const char* token = nullptr);
    void        exitServiceMode();
    uint32_t    getServiceModeClientId();
    const char* getServiceModeToken();  // 16-hex-char session token (empty when not active)
    void        markWidgetsDirty();     // see _widgets_dirty_in_svc

    // ── Log ring buffer ────────────────────────────────────────────────────────
    void pushLogEvent(cJsonPtr&& json);
    // Raw-string overload. Skips the cJSON_Print inside the cJsonPtr variant
    // — zero heap alloc on the push path. Caller is responsible for building
    // a valid JSON payload (and for any escaping). Used by widget update path
    // where logMsg JSON is composed inline via snprintf into a static buffer
    // — eliminates per-poll cJSON-tree allocations that fragment DRAM on
    // no-PSRAM boards.
    void pushLogEvent(const char* jsonStr);

    // ── Helpers ───────────────────────────────────────────────────────────────
    static String   mimeFor(const String &path);
    static cJsonPtr buildSettingsJson();

    // Apply pending flags from a settings update result (set from HTTP task, processed in loop).
    void applySettingsResult(const uSettings::SettingsApplyResult& r);

    // Trigger pending scene-bg reload after a WS save/scene_bg (mirrors HTTP POST handler).
    void triggerSceneBgReload();

    // Stage a WiFi credential connect (written to pending flags; executed in loop()).
    void stageWifiConnect(const char* ssid, const char* pass);

    // ── Shared upload helpers (icon / icon-src / background) ──────────────────
    // Call on each incoming chunk. Returns false on error (Psychic → ESP_FAIL).
    // index == 0: open file; final == true: close and set ok flag.
    bool handleIconUploadChunk(const String &filename, size_t index, uint8_t *data, size_t len, bool final);
    bool handleIconSrcUploadChunk(const String &filename, size_t index, uint8_t *data, size_t len, bool final);
    bool handleBgUploadChunk(const String &filename, size_t index, uint8_t *data, size_t len, bool final);

protected:
    // ── Library-specific server start (retry after WiFi connects) ────────────
    virtual void startHttpServer() {}

    // ── Library-specific WS operations (pure virtual) ─────────────────────────
    virtual void   wsText(uint32_t clientId, const char *msg) = 0;
    virtual void   wsTextAll(const char *msg)                 = 0;
    virtual size_t wsClientCount()                            = 0;
    virtual bool   wsClientQueueFull(uint32_t clientId)       = 0;
    virtual void   wsCloseAll()                               = 0;
    virtual void   wsCloseClient(uint32_t id)                 = 0;
    virtual void   wsCleanup()                                = 0;
    virtual bool   wsBinary(uint32_t clientId, const uint8_t* buf, size_t len) = 0;

    // ── Shared WS message processor (calls API::handleMessage) ────────────────
    void processWsMessage(uint32_t clientId, const char *msg);

    // ── Connection icon (LVGL) ─────────────────────────────────────────────────
    void updateConnectionIcon();

    // ── Upload state (icon / icon-src / background) ───────────────────────────
    File _iconUploadFile;    bool _iconUploadOk    = false;
    File _iconSrcUploadFile; bool _iconSrcUploadOk = false;
    File _bgUploadFile;      bool _bgUploadOk      = false;

    // ── Pending-flag state (set from HTTP task, processed in loop) ────────────
    volatile uint32_t        _webServeTimestamp   = 0;
    std::atomic<uint32_t>   _proxyClientId       {0};
    std::atomic<uint32_t>   _companionClientId   {0};
    uint32_t                _wifiReadTimestamp   = 0;

    volatile bool _pending_btn_size         = false;
    volatile int  _pending_btn_size_v       = 0;
    volatile bool _pending_macro_cols       = false;
    volatile bool _pending_theme            = false;
    volatile int  _pending_theme_v          = 0;
    volatile bool _pending_screen_rotate_ui = false;
    volatile int  _pending_screen_rotate_ui_v = 0;
    volatile bool _pending_brightness_ui   = false;
    volatile int  _pending_brightness_ui_v = 0;
    volatile bool _pending_bt_en_ui        = false;
    volatile bool _pending_bt_en_ui_v      = false;
    volatile bool _pending_mdns_restart    = false;
    volatile bool _pending_widgets_refresh = false;
    // Fast-path counterpart to _pending_widgets_refresh: only re-arm widget
    // update timers without destroying/recreating LVGL widget objects. Set by
    // exitServiceMode when no widget edits happened during the session.
    volatile bool _pending_widgets_timer_resume = false;
    volatile bool _pending_server_start   = false;  // retry httpd start after WiFi connects
    volatile bool _pending_tab_pos         = false;
    volatile int  _pending_tab_pos_v       = 0;
    volatile bool _pending_status_bar_pos  = false;
    volatile int  _pending_status_bar_pos_v= 0;
    volatile bool _pending_profile_apply   = false;
    char          _pending_profile_id[64]  = {};
    volatile bool _pending_masonry         = false;
    volatile int  _pending_masonry_v       = 0;
    volatile bool _pending_scene_bg_reload = false;  // reload scene_bgs cache before next rerender
    volatile bool _pending_wifi_connect    = false;
    char          _pending_wifi_ssid[33]   = {};
    char          _pending_wifi_pass[65]   = {};

    // ── Service mode state ─────────────────────────────────────────────────────
    volatile bool     _serviceMode           = false;
    volatile uint32_t _serviceModeClientId   = 0;
    volatile uint32_t _serviceModeLastMsg    = 0;  // millis() of last WS msg from owner
    volatile uint32_t _serviceModeGraceStart = 0;  // grace period start on owner disconnect
    char              _serviceModeToken[17]  = {}; // 16-hex + NUL; survives reconnect within grace
    volatile bool     _pending_exit_svc_mode  = false;
    volatile bool     _pending_stop_widgets   = false;  // set from WS task, processed in loop()
    volatile bool     _pending_ble_deinit    = false;  // defer HidKeyboard::deinit() to main task
    volatile bool     _pending_svc_ready     = false;  // deferred broadcast of service_mode_ready after deinit
    bool              _bleWasDeinitedForSvc  = false;  // tracks whether deinit was done so reinit is symmetric
    // Cleared on enterServiceMode, set when Macros::set / Widgets::set / Profiles::set
    // runs with isSave=true while _serviceMode is active. Drives the conditional
    // widget rebuild in exitServiceMode — if nothing changed we skip the
    // unconditional lv_obj_clean(ui_cntWidgets) + recreate and only re-arm
    // timers, eliminating the ~3 KB largest_free_block loss per cycle on no-PSRAM.
    volatile bool     _widgets_dirty_in_svc  = false;

    // ── WS fragment reassembly (shared data structure) ─────────────────────────
    static constexpr size_t   WS_MSG_MAX     = 8 * 1024;
    static constexpr uint32_t WS_MAX_CLIENTS = 4;
    std::map<uint32_t, std::vector<uint8_t>> _wsFragBuf;

    // ── Log ring buffer ────────────────────────────────────────────────────────
    struct LogEntry {
        uint32_t seq;
        uint32_t ts;
        char     payload[192];
    };
#ifdef BOARD_HAS_PSRAM
    static const int LOG_CAP = 32;
#else
    static const int LOG_CAP = 8;   // no-PSRAM: save ~4.8 KB heap (6.4→1.6 KB)
#endif
    LogEntry       *_logBuf  = nullptr;
    int             _logHead = 0;
    int             _logCnt  = 0;
    uint32_t        _logSeq  = 0;
    portMUX_TYPE    _logMux  = portMUX_INITIALIZER_UNLOCKED;

private:
    // LVGL connection icon callback (must be a plain function pointer for lv_async_call)
    static uint8_t _sConnIconMode;
    static void    _connIconUpdateCb(void *);
};

} // namespace WiFiModule

#endif // __WIFI_SERVER_BASE_H__
