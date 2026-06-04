#include "companion_keyboard.h"

#include <cJSON.h>
#include <esp_log.h>

#include "../../utils/json.h"
#include "../wifi/server.h"

void CompanionHidKeyboard::init(uint32_t clientId)
{
    _clientId = clientId;
    _pending.clear();
}

void CompanionHidKeyboard::deinit()
{
    _pending.clear();
    _clientId = 0;
}

void CompanionHidKeyboard::press(const char* key)
{
    if (!key || !_clientId) return;
    _pending.emplace_back(key);
}

void CompanionHidKeyboard::print(const char* text)
{
    if (!text || !_clientId) return;
    cJsonPtr msg = cJsonCreateObject();
    if (!msg) return;
    cJSON_AddStringToObject(msg.get(), "action", "exec_hid");
    cJSON_AddStringToObject(msg.get(), "type",   "text");
    cJSON_AddStringToObject(msg.get(), "text",   text);
    WiFiModule::WSServer::sendCommandToClient(_clientId, std::move(msg));
}

void CompanionHidKeyboard::releaseAll()
{
    if (!_clientId || _pending.empty()) {
        _pending.clear();
        return;
    }
    cJsonPtr msg = cJsonCreateObject();
    if (!msg) { _pending.clear(); return; }
    cJSON_AddStringToObject(msg.get(), "action", "exec_hid");
    cJSON_AddStringToObject(msg.get(), "type",   "keys");
    cJSON* arr = cJSON_CreateArray();
    if (!arr) { _pending.clear(); return; }
    for (const auto& k : _pending)
        cJSON_AddItemToArray(arr, cJSON_CreateString(k.c_str()));
    cJSON_AddItemToObject(msg.get(), "keys", arr);
    WiFiModule::WSServer::sendCommandToClient(_clientId, std::move(msg));
    _pending.clear();
}
