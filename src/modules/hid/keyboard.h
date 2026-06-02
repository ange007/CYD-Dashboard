#ifndef __HID_KEYBOARD_H__
#define __HID_KEYBOARD_H__

#include "ble_keyboard.h"
#include "usb_keyboard.h"
#include "companion_keyboard.h"

// Facade for HID keyboard backends.
// Priority: COMPANION (WiFi relay) > USB (TinyUSB, S3/C3 only) > BLE (NimBLE).
// USE_USB_HID (auto-defined in usb_keyboard.h for S3/C3 boards):
//   runtime detect — USB if host enumerates, else BLE.
// No USE_USB_HID (plain ESP32): BLE only.
// Any board: companion relay activates when companion_hello reports hid_relay.
class HidKeyboard {
public:
    enum Backend : uint8_t { NONE = 0, BLE = 1, USB = 2, COMPANION = 3 };

    static void init();
    static void deinit();
    static void loop();

    static bool isConnected();
    static void print(char* text);
    static void press(char* key);
    static void releaseAll();

    static void pauseAdvertising();
    static void resumeAdvertising();

    static Backend activeBackend();

    // Apply bluetooth_enabled setting: deinit/skip BLE when false.
    // Called from applySettingsResult() in server_base.cpp.
    static void applyBluetoothEnabled(bool on);

    // Called from api.cpp (companion_hello with hid_relay) and WS disconnect handlers.
    // Sets volatile flags; actual backend switch happens in loop() on the main task.
    static void setCompanionHidReady(bool ready, uint32_t clientId = 0);

#ifdef USE_USB_HID
    // Called from FreeRTOS timer callbacks — only set flags, no heavy work.
    static void _onBootTimerExpired();
    static void _onGraceTimerExpired();
#endif

private:
    static Backend _active;

    static void _switchToCompanion(uint32_t clientId);  // all boards
    static void _switchToBle();                         // all boards

#ifdef USE_USB_HID
    static void _switchToUsb();
#endif
};

#endif
