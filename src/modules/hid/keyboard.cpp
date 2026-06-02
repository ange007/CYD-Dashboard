#include "keyboard.h"
#include <esp_log.h>
#include "./ui/ui.h"

HidKeyboard::Backend HidKeyboard::_active = HidKeyboard::NONE;

// ── Companion relay backend (available on ALL boards) ──────────────────────────
static CompanionHidKeyboard _companion;
// Written by setCompanionHidReady() from network task; read in loop() on main task.
static volatile bool     _companionReady    = false;
static volatile uint32_t _companionClientId = 0;

void HidKeyboard::setCompanionHidReady(bool ready, uint32_t clientId)
{
    _companionClientId = clientId;
    _companionReady    = ready;
    ESP_LOGI("HID", "Companion HID relay %s (client #%u)",
             ready ? "READY" : "GONE", clientId);
}

// ── Icon helper (common to all board types) ───────────────────────────────────
static void _updateHidIcon(HidKeyboard::Backend b)
{
    _ui_enable_mutex(1);
    if (b == HidKeyboard::COMPANION) {
        lv_label_set_text(ui_lblBluetooth, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(ui_lblBluetooth, lv_color_hex(0x00AA00),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (b == HidKeyboard::USB) {
        lv_label_set_text(ui_lblBluetooth, LV_SYMBOL_USB);
        lv_obj_set_style_text_color(ui_lblBluetooth, lv_color_hex(0x00AA00),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (b == HidKeyboard::BLE) {
        lv_label_set_text(ui_lblBluetooth, LV_SYMBOL_BLUETOOTH);
        // color updated by BleHidKeyboard::loop() based on connection state
    } else {
        // NONE: no HID backend active.
        // On no-BLE builds (USE_NIMBLE not defined) showing BLUETOOTH glyph is
        // misleading — use KEYBOARD instead to indicate HID is available via
        // companion or future USB, but not BLE.
#ifdef USE_NIMBLE
        lv_label_set_text(ui_lblBluetooth, LV_SYMBOL_BLUETOOTH);
#else
        lv_label_set_text(ui_lblBluetooth, LV_SYMBOL_KEYBOARD);
#endif
        lv_obj_set_style_text_color(ui_lblBluetooth, lv_color_hex(0x888888),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    _ui_disable_mutex(1);
}

// ── Companion switch helper (common to all board types) ───────────────────────
void HidKeyboard::_switchToCompanion(uint32_t clientId)
{
    // Keep BLE/USB stacks as-is — they may be needed when companion disconnects.
    _companion.init(clientId);
    _active = COMPANION;
    _updateHidIcon(COMPANION);
    ESP_LOGI("HID", "Active backend: COMPANION (WS #%u)", clientId);
}

// ──────────────────────────────────────────────────────────────────────────────
// USE_USB_HID path (ESP32-S3 / ESP32-C3)
// ──────────────────────────────────────────────────────────────────────────────
#ifdef USE_USB_HID

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

static constexpr uint32_t BOOT_TIMEOUT_MS  = 2000;
static constexpr uint32_t GRACE_TIMEOUT_MS = 10000;

static TimerHandle_t _bootTimer  = nullptr;
static TimerHandle_t _graceTimer = nullptr;

static volatile bool _pendingBle = false;

static void _bootTimerCb(TimerHandle_t)  { _pendingBle = true; }
static void _graceTimerCb(TimerHandle_t) { _pendingBle = true; }

void HidKeyboard::_switchToUsb()
{
    if (BleHidKeyboard::isInitialized()) BleHidKeyboard::deinit();
    _active = USB;
    _updateHidIcon(USB);
    ESP_LOGI("HID", "Active backend: USB");
}

void HidKeyboard::_switchToBle()
{
    if (!BleHidKeyboard::isInitialized()) BleHidKeyboard::init();
    _active = BLE;
    _updateHidIcon(BLE);
    ESP_LOGI("HID", "Active backend: BLE");
}

void HidKeyboard::init()
{
    UsbHidKeyboard::init();

    _graceTimer = xTimerCreate("hid_grace", pdMS_TO_TICKS(GRACE_TIMEOUT_MS),
                               pdFALSE, nullptr, _graceTimerCb);

    // Companion already signalled ready before init (unlikely at boot, but handle it)
    if (_companionReady) {
        _switchToCompanion(_companionClientId);
        return;
    }

    if (UsbHidKeyboard::isMounted()) {
        _switchToUsb();
        ESP_LOGI("HID", "USB already mounted at boot");
        return;
    }

    _bootTimer = xTimerCreate("hid_boot", pdMS_TO_TICKS(BOOT_TIMEOUT_MS),
                              pdFALSE, nullptr, _bootTimerCb);
    if (_bootTimer) xTimerStart(_bootTimer, 0);
    _active = NONE;
    ESP_LOGI("HID", "Waiting %.1fs for USB host...", BOOT_TIMEOUT_MS / 1000.0f);
}

void HidKeyboard::deinit()
{
    if (_bootTimer)  { xTimerDelete(_bootTimer,  0); _bootTimer  = nullptr; }
    if (_graceTimer) { xTimerDelete(_graceTimer, 0); _graceTimer = nullptr; }
    _companion.deinit();
    if (BleHidKeyboard::isInitialized()) BleHidKeyboard::deinit();
    _active     = NONE;
    _pendingBle = false;
}

void HidKeyboard::loop()
{
    // ── Companion has highest priority ────────────────────────────────────────
    if (_companionReady && _active != COMPANION) {
        if (_bootTimer)  xTimerStop(_bootTimer,  0);
        if (_graceTimer) xTimerStop(_graceTimer, 0);
        _pendingBle = false;
        _switchToCompanion(_companionClientId);
        return;
    }
    if (!_companionReady && _active == COMPANION) {
        // Companion disconnected — fall through to USB/BLE selection
        _companion.deinit();
        _active = NONE;
        _updateHidIcon(NONE);
        ESP_LOGI("HID", "Companion disconnected — falling back to USB/BLE");
        if (!UsbHidKeyboard::isMounted()) _pendingBle = true;
        // fall through ↓
    }
    if (_active == COMPANION) return;  // already handled above

    // ── USB / BLE selection ───────────────────────────────────────────────────
    bool mounted = UsbHidKeyboard::isMounted();

    if (mounted && _active != USB) {
        if (_graceTimer) xTimerStop(_graceTimer, 0);
        if (_bootTimer)  xTimerStop(_bootTimer,  0);
        _pendingBle = false;
        _switchToUsb();
    } else if (!mounted && _active == USB) {
        _active = NONE;
        _pendingBle = false;
        if (_graceTimer) xTimerReset(_graceTimer, 0);
        ESP_LOGI("HID", "USB unmounted — %us grace before BLE fallback",
                 (unsigned)(GRACE_TIMEOUT_MS / 1000));
    }

    if (_pendingBle && _active == NONE) {
        _pendingBle = false;
        _switchToBle();
    }

    if (_active == BLE) BleHidKeyboard::loop();
}

bool HidKeyboard::isConnected()
{
    if (_active == COMPANION) return _companion.isConnected();
    if (_active == USB) return UsbHidKeyboard::isMounted();
    if (_active == BLE) return BleHidKeyboard::isConnected();
    return false;
}

void HidKeyboard::print(char* text)
{
    if (_active == COMPANION) _companion.print(text);
    else if (_active == USB)  UsbHidKeyboard::print(text);
    else if (_active == BLE)  BleHidKeyboard::print(text);
}

void HidKeyboard::press(char* key)
{
    if (_active == COMPANION) _companion.press(key);
    else if (_active == USB)  UsbHidKeyboard::press(key);
    else if (_active == BLE)  BleHidKeyboard::press(key);
}

void HidKeyboard::releaseAll()
{
    if (_active == COMPANION) _companion.releaseAll();
    else if (_active == USB)  UsbHidKeyboard::releaseAll();
    else if (_active == BLE)  BleHidKeyboard::releaseAll();
}

void HidKeyboard::pauseAdvertising()  { BleHidKeyboard::pauseAdvertising(); }
void HidKeyboard::resumeAdvertising() { BleHidKeyboard::resumeAdvertising(); }

HidKeyboard::Backend HidKeyboard::activeBackend() { return _active; }

void HidKeyboard::_onBootTimerExpired()  {}
void HidKeyboard::_onGraceTimerExpired() {}

void HidKeyboard::applyBluetoothEnabled(bool on)
{
    if (!on) {
        if (BleHidKeyboard::isInitialized()) BleHidKeyboard::deinit();
        if (_active == BLE) {
            _active = NONE;
            _updateHidIcon(NONE);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// BLE-only path (plain ESP32 — no native USB-OTG)
// Companion relay still works — falls back to BLE when companion disconnects.
// ──────────────────────────────────────────────────────────────────────────────
#else

void HidKeyboard::_switchToBle()
{
    if (!BleHidKeyboard::isInitialized()) BleHidKeyboard::init();
    _active = BLE;
    _updateHidIcon(BLE);
    ESP_LOGI("HID", "Active backend: BLE");
}

void HidKeyboard::init()
{
    if (_companionReady) {
        _switchToCompanion(_companionClientId);
        return;
    }
    BleHidKeyboard::init();
    _active = BLE;
    _updateHidIcon(BLE);
}

void HidKeyboard::deinit()
{
    _companion.deinit();
    BleHidKeyboard::deinit();
    _active = NONE;
}

void HidKeyboard::loop()
{
    if (_companionReady && _active != COMPANION) {
        _switchToCompanion(_companionClientId);
        return;
    }
    if (!_companionReady && _active == COMPANION) {
        _companion.deinit();
        _switchToBle();
        return;
    }
    if (_active == COMPANION) return;
    BleHidKeyboard::loop();
}

bool HidKeyboard::isConnected()
{
    if (_active == COMPANION) return _companion.isConnected();
    return BleHidKeyboard::isConnected();
}

void HidKeyboard::print(char* text)
{
    if (_active == COMPANION) _companion.print(text);
    else BleHidKeyboard::print(text);
}

void HidKeyboard::press(char* key)
{
    if (_active == COMPANION) _companion.press(key);
    else BleHidKeyboard::press(key);
}

void HidKeyboard::releaseAll()
{
    if (_active == COMPANION) _companion.releaseAll();
    else BleHidKeyboard::releaseAll();
}

void HidKeyboard::pauseAdvertising()  { BleHidKeyboard::pauseAdvertising(); }
void HidKeyboard::resumeAdvertising() { BleHidKeyboard::resumeAdvertising(); }

HidKeyboard::Backend HidKeyboard::activeBackend() { return _active; }

void HidKeyboard::applyBluetoothEnabled(bool on)
{
    if (!on && BleHidKeyboard::isInitialized()) {
        BleHidKeyboard::deinit();
        if (_active == BLE) {
            _active = NONE;
            _updateHidIcon(NONE);
        }
    }
}

#endif
