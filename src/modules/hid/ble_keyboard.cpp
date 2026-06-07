#include "ble_keyboard.h"
#include "cyrillic.h"
#include "../cards/macros.h"

#ifdef USE_NIMBLE

#include <Arduino.h>
#include "./ui/ui.h"
#include <NimBLEDevice.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static BleKeyboard* bleKeyboard = nullptr;

// ── Async BLE HID sender ──────────────────────────────────────────────────────
// bleKeyboard->write() blocks for setDelay ms per char. Calling it from the
// loopTask (which also runs lv_timer_handler) freezes the UI. Instead, push
// events here from loopTask (non-blocking xQueueSend), drain on a dedicated
// task pinned to Core 0 (same core as NimBLE host).

struct HidEvent { uint8_t type; char data[64]; };

static QueueHandle_t _hidQueue      = nullptr;
static TaskHandle_t  _hidSenderTask = nullptr;

static void _hidSenderTaskFn(void*) {
    HidEvent ev;
    for (;;) {
        if (xQueueReceive(_hidQueue, &ev, portMAX_DELAY) != pdTRUE) continue;
        if (!BleHidKeyboard::isInitialized()) continue;
        switch (ev.type) {
        case 0: BleHidKeyboard::print(ev.data);    break;
        case 1:
            BleHidKeyboard::press(ev.data);
            vTaskDelay(pdMS_TO_TICKS(12));          // ensure press/release in separate BLE events
            break;
        case 2: BleHidKeyboard::releaseAll();       break;
        }
    }
}

const MediaKeyReport* BleHidKeyboard::_lastMediaKey = nullptr;
bool BleHidKeyboard::_initialized = false;
bool BleHidKeyboard::_advPaused   = false;

void BleHidKeyboard::init()
{
    if (_initialized) return;

    if (!_hidQueue)
        _hidQueue = xQueueCreate(16, sizeof(HidEvent));
    if (!_hidSenderTask)
        xTaskCreatePinnedToCore(_hidSenderTaskFn, "ble_hid", 2560, nullptr, 4, &_hidSenderTask, 0);

    bleKeyboard = new BleKeyboard(SERVER_NAME);
    bleKeyboard->setDelay(10);  // now blocks sender task only, not loopTask
    bleKeyboard->begin();

    // sakul fork sets setScanResponse(false); under NimBLE 2.x that drops
    // the device name from the advertisement, so hosts see only a MAC
    // address ("Unknown device"). Re-enable scan response with the name.
    {
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        if (adv) {
            adv->stop();
            adv->enableScanResponse(true);
            NimBLEAdvertisementData scanData;
            scanData.setName(SERVER_NAME);
            adv->setScanResponseData(scanData);
            adv->start();
        }
        NimBLEServer *srv = NimBLEDevice::getServer();
        if (srv) srv->advertiseOnDisconnect(true);
    }

    _initialized = true;
}

void BleHidKeyboard::deinit()
{
    if (!_initialized) return;
    _initialized = false;
    // Flush pending queue entries so sender task sees _initialized=false and skips them.
    if (_hidQueue) xQueueReset(_hidQueue);
    // Wait for any in-flight sender call to return (max ~setDelay(10) per event).
    vTaskDelay(pdMS_TO_TICKS(25));
    bleKeyboard = nullptr;
    NimBLEDevice::deinit(true);
}

bool BleHidKeyboard::isInitialized()
{
    return _initialized;
}

