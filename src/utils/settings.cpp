#include "settings.h"

#include "memory.h"
#include "constants.h"
#include <cJSON.h>
#include "json.h"
#include "heap_probe.h"

#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>

#include "../ui/ui.h"
#include "../ui/ui_themes.h"

#include <esp32_smartdisplay.h>

// Protects _sceneBgsJson against concurrent read (HTTP task) + write (main task
// after _pending_scene_bg_reload). The int statics are written only from loop()
// via pending flags and are read-atomic on 32-bit ARM — no mutex needed for them.
static SemaphoreHandle_t _settings_mutex = NULL;

static SemaphoreHandle_t getSettingsMutex() {
    if (!_settings_mutex) _settings_mutex = xSemaphoreCreateMutex();
    return _settings_mutex;
}

// Scene background mapping — cached JSON string {"scene_id": "filename.png", ...}
// Populated at boot (inside load()) and refreshed by reloadSceneBgs().
static std::string _sceneBgsJson = "{}";

// Cached values — populated by load(), used by getters
static int _btn_size          = SETTINGS_DEFAULT_BTN_SIZE;
static int _screen_rotate     = 0;
static int _macro_cols        = SETTINGS_DEFAULT_MACRO_COLS;
static int _brightness        = SETTINGS_DEFAULT_BRIGHTNESS;
static int _tab_pos           = SETTINGS_DEFAULT_TAB_POS;
static int _sleep_dim_timeout = SETTINGS_DEFAULT_SLEEP_DIM_TIMEOUT;
static int _sleep_dim_level   = SETTINGS_DEFAULT_SLEEP_DIM_LEVEL;
static int _sleep_off_timeout = SETTINGS_DEFAULT_SLEEP_OFF_TIMEOUT;
static int _masonry_style     = SETTINGS_DEFAULT_WIDGET_MASONRY;
static int _widget_columns    = SETTINGS_DEFAULT_WIDGET_COLUMNS;
static int _def_macro_bg      = SETTINGS_DEFAULT_MACRO_BG;
static int _def_macro_ic      = SETTINGS_DEFAULT_MACRO_ICON_CLR;
static int _def_macro_isz     = SETTINGS_DEFAULT_MACRO_ICON_SZ;
static int _def_macro_img_sz  = SETTINGS_DEFAULT_MACRO_IMAGE_SZ;
static int _def_widget_bg     = SETTINGS_DEFAULT_WIDGET_BG;
static int  _macro_radius      = SETTINGS_DEFAULT_MACRO_RADIUS;
static int  _macro_bg_opa      = SETTINGS_DEFAULT_MACRO_BG_OPA;
static int  _widget_bg_opa     = SETTINGS_DEFAULT_WIDGET_BG_OPA;
static int  _macro_title_pos   = SETTINGS_DEFAULT_MACRO_TITLE_POS;
static bool _macro_shadow      = (SETTINGS_DEFAULT_MACRO_SHADOW != 0);
static int  _macro_brd_clr  = SETTINGS_DEFAULT_MACRO_BORDER_CLR;
static bool _bt_enabled        = true;
static int  _status_bar_pos      = SETTINGS_DEFAULT_STATUS_BAR_POS;
static bool _status_bar_autohide = (SETTINGS_DEFAULT_STATUS_BAR_AUTOHIDE != 0);
static int  _status_bar_idle_s   = SETTINGS_DEFAULT_STATUS_BAR_IDLE_S;
static int  _status_bar_faded_opa = SETTINGS_DEFAULT_STATUS_BAR_FADED_OPA;
// autohide runtime state (not persisted)
static bool _status_bar_faded    = false;

static uint32_t _stateVersion = 0;

