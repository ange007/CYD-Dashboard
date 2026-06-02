#include "usb_keyboard.h"

#ifdef USE_USB_HID

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>
#include <esp_log.h>
#include "cyrillic.h"

// USBHIDKeyboard.h and BleKeyboard.h both typedef KeyReport — including both
// in the same TU causes a redefinition error. usb_keyboard.cpp therefore does
// NOT include ble_keyboard.h; key-name→code mapping is duplicated inline here.
// KEY_* constants are exposed by USBHIDKeyboard.h (already included above).

extern "C" bool tud_mounted(void);

// ── Key name → HID usage-id mapping (mirrors BleHidKeyboard::getKeysDictionary) ──
// Raw values — same encoding as Arduino Keyboard API (used by both BleKeyboard
// and USBHIDKeyboard). Inlined to avoid including BleKeyboard.h in this TU.

struct KeyEntry { const char* name; uint8_t code; };
static const KeyEntry KEY_MAP[] = {
    {"LEFT_CTRL",    0x80}, {"LEFT_SHIFT",   0x81},
    {"LEFT_ALT",     0x82}, {"LEFT_GUI",     0x83},
    {"RIGHT_CTRL",   0x84}, {"RIGHT_SHIFT",  0x85},
    {"RIGHT_ALT",    0x86}, {"RIGHT_GUI",    0x87},
    {"UP_ARROW",     0xDA}, {"DOWN_ARROW",   0xD9},
    {"LEFT_ARROW",   0xD8}, {"RIGHT_ARROW",  0xD7},
    {"BACKSPACE",    0xB2}, {"TAB",          0xB3},
    {"RETURN",       0xB0}, {"ENTER",        0xB0},
    {"ESC",          0xB1}, {"INSERT",       0xD1},
    {"PRTSC",        0xCE}, {"DELETE",       0xD4},
    {"PAGE_UP",      0xD3}, {"PAGE_DOWN",    0xD6},
    {"HOME",         0xD2}, {"END",          0xD5},
    {"CAPS_LOCK",    0xC1},
    {"F1",  0xC2}, {"F2",  0xC3}, {"F3",  0xC4}, {"F4",  0xC5},
    {"F5",  0xC6}, {"F6",  0xC7}, {"F7",  0xC8}, {"F8",  0xC9},
    {"F9",  0xCA}, {"F10", 0xCB}, {"F11", 0xCC}, {"F12", 0xCD},
    {"F13", 0xF0}, {"F14", 0xF1}, {"F15", 0xF2}, {"F16", 0xF3},
    {"F17", 0xF4}, {"F18", 0xF5}, {"F19", 0xF6}, {"F20", 0xF7},
    {"F21", 0xF8}, {"F22", 0xF9}, {"F23", 0xFA}, {"F24", 0xFB},
    {"NUM_0",        0xEA}, {"NUM_1",        0xE1},
    {"NUM_2",        0xE2}, {"NUM_3",        0xE3},
    {"NUM_4",        0xE4}, {"NUM_5",        0xE5},
    {"NUM_6",        0xE6}, {"NUM_7",        0xE7},
    {"NUM_8",        0xE8}, {"NUM_9",        0xE9},
    {"NUM_SLASH",    0xDC}, {"NUM_ASTERISK", 0xDD},
    {"NUM_MINUS",    0xDE}, {"NUM_PLUS",     0xDF},
    {"NUM_ENTER",    0xE0}, {"NUM_PERIOD",   0xEB},
};
static constexpr int KEY_MAP_SIZE = sizeof(KEY_MAP) / sizeof(KEY_MAP[0]);

static const char KEY_PREFIX[]       = "KEY_";
static const char MEDIA_KEY_PREFIX[] = "KEY_MEDIA_";