void BleHidKeyboard::loop()
{
    if (!_initialized) return;

    static bool prevState = false;
    static bool firstRun  = true;
    static bool connParamsUpdated = false;

    if (!bleKeyboard) return;
    bool state = bleKeyboard->isConnected();

    // ESP32-S3 shares one radio — wider intervals + slave latency give WiFi
    // (AP beaconing, STA traffic) more airtime and prevent BLE cycling.
    if (state && !connParamsUpdated) {
        NimBLEServer *pServer = NimBLEDevice::getServer();
        if (pServer && pServer->getConnectedCount() > 0 && bleKeyboard) {
            uint16_t handle = pServer->getPeerInfo(0).getConnHandle();
            pServer->updateConnParams(handle, 24, 48, 4, 256);
            connParamsUpdated = true;
        }
    }
    if (!state) connParamsUpdated = false;

    if (state == prevState && !firstRun) return;

    prevState = state;
    firstRun  = false;

    Cards::Macros::refreshHidButtonStates();

    // On USB-capable boards, keyboard.cpp _switchToBle() sets symbol + initial color.
    // Here we only update the color (BLE connected=blue, disconnected=red).
    _ui_enable_mutex(1);
    lv_obj_set_style_text_color(
        ui_lblBluetooth,
        state ? lv_color_hex(0x0026FF) : lv_color_hex(0xFF0000),
        LV_PART_MAIN | LV_STATE_DEFAULT
    );
    _ui_disable_mutex(1);
}

KeyDictionaryVal* BleHidKeyboard::getKeysDictionary()
{
    static KeyDictionaryVal keyDictionary[KEY_DICTIONARY_SIZE] = {
        {"LEFT_CTRL", KEY_LEFT_CTRL},
        {"LEFT_SHIFT", KEY_LEFT_SHIFT},
        {"LEFT_ALT", KEY_LEFT_ALT},
        {"LEFT_GUI", KEY_LEFT_GUI},
        {"RIGHT_CTRL", KEY_RIGHT_CTRL},
        {"RIGHT_SHIFT", KEY_RIGHT_SHIFT},
        {"RIGHT_ALT", KEY_RIGHT_ALT},
        {"RIGHT_GUI", KEY_RIGHT_GUI},
        {"UP_ARROW", KEY_UP_ARROW},
        {"DOWN_ARROW", KEY_DOWN_ARROW},
        {"LEFT_ARROW", KEY_LEFT_ARROW},
        {"RIGHT_ARROW", KEY_RIGHT_ARROW},
        {"BACKSPACE", KEY_BACKSPACE},
        {"TAB", KEY_TAB},
        {"RETURN", KEY_RETURN},
        {"ENTER", KEY_RETURN},
        {"ESC", KEY_ESC},
        {"INSERT", KEY_INSERT},
        {"PRTSC", KEY_PRTSC},
        {"DELETE", KEY_DELETE},
        {"PAGE_UP", KEY_PAGE_UP},
        {"PAGE_DOWN", KEY_PAGE_DOWN},
        {"HOME", KEY_HOME},
        {"END", KEY_END},
        {"CAPS_LOCK", KEY_CAPS_LOCK},
        {"F1", KEY_F1},
        {"F2", KEY_F2},
        {"F3", KEY_F3},
        {"F4", KEY_F4},
        {"F5", KEY_F5},
        {"F6", KEY_F6},
        {"F7", KEY_F7},
        {"F8", KEY_F8},
        {"F9", KEY_F9},
        {"F10", KEY_F10},
        {"F11", KEY_F11},
        {"F12", KEY_F12},
        {"F13", KEY_F13},
        {"F14", KEY_F14},
        {"F15", KEY_F15},
        {"F16", KEY_F16},
        {"F17", KEY_F17},
        {"F18", KEY_F18},
        {"F19", KEY_F19},
        {"F20", KEY_F20},
        {"F21", KEY_F21},
        {"F22", KEY_F22},
        {"F23", KEY_F23},
        {"F24", KEY_F24},
        {"NUM_0", KEY_NUM_0},
        {"NUM_1", KEY_NUM_1},
        {"NUM_2", KEY_NUM_2},
        {"NUM_3", KEY_NUM_3},
        {"NUM_4", KEY_NUM_4},
        {"NUM_5", KEY_NUM_5},
        {"NUM_6", KEY_NUM_6},
        {"NUM_7", KEY_NUM_7},
        {"NUM_8", KEY_NUM_8},
        {"NUM_9", KEY_NUM_9},
        {"NUM_SLASH", KEY_NUM_SLASH},
        {"NUM_ASTERISK", KEY_NUM_ASTERISK},
        {"NUM_MINUS", KEY_NUM_MINUS},
        {"NUM_PLUS", KEY_NUM_PLUS},
        {"NUM_ENTER", KEY_NUM_ENTER},
        {"NUM_PERIOD", KEY_NUM_PERIOD}
    };

    return keyDictionary;
}

