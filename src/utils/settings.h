#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include "Arduino.h"
#include <cJSON.h>
#include <string>

class uSettings {
public:
    // ── Settings apply result ─────────────────────────────────────────────────
    // Returned by applyFromJson(). Tells the server which pending flags to set
    // and carries a WS broadcast JSON object (caller must cJSON_Delete(update)).
    struct SettingsApplyResult {
        bool screen_rotate  = false; int  screen_rotate_v  = 0;
        bool theme          = false; int  theme_v          = 0;
        bool brightness     = false; int  brightness_v     = 0;
        bool btn_size       = false; int  btn_size_v       = 0;
        bool mdns_restart   = false;
        bool tab_pos        = false; int  tab_pos_v        = 0;
        bool status_bar_pos = false; int  status_bar_pos_v = 0;
        bool status_bar_autohide = false; bool status_bar_autohide_v = false;
        bool status_bar_idle_s   = false; int  status_bar_idle_s_v   = 0;
        bool status_bar_faded_opa= false; int  status_bar_faded_opa_v= 0;
        bool macro_rerender = false; // → _pending_macro_cols
        bool masonry        = false; // → _pending_masonry
        bool bt_en          = false; bool bt_en_v = true; // → HidKeyboard::applyBluetoothEnabled
        cJSON* update       = nullptr; // WS broadcast JSON — caller must cJSON_Delete
    };

    // Apply all recognized settings fields from a JSON object (parsed body).
    // Persists each field to NVS via the appropriate setXxx() call.
    // Does NOT free or touch obj. Returns result with pending flags + broadcast JSON.
    static SettingsApplyResult applyFromJson(cJSON* obj);


    // ── Persist helpers (raw NVS write) ──────────────────────────────────────
    static void saveStr(const char *key, const char *value);
    static void saveInt(const char *key, int32_t value);
    static void saveBool(const char *key, bool value);

    // ── Boot ─────────────────────────────────────────────────────────────────
    static void load();

    // ── Pure hardware-apply (no NVS, safe from any task/context) ─────────────
    static void displayRotate(int index);
    static void displayBrightness(int value);   // 0-100 %
    static void applyTheme(int index);          // 0=light 1=dark; acquires LVGL mutex
    static void applyProfileLabelVisibility(int rotation); // hide "Profile:" label on vertical screens
    static int  getScreenRotate();                          // 0-3; cached from NVS on load()

    // ── Unified save+apply (NVS write + hardware effect in one call) ──────────
    // Use these instead of saveInt() + displayXxx() pairs to avoid duplication.
    static void setScreenRotate(int index);     // saveInt + displayRotate
    static void setBrightness(int value);       // saveInt + displayBrightness
    static void setTheme(int index);            // saveInt only (LVGL apply is context-dependent)
    static void setBtnSizeValue(int value);     // saveInt + update cache

    // ── mDNS hostname ─────────────────────────────────────────────────────────
    static String getMdnsHost();                // read from NVS; falls back to SERVER_NAME
    static void   setMdnsHost(const char* host);// NVS write only

    // ── Button-size cache ─────────────────────────────────────────────────────
    static void setBtnSize(int value);          // cache only — kept for internal use
    static int  getBtnSize();

    // ── Macro column count ────────────────────────────────────────────────────
    static int  getMacroCols();                 // number of macro columns (2-6)
    static void setMacroCols(int cols);         // NVS write + cache update

    // ── Brightness cache ──────────────────────────────────────────────────────
    static int  getBrightness();                // returns last set brightness (for wake restore)

    // ── Tab position ──────────────────────────────────────────────────────────
    static void applyTabPosition(int pos);  // 0=top, 1=left, 2=right, 3=bottom (LVGL only, no NVS)
    static void setTabPos(int pos);         // NVS write only
    static int  getTabPos();

