#ifndef __HID_BLE_KEYBOARD_H__
#define __HID_BLE_KEYBOARD_H__

#include <WString.h>
#include <map>

#ifdef USE_NIMBLE
#include <BleKeyboard.h>
#else
typedef uint8_t MediaKeyReport[2];
#endif

#include <constants.h>

#define KEY_DICTIONARY_SIZE 65
#define MEDIA_KEY_DICTIONARY_SIZE 16

const char KEY_PREFIX[] = "KEY_";
const char MEDIA_KEY_PREFIX[] = "KEY_MEDIA_";

typedef struct {
    const char* name;
    uint8_t value;
} KeyDictionaryVal;

typedef struct {
    const char* name;
    const MediaKeyReport* value;
} MediaKeyDictionaryVal;

class BleHidKeyboard {
public:
    static void init();
    static void deinit();
    static bool isInitialized();
    static void loop();

    static KeyDictionaryVal* getKeysDictionary();
    static uint8_t getKeyCode(String findKey);

    static MediaKeyDictionaryVal* getMediaKeysDictionary();
    static const MediaKeyReport* getMediaKey(String findKey);

    static bool isConnected();
    static void print(char *text);
    static void press(char *key);
    static void releaseAll();

    static void pauseAdvertising();
    static void resumeAdvertising();

    // Async send — non-blocking from LVGL/loopTask. Drained by dedicated sender task.
    // type: 0=print, 1=press, 2=release_all
    static void enqueue(uint8_t type, const char* data);

private:
    static const MediaKeyReport* _lastMediaKey;
    static bool _initialized;
    static bool _advPaused;
};

#endif