static uint8_t usbGetKeyCode(const char* findKey)
{
    const char* name = (strncmp(findKey, KEY_PREFIX, 4) == 0) ? findKey + 4 : findKey;
    for (int i = 0; i < KEY_MAP_SIZE; i++) {
        if (strcmp(name, KEY_MAP[i].name) == 0) return KEY_MAP[i].code;
    }
    return 0;
}

// ── Global USB HID instances ──────────────────────────────────────────────────
// Constructors run before app_main() → registered with TinyUSB before USB.begin().
static USBHIDKeyboard        _kbd;
static USBHIDConsumerControl _consumer;

// ─────────────────────────────────────────────────────────────────────────────

void UsbHidKeyboard::init()
{
    _kbd.begin();
    _consumer.begin();
    // Do NOT call USB.begin() here: ARDUINO_USB_CDC_ON_BOOT=1 already started
    // the USB stack in initArduino(). Calling it again triggers a second
    // re-enumeration that confuses Windows driver assignment for the composite
    // CDC+HID descriptor. begin() on the HID classes is sufficient.
    ESP_LOGI("USB_HID", "USB HID keyboard + consumer control init done");
}

bool UsbHidKeyboard::isMounted()
{
    return tud_mounted();
}

void UsbHidKeyboard::print(char* text)
{
    if (!tud_mounted()) return;

    bool hasMultibyte = false;
    for (const char* c = text; *c; c++) {
        if ((uint8_t)*c >= 0x80) { hasMultibyte = true; break; }
    }
    if (!hasMultibyte) {
        _kbd.print(text);
        return;
    }

    const char* p = text;
    while (*p) {
        uint32_t cp = HidCyrillic::utf8Next(&p);
        if (!cp) break;
        if (cp < 0x80) {
            _kbd.write((uint8_t)cp);
        } else {
            bool upper   = HidCyrillic::isUpperCyrillic(cp);
            uint32_t low = upper ? HidCyrillic::toLowerCyrillic(cp) : cp;
            char key     = HidCyrillic::cyrillicKey(low);
            if (key) {
                char send = upper ? HidCyrillic::shiftKey(key) : key;
                _kbd.write((uint8_t)send);
                delay(5);
            }
        }
    }
}

void UsbHidKeyboard::press(char* key)
{
    if (!tud_mounted()) return;

    if (strncmp(key, MEDIA_KEY_PREFIX, strlen(MEDIA_KEY_PREFIX)) == 0) {
        const char* name = key + strlen(MEDIA_KEY_PREFIX);
        // HID Consumer Control usage IDs (page 0x0C)
        struct { const char* n; uint16_t u; } map[] = {
            {"NEXT_TRACK",                    0x00B5},
            {"PREVIOUS_TRACK",                0x00B6},
            {"STOP",                          0x00B7},
            {"PLAY_PAUSE",                    0x00CD},
            {"MUTE",                          0x00E2},
            {"VOLUME_UP",                     0x00E9},
            {"VOLUME_DOWN",                   0x00EA},
            {"WWW_HOME",                      0x0223},
            {"LOCAL_MACHINE_BROWSER",         0x0194},
            {"CALCULATOR",                    0x0192},
            {"WWW_BOOKMARKS",                 0x022A},
            {"WWW_SEARCH",                    0x0221},
            {"WWW_STOP",                      0x0226},
            {"WWW_BACK",                      0x0224},
            {"CONSUMER_CONTROL_CONFIGURATION",0x0183},
            {"EMAIL_READER",                  0x018A},
        };
        for (auto& e : map) {
            if (strcmp(name, e.n) == 0) { _consumer.press(e.u); return; }
        }
    } else if (strncmp(key, KEY_PREFIX, strlen(KEY_PREFIX)) == 0) {
        uint8_t kc = usbGetKeyCode(key);
        if (kc) _kbd.press(kc);
    } else if (key && key[0]) {
        _kbd.press((uint8_t)key[0]);
    }
}

void UsbHidKeyboard::releaseAll()
{
    _kbd.releaseAll();
    _consumer.release();
}

#endif // USE_USB_HID