    // ── Status bar placement + autohide ───────────────────────────────────────
    // status_bar_pos: 0=top, 1=bottom, 2=left, 3=right
    static void applyStatusBarPosition(int pos); // LVGL only (re-aligns ui_pnlStatus + resizes ui_tabsMain)
    static void setStatusBarPos(int pos);        // NVS write only
    static int  getStatusBarPos();
    static void setStatusBarAutohide(bool on);   // NVS write only
    static bool getStatusBarAutohide();
    static void setStatusBarIdleSec(int s);      // NVS write only
    static int  getStatusBarIdleSec();
    static void setStatusBarFadedOpa(int opa);   // 0..255; clamp
    static int  getStatusBarFadedOpa();
    // Called from main loop tick to fade ui_pnlStatus on inactivity
    static void tickStatusBarAutohide();
    // Install size-change callback on ui_pnlStatus that hides "Profile:" label
    // when profile button gets less horizontal room than a small threshold.
    // Idempotent — safe to call multiple times (unregisters first).
    static void installProfileLabelAutoSizer();

    // ── Sleep / power management ──────────────────────────────────────────────
    static int  getSleepDimTimeout();           // minutes; 0 = disabled
    static void setSleepDimTimeout(int minutes);
    static int  getSleepDimLevel();             // dim brightness %
    static void setSleepDimLevel(int pct);
    static int  getSleepOffTimeout();           // minutes after dim → off; 0 = disabled
    static void setSleepOffTimeout(int minutes);

    // ── Widget layout ─────────────────────────────────────────────────────────
    static int  getMasonryStyle();              // 0=standard, 1=column-flex, 2=masonry
    static void setMasonryStyle(int style);     // NVS write + cache update
    static int  getWidgetColumns();             // 0=auto, 2/3/4=fixed column count
    static void setWidgetColumns(int columns);  // NVS write + cache update

    // ── Default styling ───────────────────────────────────────────────────────
    static void setDefaultMacroBg(int rgb);
    static int  getDefaultMacroBg();
    static void setDefaultMacroIconColor(int rgb);
    static int  getDefaultMacroIconColor();  // 0 = auto (contrast with bg)
    static void setDefaultMacroIconSize(int sz);
    static int  getDefaultMacroIconSize();   // 0=s, 1=m, 2=l
    static void setDefaultMacroImageSize(int sz);
    static int  getDefaultMacroImageSize();  // 0=s(50%), 1=m(75%), 2=l(100%)
    static void setDefaultWidgetBg(int rgb);
    static int  getDefaultWidgetBg();

    // ── Visual appearance ─────────────────────────────────────────────────────
    static int  getMacroRadius();                // 0-255 (255 = auto-circle = btnSize/2)
    static void setMacroRadius(int r);
    static int  getMacroBgOpa();                 // 0-255 button background opacity
    static void setMacroBgOpa(int opa);
    static int  getWidgetBgOpa();                // 0-255 widget card background opacity
    static void setWidgetBgOpa(int opa);
    static int  getMacroTitlePos();              // 0=inside, 1=below, 2=hidden
    static void setMacroTitlePos(int pos);
    static bool getMacroShadow();               // true=enabled, false=disabled
    static void setMacroShadow(bool enabled);

    // ── Bluetooth / HID backend ───────────────────────────────────────────────
    static bool getBluetoothEnabled();         // true = BLE allowed (default)
    static void setBluetoothEnabled(bool on);  // NVS write + cache update
    static int  getMacroBorderColor();          // 0=no border, otherwise 24-bit RGB
    static void setMacroBorderColor(int rgb);

    // ── Scene background cache ────────────────────────────────────────────────
    // Loaded at boot and refreshed via pending flag — never call from render path.
    static void        reloadSceneBgs();            // NVS read — call from main task only
    static const char* getSceneBgsJson();           // unsafe across tasks (ptr into std::string)
    static std::string getSceneBgsJsonCopy();       // thread-safe copy for HTTP-task callers

    // ── State version (for HTTP ETag on /api/init) ───────────────────────────
    // Monotonic counter incremented on any mutation to macros, widgets,
    // settings, profiles, scene backgrounds. Used as the ETag value for
    // /api/init so repeat fetches with matching If-None-Match return 304.
    // Lives in DRAM, resets to 0 on reboot — clients re-fetch after reconnect
    // anyway, so the reset is not visible to the SPA.
    static uint32_t getStateVersion();
    static void     bumpStateVersion();
};

#endif // __SETTINGS_H__
