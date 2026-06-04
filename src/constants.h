#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#define SERVER_NAME "CYD-Dashboard"
#define LVGL_REFRESH_TIME (10u)
#define WIFI_READ_TIMEOUT 1000
#define FMW_VERSION 0.1

const char MACROS_TYPE_KEYS[] = "keys";
const char MACROS_TYPE_COMMAND[] = "command";
const char MACROS_TYPE_URL[] = "url";
const char MACROS_TYPE_ACTION[] = "action";
const char MACROS_TYPE_SCENE[] = "scene";
const char MACROS_TYPE_BACK[] = "_back";
const char MACROS_TYPE_TOGGLE[] = "toggle";
const char MACROS_TYPE_MULTI[] = "multi";


const char WIDGET_TYPE_TEXT[] = "text";
const char WIDGET_TYPE_CHART[] = "chart";
const char WIDGET_TYPE_PROGRESS[] = "progress";
const char WIDGET_TYPE_COUNTER[] = "counter";
const char WIDGET_TYPE_TIMER[] = "timer";
const char WIDGET_TYPE_IMAGE[] = "image";

const char WIDGET_DATA_TARGET_URL[] = "url";
const char WIDGET_DATA_TARGET_URL_SYSTEM[] = "url_system";
const char WIDGET_DATA_TARGET_SYSTEM[] = "system";

#define MEMORY_SETTINGS_KEY "settings"
#define MEMORY_WIFI_KEY "wifi"
#define MEMORY_MACROS_KEY "macros"
#define MEMORY_WIDGETS_KEY "widgets"
#define MEMORY_STATES_KEY "states"

// Access Point defaults (used when no STA credentials are saved)
#define WIFI_AP_SSID "CYD-Dashboard"
#define WIFI_AP_PASS "dashboard"

// Default macro button size — adapts to screen resolution at compile time.
// DISPLAY_WIDTH is injected by the board definition (esp32-smartdisplay).
#if defined(DISPLAY_WIDTH) && DISPLAY_WIDTH >= 800
  #define SETTINGS_DEFAULT_BTN_SIZE  90
#elif defined(DISPLAY_WIDTH) && DISPLAY_WIDTH >= 480
  #define SETTINGS_DEFAULT_BTN_SIZE  70
#else
  #define SETTINGS_DEFAULT_BTN_SIZE  50
#endif

// Default macro column count (number of buttons per row)
#define SETTINGS_DEFAULT_MACRO_COLS  4

// Settings defaults
#define SETTINGS_DEFAULT_THEME       0    // 0 = light, 1 = dark, 2 = ocean, 3 = warm
#define SETTINGS_DEFAULT_BRIGHTNESS  100  // backlight 0-100 %
#define SETTINGS_DEFAULT_STARTUP_TAB 0    // 0 = macros, 1 = widgets
#define SETTINGS_DEFAULT_TAB_POS     0

// Status bar placement + behaviour
// status_bar_pos:      0=top, 1=bottom (default), 2=left, 3=right
// status_bar_autohide: fade status panel to low opacity on LVGL inactivity, restore on touch
// status_bar_idle_s:   seconds of inactivity before fade; 0 = autohide disabled
#define SETTINGS_DEFAULT_STATUS_BAR_POS         1
#define SETTINGS_DEFAULT_STATUS_BAR_AUTOHIDE    0    // 0=off, 1=on
#define SETTINGS_DEFAULT_STATUS_BAR_IDLE_S      5    // seconds
#define SETTINGS_DEFAULT_STATUS_BAR_FADED_OPA  40    // 0..255 opacity while faded (user-tunable)
#define SETTINGS_STATUS_BAR_SIZE_PX            45    // reserved space perpendicular to its edge

// Sleep / power management
#define SETTINGS_DEFAULT_SLEEP_DIM_TIMEOUT  5   // minutes until dim (0 = disabled)
#define SETTINGS_DEFAULT_SLEEP_DIM_LEVEL    20  // dim brightness %
#define SETTINGS_DEFAULT_SLEEP_OFF_TIMEOUT  0   // minutes after dim until off (0 = disabled)

// Widget layout
#define SETTINGS_DEFAULT_WIDGET_MASONRY  0  // 0=standard flex-wrap, 1=column-flex, 2=masonry layout
#define SETTINGS_DEFAULT_WIDGET_COLUMNS  0  // 0=auto (computed from display width), 2/3/4=fixed

// Default styling (0 = use hardcoded fallback)
#define SETTINGS_DEFAULT_MACRO_BG        0  // 24-bit RGB
#define SETTINGS_DEFAULT_MACRO_ICON_CLR  0  // 24-bit RGB (0 = auto from bg luminance)
#define SETTINGS_DEFAULT_MACRO_ICON_SZ   0  // 0=s, 1=m, 2=l
#define SETTINGS_DEFAULT_MACRO_IMAGE_SZ  1  // 0=s(50%), 1=m(75%), 2=l(100%) — image icon default
#define SETTINGS_DEFAULT_WIDGET_BG       0  // 24-bit RGB

// Visual appearance defaults
#define SETTINGS_DEFAULT_MACRO_RADIUS    5    // corner radius in px; 255 = auto-circle (btnSize/2)
#define SETTINGS_DEFAULT_MACRO_BG_OPA    255  // button background opacity (0=transparent, 255=opaque)
#define SETTINGS_DEFAULT_WIDGET_BG_OPA   255  // widget card background opacity
#define SETTINGS_DEFAULT_MACRO_TITLE_POS 0    // 0=inside button, 1=below button, 2=hidden
#define SETTINGS_DEFAULT_MACRO_SHADOW    1    // 1=show shadow, 0=disabled
#define SETTINGS_DEFAULT_MACRO_BORDER_CLR 0   // 0=no border, otherwise 24-bit RGB

// Compile-time guard for NVS key length. NVS silently truncates keys > 15 chars,
// causing saves to be lost. Use NVS_KEY("my_key") instead of bare string literals.
#define NVS_KEY(k) ([]() { \
    static_assert(sizeof(k) - 1 <= 15, "NVS key '" k "' exceeds 15-char limit"); \
    return k; \
}())

#endif // __CONSTANTS_H__