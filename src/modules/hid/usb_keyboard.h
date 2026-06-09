#ifndef __HID_USB_KEYBOARD_H__
#define __HID_USB_KEYBOARD_H__

#include <stdint.h>

// DISABLE_USB_HID forces the BLE-only HID path on S3/C3 boards (debug builds)
// to reproduce/observe Bluetooth keyboard behaviour without USB taking over.
#if (defined(ARDUINO_ESP32S3_DEV) || defined(ARDUINO_ESP32C3_DEV)) && !defined(DISABLE_USB_HID)
#define USE_USB_HID 1
#endif

#ifdef USE_USB_HID

class UsbHidKeyboard {
public:
    static void init();
    static bool isMounted();

    static void print(char* text);
    static void press(char* key);
    static void releaseAll();
};

#endif // USE_USB_HID
#endif // __HID_USB_KEYBOARD_H__