uint8_t BleHidKeyboard::getKeyCode(String findKey)
{
    const char *prefix = KEY_PREFIX;
    String keyStr = (findKey.indexOf(prefix) == 0)
        ? findKey.substring(strlen(prefix)).c_str()
        : findKey.c_str();

    const KeyDictionaryVal* keyDictionary = getKeysDictionary();

    for(int i = 0; i < KEY_DICTIONARY_SIZE; i++) {
        if (keyStr.equals(keyDictionary[i].name)) {
            return keyDictionary[i].value;
        }
    }

    return 0;
}

MediaKeyDictionaryVal* BleHidKeyboard::getMediaKeysDictionary()
{
    static MediaKeyDictionaryVal MediaKeyDictionary[MEDIA_KEY_DICTIONARY_SIZE] = {
        {"NEXT_TRACK", &KEY_MEDIA_NEXT_TRACK},
        {"PREVIOUS_TRACK", &KEY_MEDIA_PREVIOUS_TRACK},
        {"STOP", &KEY_MEDIA_STOP},
        {"PLAY_PAUSE", &KEY_MEDIA_PLAY_PAUSE},
        {"MUTE", &KEY_MEDIA_MUTE},
        {"VOLUME_UP", &KEY_MEDIA_VOLUME_UP},
        {"VOLUME_DOWN", &KEY_MEDIA_VOLUME_DOWN},
        {"WWW_HOME", &KEY_MEDIA_WWW_HOME},
        {"LOCAL_MACHINE_BROWSER", &KEY_MEDIA_LOCAL_MACHINE_BROWSER},
        {"CALCULATOR", &KEY_MEDIA_CALCULATOR},
        {"WWW_BOOKMARKS", &KEY_MEDIA_WWW_BOOKMARKS},
        {"WWW_SEARCH", &KEY_MEDIA_WWW_SEARCH},
        {"WWW_STOP", &KEY_MEDIA_WWW_STOP},
        {"WWW_BACK", &KEY_MEDIA_WWW_BACK},
        {"CONSUMER_CONTROL_CONFIGURATION", &KEY_MEDIA_CONSUMER_CONTROL_CONFIGURATION},
        {"EMAIL_READER", &KEY_MEDIA_EMAIL_READER}
    };

    return MediaKeyDictionary;
}

const MediaKeyReport* BleHidKeyboard::getMediaKey(String findKey)
{
    const char *prefix = MEDIA_KEY_PREFIX;
    String keyStr = (findKey.indexOf(prefix) == 0)
        ? findKey.substring(strlen(prefix))
        : findKey;

    const MediaKeyDictionaryVal* mediaKeyDictionary = getMediaKeysDictionary();

    for(int i = 0; i < MEDIA_KEY_DICTIONARY_SIZE; i++) {
        if (keyStr.equals(mediaKeyDictionary[i].name)) {
            return mediaKeyDictionary[i].value;
        }
    }

    return nullptr;
}

bool BleHidKeyboard::isConnected()
{
    return bleKeyboard && bleKeyboard->isConnected();
}

void BleHidKeyboard::print(char *text)
{
    bool hasMultibyte = false;
    for (const char *c = text; *c; c++) {
        if ((uint8_t)*c >= 0x80) { hasMultibyte = true; break; }
    }
    if (!hasMultibyte) {
        if (bleKeyboard) bleKeyboard->print(text);
        return;
    }

    const char *p = text;
    while (*p) {
        uint32_t cp = HidCyrillic::utf8Next(&p);

        if (cp < 0x80) {
            if (bleKeyboard) bleKeyboard->write((uint8_t)cp);
        } else {
            bool upper = HidCyrillic::isUpperCyrillic(cp);
            uint32_t lower = upper ? HidCyrillic::toLowerCyrillic(cp) : cp;
            char key = HidCyrillic::cyrillicKey(lower);
            if (key) {
                char send = upper ? HidCyrillic::shiftKey(key) : key;
                if (bleKeyboard) bleKeyboard->write((uint8_t)send);
                delay(5);
            }
        }
    }
}

