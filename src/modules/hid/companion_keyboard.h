#ifndef __HID_COMPANION_KEYBOARD_H__
#define __HID_COMPANION_KEYBOARD_H__

#include <stdint.h>
#include <vector>
#include <string>

// HID relay via the companion desktop app.
// press() buffers key names. releaseAll() flushes them as a single
// {action:"exec_hid", type:"keys", keys:[...]} WS message so the companion
// can execute the full combo atomically (e.g. ["ctrl","c"] → KeyTap("c","ctrl")).
// print() sends a text event immediately. All methods are no-ops when not connected.
class CompanionHidKeyboard {
public:
    void init(uint32_t clientId);
    void deinit();
    bool isConnected() const { return _clientId != 0; }

    void press(const char* key);
    void print(const char* text);
    void releaseAll();

private:
    uint32_t _clientId = 0;
    std::vector<std::string> _pending;  // key names accumulated between press() and releaseAll()
};

#endif // __HID_COMPANION_KEYBOARD_H__