// ─── input theme helpers ─────────────────────────────────────────────────────
// Register theme tokens on a generic input control (textarea, button, etc.)
static void applyInputStyle(lv_obj_t* obj)
{
    if (!obj) return;

    ui_object_set_themeable_style_property(obj, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,     _ui_theme_color_widgetBg);
    ui_object_set_themeable_style_property(obj, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,       _ui_theme_alpha_widgetBg);
    ui_object_set_themeable_style_property(obj, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR,   _ui_theme_color_text);
    ui_object_set_themeable_style_property(obj, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,     _ui_theme_alpha_text);
    ui_object_set_themeable_style_property(obj, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_widgetBorder);
    ui_object_set_themeable_style_property(obj, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA,   _ui_theme_alpha_widgetBorder);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// Apply theme to a dropdown button + its list.
//   Modern flat look: thin border, ~6px radius, subtle bg, chevron right.
static void applyDropdownStyle(lv_obj_t* dd)
{
    if (!dd) return;

    applyInputStyle(dd);
    // Modern flat: small radius + thin border (border tokens already set by applyInputStyle)
    lv_obj_set_style_radius(dd, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dd, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(dd, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(dd, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t* list = lv_dropdown_get_list(dd);
    if (!list) return;

    ui_object_set_themeable_style_property(list, LV_PART_MAIN     | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,     _ui_theme_color_widgetBg);
    ui_object_set_themeable_style_property(list, LV_PART_MAIN     | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,       _ui_theme_alpha_widgetBg);
    ui_object_set_themeable_style_property(list, LV_PART_MAIN     | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR,   _ui_theme_color_text);
    ui_object_set_themeable_style_property(list, LV_PART_MAIN     | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,     _ui_theme_alpha_text);
    ui_object_set_themeable_style_property(list, LV_PART_MAIN     | LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_widgetBorder);
    ui_object_set_themeable_style_property(list, LV_PART_MAIN     | LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA,   _ui_theme_alpha_widgetBorder);
    lv_obj_set_style_radius(list, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(list, LV_PART_SELECTED | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,     _ui_theme_color_button);
    ui_object_set_themeable_style_property(list, LV_PART_SELECTED | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,       _ui_theme_alpha_button);
    ui_object_set_themeable_style_property(list, LV_PART_SELECTED | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR,   _ui_theme_color_buttonText);
    ui_object_set_themeable_style_property(list, LV_PART_SELECTED | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,     _ui_theme_alpha_buttonText);
}

// Apply theme to an lv_switch — modern pill-shaped toggle.
//   Off: light grey pill track, white knob.
//   On:  bright blue (button accent) pill track, white knob.
//   PART_MAIN      = track (full pill, no border)
//   PART_INDICATOR = highlighted track when CHECKED (accent blue)
//   PART_KNOB      = knob (always white via buttonText)
static void applySwitchStyle(lv_obj_t* sw)
{
    if (!sw) return;

    // Track: full pill, no border (indicator overlay handles checked state).
    ui_object_set_themeable_style_property(sw, LV_PART_MAIN      | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,     _ui_theme_color_widgetBg);
    ui_object_set_themeable_style_property(sw, LV_PART_MAIN      | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,       _ui_theme_alpha_widgetBg);
    lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Indicator (track when CHECKED): bright blue, full pill
    ui_object_set_themeable_style_property(sw, LV_PART_INDICATOR | LV_STATE_CHECKED, LV_STYLE_BG_COLOR,     _ui_theme_color_button);
    ui_object_set_themeable_style_property(sw, LV_PART_INDICATOR | LV_STATE_CHECKED, LV_STYLE_BG_OPA,       _ui_theme_alpha_button);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_INDICATOR | LV_STATE_CHECKED);

    // Knob: white circle with widgetBorder outline so it's visible on light tracks.
    // Border gives contrast in light themes (white knob + grey/amber border on near-white track).
    // Negative pad shrinks knob inside track (LVGL switch-specific behavior).
    ui_object_set_themeable_style_property(sw, LV_PART_KNOB      | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,     _ui_theme_color_buttonText);
    ui_object_set_themeable_style_property(sw, LV_PART_KNOB      | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,       _ui_theme_alpha_buttonText);
    ui_object_set_themeable_style_property(sw, LV_PART_KNOB      | LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_widgetBorder);
    ui_object_set_themeable_style_property(sw, LV_PART_KNOB      | LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA,   _ui_theme_alpha_widgetBorder);
    lv_obj_set_style_border_width(sw, 1, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(sw, -2, LV_PART_KNOB | LV_STATE_DEFAULT);
}

// Apply theme to an lv_slider / lv_bar — modern pill-shaped track.
//   Track: light pill bg.
//   Filled portion: bright blue.
//   Knob: bright blue circle, slightly larger than track height for a "ball-on-rail" look.
//   PART_MAIN      = track          (full pill, light bg)
//   PART_INDICATOR = filled portion (full pill, blue)
//   PART_KNOB      = drag handle    (circle, blue)
static void applySliderStyle(lv_obj_t* sl)
{
    if (!sl) return;

    // Track: full pill, light grey-blue bg
    ui_object_set_themeable_style_property(sl, LV_PART_MAIN      | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_widgetBg);
    ui_object_set_themeable_style_property(sl, LV_PART_MAIN      | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_widgetBg);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Indicator: bright blue fill, full pill
    ui_object_set_themeable_style_property(sl, LV_PART_INDICATOR | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_button);
    ui_object_set_themeable_style_property(sl, LV_PART_INDICATOR | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_button);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // Knob: white circle with blue border — contrasts against both the blue fill and
    // the grey track so it's always visible regardless of slider position.
    ui_object_set_themeable_style_property(sl, LV_PART_KNOB      | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,     _ui_theme_color_buttonText);
    ui_object_set_themeable_style_property(sl, LV_PART_KNOB      | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,       _ui_theme_alpha_buttonText);
    ui_object_set_themeable_style_property(sl, LV_PART_KNOB      | LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_button);
    ui_object_set_themeable_style_property(sl, LV_PART_KNOB      | LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA,   _ui_theme_alpha_button);
    lv_obj_set_style_border_width(sl, 2, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(sl, 4, LV_PART_KNOB | LV_STATE_DEFAULT);  // inflate knob beyond track
}

// Apply theme to an lv_keyboard.
//   PART_MAIN  = bg panel        (panelBackground)
//   PART_ITEMS = individual keys (button bg + buttonText)
//   PART_ITEMS+CHECKED = active modifier keys (caps / shift)
static void applyKeyboardStyle(lv_obj_t* kbd)
{
    if (!kbd) return;

    ui_object_set_themeable_style_property(kbd, LV_PART_MAIN  | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,   _ui_theme_color_panelBackground);
    ui_object_set_themeable_style_property(kbd, LV_PART_MAIN  | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,     _ui_theme_alpha_panelBackground);
    ui_object_set_themeable_style_property(kbd, LV_PART_ITEMS | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,   _ui_theme_color_button);
    ui_object_set_themeable_style_property(kbd, LV_PART_ITEMS | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,     _ui_theme_alpha_button);
    ui_object_set_themeable_style_property(kbd, LV_PART_ITEMS | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_buttonText);
    ui_object_set_themeable_style_property(kbd, LV_PART_ITEMS | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,   _ui_theme_alpha_buttonText);
    ui_object_set_themeable_style_property(kbd, LV_PART_ITEMS | LV_STATE_PRESSED, LV_STYLE_BG_COLOR,   _ui_theme_color_pressed_button);
    ui_object_set_themeable_style_property(kbd, LV_PART_ITEMS | LV_STATE_PRESSED, LV_STYLE_BG_OPA,     _ui_theme_alpha_pressed_button);
    ui_object_set_themeable_style_property(kbd, LV_PART_ITEMS | LV_STATE_CHECKED, LV_STYLE_BG_COLOR,   _ui_theme_color_accent);
    ui_object_set_themeable_style_property(kbd, LV_PART_ITEMS | LV_STATE_CHECKED, LV_STYLE_BG_OPA,     _ui_theme_alpha_accent);
}

// ─── persist helpers ────────────────────────────────────────────────────────

// NVS keys are limited to 15 characters. Silently truncated keys cause saves to
// be lost across reboots with no error from the Preferences library.
static bool nvsKeyOk(const char* key)
{
    if (strlen(key) <= 15) return true;

    ESP_LOGE("Settings", "NVS key too long (%d > 15): '%s' — value NOT saved!", strlen(key), key);

    return false;
}

void uSettings::saveStr(const char *key, const char *value)
{
    if (!nvsKeyOk(key)) return;

    uMemory::write(MEMORY_SETTINGS_KEY, [&](Preferences preferences) {
        preferences.putString(key, value);
    });
}

void uSettings::saveInt(const char *key, int32_t value)
{
    if (!nvsKeyOk(key)) return;

    uMemory::write(MEMORY_SETTINGS_KEY, [&](Preferences preferences) {
        preferences.putInt(key, value);
    });
}

void uSettings::saveBool(const char *key, bool value)
{
    if (!nvsKeyOk(key)) return;
    uMemory::write(MEMORY_SETTINGS_KEY, [&](Preferences preferences) {
        preferences.putBool(key, value);
    });
}

// ─── apply helpers (pure apply — no NVS writes here) ─────────────────────────

void uSettings::displayRotate(int index)
{
    __attribute__((unused)) auto disp = lv_disp_get_default();
    if      (index == 1) lv_disp_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    else if (index == 2) lv_disp_set_rotation(disp, LV_DISPLAY_ROTATION_180);
    else if (index == 3) lv_disp_set_rotation(disp, LV_DISPLAY_ROTATION_270);
    else                 lv_disp_set_rotation(disp, LV_DISPLAY_ROTATION_0);
}

void uSettings::applyProfileLabelVisibility(int rotation)
{
    // In 90°/270° mode the status bar is ~240 px wide.  The center column
    // (flex_grow) still fills the gap, but the "Profile:" label takes ~45px
    // leaving only ~35px for the button text — not enough to read a name.
    // Hide the label so the button gets the full center width.
    // In 90°/270° always hide — status bar is ~240 px wide, not enough room.
    // In 0°/180° do NOT force-show: the autosizer (_profileLabelAutosize_cb)
    // controls visibility based on available width. Force-showing here would
    // undo a correct hide from the autosizer, and this function fires on every
    // settings save (screen_rotate is always in the full-form payload).
    bool isVertical = (rotation == 1 || rotation == 3);
    if (ui_lblProfile && isVertical) {
        lv_obj_add_flag(ui_lblProfile, LV_OBJ_FLAG_HIDDEN);
    }
}

// Pure hardware apply — NO saveInt() inside here (would open NVS inside load's read callback)
void uSettings::displayBrightness(int value)
{
    if (value < 0)   value = 0;
    if (value > 100) value = 100;
    _brightness = value;
    smartdisplay_lcd_set_backlight(value / 100.0f);
}

// Called from HTTP handler via pending flag — runs on main loop task, safe to take LVGL mutex
void uSettings::applyTheme(int index)
{
    _ui_enable_mutex(1);

    uint8_t t;
    switch (index) {
        case 1:  t = UI_THEME_DARK;       break;
        case 2:  t = UI_THEME_OCEAN;      break;
        case 3:  t = UI_THEME_WARM;       break;
        case 4:  t = UI_THEME_REAL_BLACK; break;
        default: t = UI_THEME_DEFAULT;    break;
    }
    ui_theme_set(t);

    _ui_disable_mutex(1);
}

void uSettings::setBtnSize(int value)
{
    _btn_size = value;
}

int uSettings::getBtnSize()
{
    return _btn_size;
}

int uSettings::getMacroCols()
{
    return _macro_cols;
}

void uSettings::setMacroCols(int cols)
{
    _macro_cols = cols;
    saveInt(NVS_KEY("macro_cols"), cols);
}

// ── Unified save+apply helpers ────────────────────────────────────────────────
// These are the single source of truth for persisting a setting + applying it.
// Both ui_events.cpp (main loop) and server.cpp (HTTP handler) call these,
// eliminating the duplicated saveInt+displayXxx pattern.

void uSettings::setScreenRotate(int index)
{
    _screen_rotate = index;
    saveInt(NVS_KEY("screen_rotate"), index);
    displayRotate(index);
}

int uSettings::getScreenRotate() { return _screen_rotate; }

void uSettings::setBrightness(int value)
{
    saveInt(NVS_KEY("brightness"), value);
    displayBrightness(value);
}

void uSettings::setTheme(int index)
{
    // NVS only — LVGL theme switch is context-sensitive:
    //   • main loop  → call applyTheme() directly after this
    //   • HTTP task  → set _pending_theme flag; loop() calls applyTheme()
    saveInt(NVS_KEY("theme"), index);
}

String uSettings::getMdnsHost()
{
    String host = SERVER_NAME;
    uMemory::read(MEMORY_SETTINGS_KEY, [&host](Preferences preferences) {
        host = preferences.getString("mdns_host", SERVER_NAME);
    });

    return host;
}

void uSettings::setMdnsHost(const char* host)
{
    saveStr(NVS_KEY("mdns_host"), host);
}

void uSettings::setBtnSizeValue(int value)
{
    saveInt(NVS_KEY("btn_size"), value);
    _btn_size = value;
}

// ── Tab position ─────────────────────────────────────────────────────────────

void uSettings::applyTabPosition(int pos)
{
    _tab_pos = pos;

    // Update tab labels based on position
    bool isVertical = (pos == 1 || pos == 2);
    lv_tabview_set_tab_text(ui_tabsMain, 0, isVertical ? LV_SYMBOL_KEYBOARD : "Macros");
    lv_tabview_set_tab_text(ui_tabsMain, 1, isVertical ? LV_SYMBOL_LIST : "Widgets");

    lv_dir_t dir = LV_DIR_TOP;
    int      sz  = 35;
    lv_border_side_t side = LV_BORDER_SIDE_BOTTOM;
    if (pos == 1) { dir = LV_DIR_LEFT;   sz = 42; side = LV_BORDER_SIDE_RIGHT;  }
    if (pos == 2) { dir = LV_DIR_RIGHT;  sz = 42; side = LV_BORDER_SIDE_LEFT;   }
    if (pos == 3) { dir = LV_DIR_BOTTOM; sz = 35; side = LV_BORDER_SIDE_TOP;    }

    lv_tabview_set_tab_bar_position(ui_tabsMain, dir);
    lv_tabview_set_tab_bar_size(ui_tabsMain, sz);

    // ── LVGL 9 tab bar structure ────────────────────────────────────────────────
    // In LVGL 9, the tab bar is a plain lv_obj flex-container (NOT a btnmatrix).
    // lv_tabview_get_tab_btns() is a v8 compat macro → lv_tabview_get_tab_bar().
    // LV_PART_ITEMS has NO effect on a flex container; each tab is an individual
    // lv_button child that must be styled separately.
    // ───────────────────────────────────────────────────────────────────────────
    lv_obj_t* tab_bar = lv_tabview_get_tab_bar(ui_tabsMain);

    // Tab bar container background (MAIN selector works on a flex-container)
    // Do NOT set pad/border/size properties here — they change the tab_bar's
    // layout metrics AFTER lv_tabview_set_tab_bar_size() and can trigger
    // LV_EVENT_SIZE_CHANGED → lv_tabview_set_active → layout loop.
    ui_object_set_themeable_style_property(tab_bar, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_panelBackground);
    ui_object_set_themeable_style_property(tab_bar, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,   _ui_theme_alpha_panelBackground);

    // Style each individual tab button.
    // Avoid style calls that alter SIZE/PADDING on the tab_bar itself after
    // lv_tabview_set_tab_bar_size() — they trigger LV_EVENT_SIZE_CHANGED which
    // calls lv_tabview_set_active() → lv_obj_update_layout() and can hang the
    // device if executed synchronously inside this function.
    uint32_t tab_count = lv_tabview_get_tab_count(ui_tabsMain);
    for (uint32_t i = 0; i < tab_count; i++) {
        lv_obj_t* btn = lv_tabview_get_tab_button(ui_tabsMain, (int32_t)i);
        if (!btn) continue;

        // DEFAULT state — inactive tab
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,   _ui_theme_color_panelBackground);
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA,     _ui_theme_alpha_panelBackground);
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_text);
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,   _ui_theme_alpha_text);
        // Border is invisible in DEFAULT but same WIDTH as CHECKED — critical!
        // If border_width differs between states, the button changes size when
        // CHECKED state toggles, which marks layout dirty on every state change
        // → lv_obj_update_layout() while-loop never exits → device hangs.
        lv_obj_set_style_border_width(btn, 4, LV_PART_MAIN);   // constant width in ALL states
        lv_obj_set_style_border_side(btn, side, LV_PART_MAIN);  // constant side in ALL states
        lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN);

        // CHECKED state — active tab (only color/opacity overrides, NO size changes)
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_CHECKED, LV_STYLE_BG_COLOR,     _ui_theme_color_button);
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_CHECKED, LV_STYLE_BG_OPA,       _ui_theme_alpha_button);
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_CHECKED, LV_STYLE_TEXT_COLOR,   _ui_theme_color_buttonText);
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_CHECKED, LV_STYLE_TEXT_OPA,     _ui_theme_alpha_buttonText);
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_CHECKED, LV_STYLE_BORDER_COLOR, _ui_theme_color_tabBorder);
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_CHECKED, LV_STYLE_BORDER_OPA,   _ui_theme_alpha_tabBorder);
    }
}