void BleHidKeyboard::press(char *key)
{
    if (strncmp(key, MEDIA_KEY_PREFIX, strlen(MEDIA_KEY_PREFIX)) == 0) {
        const MediaKeyReport* keyCode = getMediaKey(key);

        if (keyCode != nullptr) {
            _lastMediaKey = keyCode;
            if (bleKeyboard) bleKeyboard->press(*keyCode);
        }
    } else if (strncmp(key, KEY_PREFIX, strlen(KEY_PREFIX)) == 0) {
        uint8_t keyCode = getKeyCode(key);

        if (keyCode > 0) {
            if (bleKeyboard) bleKeyboard->press(keyCode);
        }
    } else if (key && key[0] != '\0') {
        if (bleKeyboard) bleKeyboard->press((uint8_t)key[0]);
    }
}

void BleHidKeyboard::releaseAll()
{
    if (!bleKeyboard) return;
    bleKeyboard->releaseAll();

    // releaseAll() only zeros the keyboard HID report — does NOT release
    // media/consumer keys. Explicitly release the last pressed media key
    // so Windows stops treating it as held.
    if (_lastMediaKey != nullptr) {
        bleKeyboard->release(*_lastMediaKey);
        _lastMediaKey = nullptr;
    }
}

void BleHidKeyboard::pauseAdvertising()
{
    if (!_initialized) return;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv && adv->isAdvertising()) {
        adv->stop();
        _advPaused = true;
        ESP_LOGI("BLE", "Advertising paused for WiFi reconnect");
    }
}

void BleHidKeyboard::resumeAdvertising()
{
    if (!_initialized || !_advPaused) return;
    NimBLEServer *srv = NimBLEDevice::getServer();
    if (!srv || srv->getConnectedCount() == 0) {
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        if (adv) adv->start();
        ESP_LOGI("BLE", "Advertising resumed after WiFi reconnect");
    }
    _advPaused = false;
}

void BleHidKeyboard::enqueue(uint8_t type, const char* data)
{
    if (!_hidQueue) return;
    HidEvent ev;
    ev.type = type;
    strncpy(ev.data, data ? data : "", sizeof(ev.data) - 1);
    ev.data[sizeof(ev.data) - 1] = '\0';
    xQueueSend(_hidQueue, &ev, pdMS_TO_TICKS(5));  // drop on full rather than block loopTask
}

#else /* !USE_NIMBLE — no-BLE build. Empty stubs keep call sites compiling. */

const MediaKeyReport* BleHidKeyboard::_lastMediaKey = nullptr;
bool BleHidKeyboard::_initialized = false;
bool BleHidKeyboard::_advPaused   = false;

void BleHidKeyboard::init() {}
void BleHidKeyboard::deinit() {}
bool BleHidKeyboard::isInitialized() { return false; }
void BleHidKeyboard::loop() {}
KeyDictionaryVal* BleHidKeyboard::getKeysDictionary() { return nullptr; }
uint8_t BleHidKeyboard::getKeyCode(String) { return 0; }
MediaKeyDictionaryVal* BleHidKeyboard::getMediaKeysDictionary() { return nullptr; }
const MediaKeyReport* BleHidKeyboard::getMediaKey(String) { return nullptr; }
bool BleHidKeyboard::isConnected() { return false; }
void BleHidKeyboard::print(char*) {}
void BleHidKeyboard::press(char*) {}
void BleHidKeyboard::releaseAll() {}
void BleHidKeyboard::pauseAdvertising() {}
void BleHidKeyboard::resumeAdvertising() {}
void BleHidKeyboard::enqueue(uint8_t, const char*) {}

#endif