void uSettings::setTabPos(int pos)
{
    saveInt(NVS_KEY("tab_pos"), pos);
}

int uSettings::getTabPos()
{
    return _tab_pos;
}

// ── Status bar placement + autohide ──────────────────────────────────────────

// Apply LVGL layout for the current status bar position.
// pos: 0=top, 1=bottom, 2=left, 3=right
// Side-effect: resizes+repositions ui_pnlStatus and shrinks ui_tabsMain so
// tabs never overlap the status panel.  Status bar keeps LV_OBJ_FLAG_FLOATING
// so touches on it don't scroll the screen flex container.
// Reconfigure status-bar child columns (Left, Profile center, Right) so their
// flex layout + size properties match the current orientation. Called by
// applyStatusBarPosition whenever the panel flips between horizontal and
// vertical. In horizontal mode each column keeps its fixed height=100% and
// inner FLEX_ROW (buttons side by side). In vertical mode the column takes
// full width of the 45 px strip and stacks its children top-to-bottom.
static void _statusBarReconfigChildren(bool vertical)
{
    lv_obj_t* cols[3] = { ui_pnlStatusLeft, ui_cntProfile, ui_pnlStatusRight };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* c = cols[i];
        if (!c) continue;
        if (vertical) {
            lv_obj_set_width(c, lv_pct(100));
            lv_obj_set_height(c, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_column(c, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_row(c,    5, LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_obj_set_height(c, lv_pct(100));
            // Center column grows via flex_grow, so keep width unset there;
            // Left/Right use LV_SIZE_CONTENT for a hug-children behaviour.
            if (c == ui_cntProfile) {
                lv_obj_set_width(c, LV_SIZE_CONTENT);
                lv_obj_set_flex_grow(c, 1);
            } else {
                lv_obj_set_width(c, LV_SIZE_CONTENT);
                lv_obj_set_flex_grow(c, 0);
            }
            lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_column(c, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_row(c,    0, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    // In vertical mode, let the center column (profile) grow vertically so
    // it consumes all leftover space between Left and Right columns.
    if (vertical && ui_cntProfile) {
        lv_obj_set_flex_grow(ui_cntProfile, 1);
        lv_obj_set_height(ui_cntProfile, LV_SIZE_CONTENT);
    }
}

void uSettings::applyStatusBarPosition(int pos)
{
    if (pos < 0 || pos > 3) pos = SETTINGS_DEFAULT_STATUS_BAR_POS;
    _status_bar_pos = pos;

    if (!ui_pnlStatus || !ui_tabsMain) return;

    const int SZ = SETTINGS_STATUS_BAR_SIZE_PX;
    lv_obj_t* screen = lv_obj_get_parent(ui_tabsMain);
    int scrW = screen ? lv_obj_get_width(screen)  : lv_display_get_horizontal_resolution(NULL);
    int scrH = screen ? lv_obj_get_height(screen) : lv_display_get_vertical_resolution(NULL);

    bool vertical = (pos == 2 || pos == 3);
    _statusBarReconfigChildren(vertical);

    // Screen uses FLEX_FLOW_ROW_WRAP — flex ignores lv_obj_set_align() for
    // non-floating children, so both tabview and status bar MUST be FLOATING
    // to be positioned by absolute coords we compute below.
    lv_obj_add_flag(ui_tabsMain,  LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(ui_pnlStatus, LV_OBJ_FLAG_FLOATING);
    // Clear any leftover align from .c init — use set_pos exclusively below.
    lv_obj_set_align(ui_tabsMain,  LV_ALIGN_TOP_LEFT);
    lv_obj_set_align(ui_pnlStatus, LV_ALIGN_TOP_LEFT);
    // Zero screen padding so (0,0) maps to the physical top-left corner;
    // LVGL positions floating children relative to content area (inside pad).
    if (screen) {
        lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // When autohide is on, tabview takes the full screen and the status bar
    // floats as an overlay (tab content peeks through when the bar fades).
    // EXCEPT when tab_pos and status_bar_pos share the same edge — then tabs
    // would hide behind the status bar, so we fall back to the shrink layout
    // to keep the tab-bar reachable.
    bool sameEdge = (_tab_pos == 0 && pos == 0) ||
                    (_tab_pos == 3 && pos == 1) ||
                    (_tab_pos == 1 && pos == 2) ||
                    (_tab_pos == 2 && pos == 3);
    bool overlay = _status_bar_autohide && !sameEdge;

    // Compute absolute rectangles for status bar and tabview.
    int tabsX = 0, tabsY = 0, tabsW = scrW, tabsH = scrH;
    int sbX   = 0, sbY   = 0, sbW   = scrW, sbH   = SZ;

    switch (pos) {
        case 0:  // top
            sbX = 0;     sbY = 0;     sbW = scrW; sbH = SZ;
            tabsX = 0;   tabsY = overlay ? 0 : SZ;
            tabsW = scrW; tabsH = overlay ? scrH : (scrH - SZ);
            lv_obj_set_flex_flow(ui_pnlStatus, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_border_side(ui_pnlStatus, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);
            break;
        case 2:  // left
            sbX = 0;     sbY = 0;     sbW = SZ;   sbH = scrH;
            tabsX = overlay ? 0 : SZ; tabsY = 0;
            tabsW = overlay ? scrW : (scrW - SZ); tabsH = scrH;
            lv_obj_set_flex_flow(ui_pnlStatus, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_border_side(ui_pnlStatus, LV_BORDER_SIDE_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
            break;
        case 3:  // right
            sbX = scrW - SZ; sbY = 0; sbW = SZ;   sbH = scrH;
            tabsX = 0;       tabsY = 0;
            tabsW = overlay ? scrW : (scrW - SZ); tabsH = scrH;
            lv_obj_set_flex_flow(ui_pnlStatus, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_border_side(ui_pnlStatus, LV_BORDER_SIDE_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            break;
        case 1:  // bottom (default)
        default:
            sbX = 0;    sbY = scrH - SZ; sbW = scrW; sbH = SZ;
            tabsX = 0;  tabsY = 0;
            tabsW = scrW; tabsH = overlay ? scrH : (scrH - SZ);
            lv_obj_set_flex_flow(ui_pnlStatus, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_border_side(ui_pnlStatus, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
            break;
    }

    lv_obj_set_pos (ui_pnlStatus, sbX, sbY);
    lv_obj_set_size(ui_pnlStatus, sbW, sbH);
    lv_obj_set_pos (ui_tabsMain,  tabsX, tabsY);
    lv_obj_set_size(ui_tabsMain,  tabsW, tabsH);
    lv_obj_invalidate(ui_pnlStatus);
    lv_obj_invalidate(ui_tabsMain);
}

void uSettings::setStatusBarPos(int pos)
{
    _status_bar_pos = pos;
    saveInt(NVS_KEY("sb_pos"), pos);
}
int uSettings::getStatusBarPos() { return _status_bar_pos; }

void uSettings::setStatusBarAutohide(bool on)
{
    bool changed = (_status_bar_autohide != on);
    _status_bar_autohide = on;
    saveBool(NVS_KEY("sb_autohide"), on);
    // If autohide just got disabled, make sure the panel is fully opaque again.
    if (!on && ui_pnlStatus) {
        lv_obj_set_style_opa(ui_pnlStatus, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        _status_bar_faded = false;
    }
    // Autohide mode drives whether tabview overlaps the status bar or not —
    // reflow layout when the mode flips.
    if (changed) applyStatusBarPosition(_status_bar_pos);
}
bool uSettings::getStatusBarAutohide() { return _status_bar_autohide; }

void uSettings::setStatusBarIdleSec(int s)
{
    if (s < 0)   s = 0;
    if (s > 600) s = 600;
    _status_bar_idle_s = s;
    saveInt(NVS_KEY("sb_idle_s"), s);
}
int uSettings::getStatusBarIdleSec() { return _status_bar_idle_s; }

void uSettings::setStatusBarFadedOpa(int opa)
{
    if (opa < 0)   opa = 0;
    if (opa > 255) opa = 255;
    _status_bar_faded_opa = opa;
    saveInt(NVS_KEY("sb_faded_opa"), opa);
    // If panel is currently faded, apply new level immediately.
    if (_status_bar_faded && ui_pnlStatus) {
        lv_obj_set_style_opa(ui_pnlStatus, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}
int uSettings::getStatusBarFadedOpa() { return _status_bar_faded_opa; }

// Called from main loop. Reuses LVGL's built-in inactivity timer so we share
// the same "activity" source as the dim/off logic — any touch on any widget
// resets it. No separate bookkeeping required.
void uSettings::tickStatusBarAutohide()
{
    if (!ui_pnlStatus) return;
    if (!_status_bar_autohide || _status_bar_idle_s <= 0) {
        if (_status_bar_faded) {
            lv_obj_set_style_opa(ui_pnlStatus, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            _status_bar_faded = false;
        }
        return;
    }

    uint32_t inactiveMs = lv_disp_get_inactive_time(NULL);
    uint32_t thresholdMs = (uint32_t)_status_bar_idle_s * 1000UL;

    if (inactiveMs >= thresholdMs) {
        if (!_status_bar_faded) {
            lv_obj_set_style_opa(ui_pnlStatus, _status_bar_faded_opa,
                                 LV_PART_MAIN | LV_STATE_DEFAULT);
            _status_bar_faded = true;
        }
    } else if (_status_bar_faded) {
        lv_obj_set_style_opa(ui_pnlStatus, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        _status_bar_faded = false;
    }
}

// Hide the "Profile:" label on ui_cntProfile whenever the profile dropdown
// would otherwise shrink below a readable width. Fires on every size change
// of ui_pnlStatus (rotation, status bar reposition, tab reposition all
// trigger one). Threshold covers ~8 chars at the default font.
static void _profileLabelAutosize_cb(lv_event_t* e)
{
    (void)e;
    if (!ui_pnlStatus || !ui_lblProfile || !ui_ddProfile) return;

    // In 90°/270° the status bar is always too narrow for the label — keep it
    // hidden regardless of what the width measurement says.  Without this check
    // a Macros::rerender() triggers a SIZE_CHANGED cascade that un-hides the
    // label even though rotation hasn't changed.
    int rot = uSettings::getScreenRotate();
    if (rot == 1 || rot == 3) {
        lv_obj_add_flag(ui_lblProfile, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Use the profile panel (center grow column) as the measurement source —
    // it already reflects leftover space after Left and Right columns.
    lv_obj_t* center = lv_obj_get_parent(ui_ddProfile);
    if (!center) return;
    // Flush layout so the flex-grow center column reports its real width
    // (SIZE_CHANGED on the parent fires before children re-layout).
    lv_obj_update_layout(center);
    int avail = lv_obj_get_width(center);
    // Use a FIXED label-width estimate, not lv_obj_get_width(ui_lblProfile):
    // once hidden the label reports width 0, so (avail - 0) would exceed the
    // threshold and immediately un-hide it → oscillation / label never hides.
    const int LABEL_W_EST    = 48;  // "Profile:" at default font
    const int DROPDOWN_MIN_W = 80;  // ~8 chars of profile name + chevron
    bool hide = avail < (LABEL_W_EST + DROPDOWN_MIN_W);
    if (hide) lv_obj_add_flag(ui_lblProfile, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_clear_flag(ui_lblProfile, LV_OBJ_FLAG_HIDDEN);
}

void uSettings::installProfileLabelAutoSizer()
{
    if (!ui_pnlStatus) return;
    lv_obj_remove_event_cb(ui_pnlStatus, _profileLabelAutosize_cb);
    lv_obj_add_event_cb(ui_pnlStatus, _profileLabelAutosize_cb,
                        LV_EVENT_SIZE_CHANGED, nullptr);
    // Kick once so the initial render matches the current size.
    _profileLabelAutosize_cb(nullptr);
}

// ── Brightness getter ─────────────────────────────────────────────────────────

int uSettings::getBrightness()
{
    return _brightness;
}

// ── Sleep / power management ──────────────────────────────────────────────────

int  uSettings::getSleepDimTimeout()  { return _sleep_dim_timeout; }
int  uSettings::getSleepDimLevel()    { return _sleep_dim_level; }
int  uSettings::getSleepOffTimeout()  { return _sleep_off_timeout; }

void uSettings::setSleepDimTimeout(int minutes) {
    _sleep_dim_timeout = minutes;
    saveInt(NVS_KEY("sleep_dim_t"), minutes);
}
void uSettings::setSleepDimLevel(int pct) {
    _sleep_dim_level = pct;
    saveInt(NVS_KEY("sleep_dim_l"), pct);
}
void uSettings::setSleepOffTimeout(int minutes) {
    _sleep_off_timeout = minutes;
    saveInt(NVS_KEY("sleep_off_t"), minutes);
}

// ─── load on boot ─────────────────────────────────────────────────────────────
// Runs in setup() — single-threaded, no async tasks yet.
// IMPORTANT: never call saveInt/saveBool/saveStr inside this callback — it would
// try to open the same NVS namespace for write while it is already open for read.

void uSettings::load()
{
    HeapProbe _probe("settings.load");
    uMemory::read(MEMORY_SETTINGS_KEY, [](Preferences preferences) {
        // Screen rotation
        int screenRotate = preferences.getInt("screen_rotate", 0);
        if (screenRotate < 0 || screenRotate > 3) { log_w("settings: screen_rotate out of range, reset"); screenRotate = 0; }
        _screen_rotate = screenRotate;
        lv_dropdown_set_selected(ui_dropdownScreenRotate, screenRotate);
        displayRotate(screenRotate);
        applyProfileLabelVisibility(screenRotate);

        // Theme — call ui_theme_set() directly (no mutex needed in single-threaded setup)
        int theme = preferences.getInt("theme", SETTINGS_DEFAULT_THEME);
        if (theme < 0 || theme > 4) { log_w("settings: theme out of range, reset"); theme = SETTINGS_DEFAULT_THEME; }
        uint8_t themeIdx;
        switch (theme) {
            case 1:  themeIdx = UI_THEME_DARK;       break;
            case 2:  themeIdx = UI_THEME_OCEAN;      break;
            case 3:  themeIdx = UI_THEME_WARM;       break;
            case 4:  themeIdx = UI_THEME_REAL_BLACK; break;
            default: themeIdx = UI_THEME_DEFAULT;    break;
        }
        ui_theme_set(themeIdx);
        // Sync on-device dropdown to the saved theme index
        lv_dropdown_set_selected(ui_swReverseColor, (uint16_t)theme);

        // Brightness — pure hardware apply, no NVS write
        int brightness = preferences.getInt("brightness", SETTINGS_DEFAULT_BRIGHTNESS);
        if (brightness < 0 || brightness > 100) { log_w("settings: brightness out of range, reset"); brightness = SETTINGS_DEFAULT_BRIGHTNESS; }
        displayBrightness(brightness);
        lv_slider_set_value(ui_sliderBrightness, brightness, LV_ANIM_OFF);

        // Button size — cache only (web UI controls this; on-device dropdown
        // repurposed to Macro Cols below).
        _btn_size = preferences.getInt("btn_size", SETTINGS_DEFAULT_BTN_SIZE);
        if (_btn_size < 30 || _btn_size > 200) { log_w("settings: btn_size out of range, reset"); _btn_size = SETTINGS_DEFAULT_BTN_SIZE; }

        // Macro column count — drives on-device "Macro Cols" dropdown (values 2..6 → idx 0..4)
        _macro_cols = preferences.getInt("macro_cols", SETTINGS_DEFAULT_MACRO_COLS);
        int cidx = _macro_cols - 2;
        if (cidx < 0) cidx = 0;
        if (cidx > 4) cidx = 4;
        lv_dropdown_set_selected(ui_dropdownGridStyle, (uint16_t)cidx);

        // mDNS hostname — pre-fill WiFi screen textarea
        String host = preferences.getString("mdns_host", SERVER_NAME);
        lv_textarea_set_text(ui_txtWifiHostname, host.c_str());

        // Startup tab — switch tabview to widgets tab if requested
        int startupTab = preferences.getInt("startup_tab", SETTINGS_DEFAULT_STARTUP_TAB);
        if (startupTab < 0 || startupTab > 1) { log_w("settings: startup_tab out of range, reset"); startupTab = SETTINGS_DEFAULT_STARTUP_TAB; }
        if (startupTab == 1) {
            lv_tabview_set_active(ui_tabsMain, 1, LV_ANIM_OFF);
        }

        // Tab position — apply after startup tab (position overrides default LV_DIR_TOP)
        int tabPos = preferences.getInt("tab_pos", SETTINGS_DEFAULT_TAB_POS);
        if (tabPos < 0 || tabPos > 3) { log_w("settings: tab_pos out of range, reset"); tabPos = SETTINGS_DEFAULT_TAB_POS; }
        applyTabPosition(tabPos);

        // Status bar placement + autohide (cache here, apply LVGL below so keys
        // stay grouped with other NVS reads inside this single-open lambda).
        int sbp = preferences.getInt("sb_pos",  SETTINGS_DEFAULT_STATUS_BAR_POS);
        if (sbp < 0 || sbp > 3) { log_w("settings: sb_pos out of range, reset"); sbp = SETTINGS_DEFAULT_STATUS_BAR_POS; }
        _status_bar_pos      = sbp;
        _status_bar_autohide = preferences.getBool("sb_autohide", SETTINGS_DEFAULT_STATUS_BAR_AUTOHIDE != 0);
        _status_bar_idle_s   = preferences.getInt ("sb_idle_s",   SETTINGS_DEFAULT_STATUS_BAR_IDLE_S);
        if (_status_bar_idle_s < 0 || _status_bar_idle_s > 600) _status_bar_idle_s = SETTINGS_DEFAULT_STATUS_BAR_IDLE_S;
        _status_bar_faded_opa = preferences.getInt ("sb_faded_opa", SETTINGS_DEFAULT_STATUS_BAR_FADED_OPA);
        if (_status_bar_faded_opa < 0 || _status_bar_faded_opa > 255) _status_bar_faded_opa = SETTINGS_DEFAULT_STATUS_BAR_FADED_OPA;

        // Sleep settings — cache only (applied in main loop)
        _sleep_dim_timeout = preferences.getInt("sleep_dim_t", SETTINGS_DEFAULT_SLEEP_DIM_TIMEOUT);
        _sleep_dim_level   = preferences.getInt("sleep_dim_l", SETTINGS_DEFAULT_SLEEP_DIM_LEVEL);
        if (_sleep_dim_level < 0 || _sleep_dim_level > 100) { log_w("settings: sleep_dim_l out of range, reset"); _sleep_dim_level = SETTINGS_DEFAULT_SLEEP_DIM_LEVEL; }
        _sleep_off_timeout = preferences.getInt("sleep_off_t", SETTINGS_DEFAULT_SLEEP_OFF_TIMEOUT);

        // Widget masonry style + column count
        _masonry_style  = preferences.getInt("widget_masonry",  SETTINGS_DEFAULT_WIDGET_MASONRY);
        _widget_columns = preferences.getInt("widget_columns",  SETTINGS_DEFAULT_WIDGET_COLUMNS);

        // Default styling
        _def_macro_bg   = preferences.getInt("def_m_bg",  SETTINGS_DEFAULT_MACRO_BG);
        _def_macro_ic   = preferences.getInt("def_m_ic",  SETTINGS_DEFAULT_MACRO_ICON_CLR);
        _def_macro_isz  = preferences.getInt("def_m_isz", SETTINGS_DEFAULT_MACRO_ICON_SZ);
        _def_macro_img_sz = preferences.getInt("def_m_imsz", SETTINGS_DEFAULT_MACRO_IMAGE_SZ);
        _def_widget_bg  = preferences.getInt("def_w_bg",  SETTINGS_DEFAULT_WIDGET_BG);

        // Visual appearance
        _macro_radius    = preferences.getInt("macro_radius",  SETTINGS_DEFAULT_MACRO_RADIUS);
        _macro_bg_opa    = preferences.getInt("macro_bg_opa",  SETTINGS_DEFAULT_MACRO_BG_OPA);
        _widget_bg_opa   = preferences.getInt("widget_bg_opa", SETTINGS_DEFAULT_WIDGET_BG_OPA);
        _macro_title_pos = preferences.getInt("macro_title",   SETTINGS_DEFAULT_MACRO_TITLE_POS);
        _macro_shadow    = preferences.getBool("macro_shadow", SETTINGS_DEFAULT_MACRO_SHADOW != 0);
        _bt_enabled      = preferences.getBool("bt_en", true);
        _macro_brd_clr = preferences.getInt("macro_brd_clr", SETTINGS_DEFAULT_MACRO_BORDER_CLR);

        // Scene background cache — load at boot so renderForScene() never reads NVS
        String sbStr = preferences.isKey("scene_bgs") ? preferences.getString("scene_bgs") : "{}";
        _sceneBgsJson = sbStr.c_str();
    });

    // Apply status bar position + install profile-label size autosizer AFTER NVS
    // lambda — both touch LVGL tree which isn't safe with NVS lock held.
    applyStatusBarPosition(_status_bar_pos);
    installProfileLabelAutoSizer();

    // Theme all input controls — must run outside the NVS lambda (LVGL, not NVS)
    applyDropdownStyle(ui_dropdownScreenRotate);
    applyDropdownStyle(ui_dropdownGridStyle);
    applyDropdownStyle(ui_swReverseColor);
    applyDropdownStyle(ui_ddWifiSSID);
    applyInputStyle(ui_txtWifiPass);
    applyInputStyle(ui_txtWifiHostname);
    applySliderStyle(ui_sliderBrightness);
    applyKeyboardStyle(ui_Keyboard);

#ifdef USE_NIMBLE
    applySwitchStyle(ui_swBluetooth);
    // Sync Bluetooth switch state — outside NVS lambda because lv_obj_add_state
    // triggers LV_EVENT_VALUE_CHANGED → ui_event_swBluetooth → setBluetoothEnabled
    // → saveBool. Doing this inside the read() lambda would deadlock the NVS lock.
    if (_bt_enabled) lv_obj_add_state(ui_swBluetooth, LV_STATE_CHECKED);
    else             lv_obj_clear_state(ui_swBluetooth, LV_STATE_CHECKED);
#else
    // No BLE stack on this board — hide the Bluetooth row in Settings.
    if (ui_swBluetooth) lv_obj_add_flag(lv_obj_get_parent(lv_obj_get_parent(ui_swBluetooth)),
                                         LV_OBJ_FLAG_HIDDEN);
#endif
}

// ── Widget masonry style + column count ──────────────────────────────────────

int  uSettings::getMasonryStyle()          { return _masonry_style; }
void uSettings::setMasonryStyle(int style) {
    _masonry_style = style;
    saveInt(NVS_KEY("widget_masonry"), style);
}

int  uSettings::getWidgetColumns()            { return _widget_columns; }
void uSettings::setWidgetColumns(int columns) {
    _widget_columns = columns;
    saveInt(NVS_KEY("widget_columns"), columns);
}

// ── Default styling ──────────────────────────────────────────────────────────

void uSettings::setDefaultMacroBg(int rgb)     { _def_macro_bg = rgb;  saveInt(NVS_KEY("def_m_bg"), rgb); }
int  uSettings::getDefaultMacroBg()           { return _def_macro_bg; }
void uSettings::setDefaultMacroIconColor(int rgb) { _def_macro_ic = rgb; saveInt(NVS_KEY("def_m_ic"), rgb); }
int  uSettings::getDefaultMacroIconColor()    { return _def_macro_ic; }
void uSettings::setDefaultMacroIconSize(int sz) { _def_macro_isz = sz;  saveInt(NVS_KEY("def_m_isz"), sz); }
int  uSettings::getDefaultMacroIconSize()     { return _def_macro_isz; }
void uSettings::setDefaultMacroImageSize(int sz) { _def_macro_img_sz = sz; saveInt(NVS_KEY("def_m_imsz"), sz); }
int  uSettings::getDefaultMacroImageSize()    { return _def_macro_img_sz; }
void uSettings::setDefaultWidgetBg(int rgb)    { _def_widget_bg = rgb;  saveInt(NVS_KEY("def_w_bg"), rgb); }
int  uSettings::getDefaultWidgetBg()          { return _def_widget_bg; }

// ── Visual appearance ────────────────────────────────────────────────────────

int  uSettings::getMacroRadius()          { return _macro_radius; }
void uSettings::setMacroRadius(int r)     { _macro_radius = r;   saveInt(NVS_KEY("macro_radius"), r); }

int  uSettings::getMacroBgOpa()           { return _macro_bg_opa; }
void uSettings::setMacroBgOpa(int opa)    { _macro_bg_opa = opa; saveInt(NVS_KEY("macro_bg_opa"), opa); }

int  uSettings::getWidgetBgOpa()          { return _widget_bg_opa; }
void uSettings::setWidgetBgOpa(int opa)   { _widget_bg_opa = opa; saveInt(NVS_KEY("widget_bg_opa"), opa); }

int  uSettings::getMacroTitlePos()        { return _macro_title_pos; }
void uSettings::setMacroTitlePos(int pos) { _macro_title_pos = pos; saveInt(NVS_KEY("macro_title"), pos); }

bool uSettings::getMacroShadow()              { return _macro_shadow; }
void uSettings::setMacroShadow(bool enabled)  { _macro_shadow = enabled; saveBool(NVS_KEY("macro_shadow"), enabled); }

bool uSettings::getBluetoothEnabled()         { return _bt_enabled; }
void uSettings::setBluetoothEnabled(bool on)  { _bt_enabled = on; saveBool(NVS_KEY("bt_en"), on); }

int  uSettings::getMacroBorderColor()         { return _macro_brd_clr; }
void uSettings::setMacroBorderColor(int rgb)  { _macro_brd_clr = rgb; saveInt(NVS_KEY("macro_brd_clr"), rgb); }

// ── Scene background cache ────────────────────────────────────────────────────
// Called from the main loop (outside LVGL mutex, safe to read NVS).
void uSettings::reloadSceneBgs() {
    uMemory::read(MEMORY_SETTINGS_KEY, [](Preferences prefs) {
        String v = prefs.isKey("scene_bgs") ? prefs.getString("scene_bgs") : "{}";
        SemaphoreHandle_t mx = getSettingsMutex();
        if (xSemaphoreTake(mx, pdMS_TO_TICKS(100)) == pdTRUE) {
            _sceneBgsJson = v.c_str();
            xSemaphoreGive(mx);
        } else {
            ESP_LOGW("Settings", "reloadSceneBgs: mutex timeout");
        }
    });
}

const char* uSettings::getSceneBgsJson() {
    // Returns pointer into std::string internal buffer. Caller must consume
    // before any concurrent write. Safe for render-path callers on main task.
    return _sceneBgsJson.c_str();
}

std::string uSettings::getSceneBgsJsonCopy() {
    SemaphoreHandle_t mx = getSettingsMutex();
    if (xSemaphoreTake(mx, pdMS_TO_TICKS(100)) == pdTRUE) {
        std::string copy = _sceneBgsJson;
        xSemaphoreGive(mx);
        return copy;
    }
    return "{}";
}

// ── State version counter ────────────────────────────────────────────────────

uint32_t uSettings::getStateVersion() { return _stateVersion; }
void     uSettings::bumpStateVersion() { _stateVersion++; }

// ── applyFromJson — single source of truth for settings POST / import ─────────
// Applies all recognized fields from obj, returns pending flags + WS broadcast JSON.
// Clamp int to [lo, hi]. Logs a warning if the input was out of range so
// corrupt imports or buggy clients leave a trail for debugging.
static int _clampLog(const char* name, int v, int lo, int hi) {
    if (v < lo) { ESP_LOGW("Settings", "clamp %s: %d < %d — using %d", name, v, lo, hi == INT32_MAX ? lo : lo); return lo; }
    if (v > hi) { ESP_LOGW("Settings", "clamp %s: %d > %d — using %d", name, v, hi, hi); return hi; }
    return v;
}

// Validation ranges — kept in one place so UI + firmware agree. Out-of-range
// values from a malicious client or a corrupt NVS import can otherwise feed
// e.g. brightness=99999 into the LCD driver (undefined behaviour) or
// btn_size=-50 into LVGL layout (negative widths → rendering panic).
#define CLAMP(key, lo, hi) _clampLog(key, v, lo, hi)

uSettings::SettingsApplyResult uSettings::applyFromJson(cJSON* obj) {
    HeapProbe _probe("settings.applyFromJson");
    SettingsApplyResult r;
    cJsonPtr updateOwner = cJsonCreateObject();
    if (!updateOwner) {
        log_e("settings: applyFromJson OOM on update object");
        return r;
    }
    cJSON_AddStringToObject(updateOwner.get(), "action", "settings_update");

    cJSON* item;

    item = cJSON_GetObjectItem(obj, "screen_rotate");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("screen_rotate", 0, 3); setScreenRotate(v); r.screen_rotate = true; r.screen_rotate_v = v; cJSON_AddNumberToObject(updateOwner.get(),"screen_rotate", v); }

    item = cJSON_GetObjectItem(obj, "theme");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("theme", 0, 4); setTheme(v); r.theme = true; r.theme_v = v; cJSON_AddNumberToObject(updateOwner.get(),"theme", v); }

    item = cJSON_GetObjectItem(obj, "brightness");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("brightness", 0, 100); setBrightness(v); r.brightness = true; r.brightness_v = v; cJSON_AddNumberToObject(updateOwner.get(),"brightness", v); }

    item = cJSON_GetObjectItem(obj, "btn_size");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("btn_size", 40, 200); setBtnSizeValue(v); r.btn_size = true; r.btn_size_v = v; cJSON_AddNumberToObject(updateOwner.get(),"btn_size", v); }

    item = cJSON_GetObjectItem(obj, "mdns_host");
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) > 0) { setMdnsHost(item->valuestring); r.mdns_restart = true; cJSON_AddStringToObject(updateOwner.get(),"mdns_host", item->valuestring); }

    item = cJSON_GetObjectItem(obj, "startup_tab");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; saveInt(NVS_KEY("startup_tab"), v); cJSON_AddNumberToObject(updateOwner.get(),"startup_tab", v); }

    item = cJSON_GetObjectItem(obj, "tab_pos");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("tab_pos", 0, 3); setTabPos(v); r.tab_pos = true; r.tab_pos_v = v; cJSON_AddNumberToObject(updateOwner.get(),"tab_pos", v); }

    item = cJSON_GetObjectItem(obj, "status_bar_pos");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("status_bar_pos", 0, 3); setStatusBarPos(v); r.status_bar_pos = true; r.status_bar_pos_v = v; cJSON_AddNumberToObject(updateOwner.get(),"status_bar_pos", v); }

    item = cJSON_GetObjectItem(obj, "status_bar_autohide");
    if (cJSON_IsBool(item)) { bool v = cJSON_IsTrue(item); setStatusBarAutohide(v); r.status_bar_autohide = true; r.status_bar_autohide_v = v; cJSON_AddBoolToObject(updateOwner.get(),"status_bar_autohide", v); }

    item = cJSON_GetObjectItem(obj, "status_bar_idle_s");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("status_bar_idle_s", 0, 600); setStatusBarIdleSec(v); r.status_bar_idle_s = true; r.status_bar_idle_s_v = v; cJSON_AddNumberToObject(updateOwner.get(),"status_bar_idle_s", v); }

    item = cJSON_GetObjectItem(obj, "status_bar_faded_opa");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("status_bar_faded_opa", 0, 255); setStatusBarFadedOpa(v); r.status_bar_faded_opa = true; r.status_bar_faded_opa_v = v; cJSON_AddNumberToObject(updateOwner.get(),"status_bar_faded_opa", v); }

    item = cJSON_GetObjectItem(obj, "sleep_dim_timeout");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("sleep_dim_timeout", 0, 1440); setSleepDimTimeout(v); cJSON_AddNumberToObject(updateOwner.get(),"sleep_dim_timeout", v); }

    item = cJSON_GetObjectItem(obj, "sleep_dim_level");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("sleep_dim_level", 0, 100); setSleepDimLevel(v); cJSON_AddNumberToObject(updateOwner.get(),"sleep_dim_level", v); }

    item = cJSON_GetObjectItem(obj, "sleep_off_timeout");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("sleep_off_timeout", 0, 1440); setSleepOffTimeout(v); cJSON_AddNumberToObject(updateOwner.get(),"sleep_off_timeout", v); }

    item = cJSON_GetObjectItem(obj, "widget_masonry");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("widget_masonry", 0, 2); setMasonryStyle(v); r.masonry = true; cJSON_AddNumberToObject(updateOwner.get(),"widget_masonry", v); }

    item = cJSON_GetObjectItem(obj, "widget_columns");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("widget_columns", 0, 4); setWidgetColumns(v); r.masonry = true; cJSON_AddNumberToObject(updateOwner.get(),"widget_columns", v); }

    item = cJSON_GetObjectItem(obj, "macro_cols");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("macro_cols", 1, 10); setMacroCols(v); r.macro_rerender = true; cJSON_AddNumberToObject(updateOwner.get(),"macro_cols", v); }

    item = cJSON_GetObjectItem(obj, "def_macro_bg");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; setDefaultMacroBg(v); cJSON_AddNumberToObject(updateOwner.get(),"def_macro_bg", v); }

    item = cJSON_GetObjectItem(obj, "def_macro_icon_clr");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; setDefaultMacroIconColor(v); r.macro_rerender = true; cJSON_AddNumberToObject(updateOwner.get(),"def_macro_icon_clr", v); }

    item = cJSON_GetObjectItem(obj, "def_macro_icon_sz");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("def_macro_icon_sz", 0, 2); setDefaultMacroIconSize(v); r.macro_rerender = true; cJSON_AddNumberToObject(updateOwner.get(),"def_macro_icon_sz", v); }

    item = cJSON_GetObjectItem(obj, "def_macro_image_sz");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("def_macro_image_sz", 0, 2); setDefaultMacroImageSize(v); r.macro_rerender = true; cJSON_AddNumberToObject(updateOwner.get(),"def_macro_image_sz", v); }

    item = cJSON_GetObjectItem(obj, "def_widget_bg");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; setDefaultWidgetBg(v); cJSON_AddNumberToObject(updateOwner.get(),"def_widget_bg", v); }

    item = cJSON_GetObjectItem(obj, "macro_radius");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("macro_radius", 0, 64); setMacroRadius(v); r.macro_rerender = true; cJSON_AddNumberToObject(updateOwner.get(),"macro_radius", v); }

    item = cJSON_GetObjectItem(obj, "macro_bg_opa");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("macro_bg_opa", 0, 255); setMacroBgOpa(v); r.macro_rerender = true; cJSON_AddNumberToObject(updateOwner.get(),"macro_bg_opa", v); }

    item = cJSON_GetObjectItem(obj, "widget_bg_opa");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; v = CLAMP("widget_bg_opa", 0, 255); setWidgetBgOpa(v); r.masonry = true; cJSON_AddNumberToObject(updateOwner.get(),"widget_bg_opa", v); }

    item = cJSON_GetObjectItem(obj, "macro_title_pos");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; setMacroTitlePos(v); r.macro_rerender = true; cJSON_AddNumberToObject(updateOwner.get(),"macro_title_pos", v); }

    item = cJSON_GetObjectItem(obj, "macro_shadow");
    if (cJSON_IsBool(item)) { bool v = cJSON_IsTrue(item); setMacroShadow(v); r.macro_rerender = true; cJSON_AddBoolToObject(updateOwner.get(),"macro_shadow", v); }
    item = cJSON_GetObjectItem(obj, "bt_en");
    if (cJSON_IsBool(item)) { bool v = cJSON_IsTrue(item); setBluetoothEnabled(v); r.bt_en = true; r.bt_en_v = v; cJSON_AddBoolToObject(updateOwner.get(), "bt_en", v); }

    item = cJSON_GetObjectItem(obj, "macro_brd_clr");
    if (cJSON_IsNumber(item)) { int v = (int)item->valuedouble; setMacroBorderColor(v); r.macro_rerender = true; cJSON_AddNumberToObject(updateOwner.get(),"macro_brd_clr", v); }

    bumpStateVersion();

    r.update = updateOwner.release();  // transfer ownership to caller
    return r;
}

