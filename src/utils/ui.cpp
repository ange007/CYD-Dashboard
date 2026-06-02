#include "ui.h"

#include <esp_log.h>

#include "../ui/ui.h"
#include "../utils/json.h"

std::map<String, const char*> uUI::getSymbolsDictionary()
{
    std::map<String, const char*> symbols;

    symbols["BULLET"] = LV_SYMBOL_BULLET;
    symbols["AUDIO"] = LV_SYMBOL_AUDIO;
    symbols["VIDEO"] = LV_SYMBOL_VIDEO;
    symbols["LIST"] = LV_SYMBOL_LIST;
    symbols["OK"] = LV_SYMBOL_OK;
    symbols["CLOSE"] = LV_SYMBOL_CLOSE;
    symbols["POWER"] = LV_SYMBOL_POWER;
    symbols["SETTINGS"] = LV_SYMBOL_SETTINGS;
    symbols["HOME"] = LV_SYMBOL_HOME;
    symbols["DOWNLOAD"] = LV_SYMBOL_DOWNLOAD;
    symbols["DRIVE"] = LV_SYMBOL_DRIVE;
    symbols["REFRESH"] = LV_SYMBOL_REFRESH;
    symbols["MUTE"] = LV_SYMBOL_MUTE;
    symbols["VOLUME_MID"] = LV_SYMBOL_VOLUME_MID;
    symbols["VOLUME_MAX"] = LV_SYMBOL_VOLUME_MAX;
    symbols["IMAGE"] = LV_SYMBOL_IMAGE;
    symbols["TINT"] = LV_SYMBOL_TINT;
    symbols["PREV"] = LV_SYMBOL_PREV;
    symbols["PLAY"] = LV_SYMBOL_PLAY;
    symbols["PAUSE"] = LV_SYMBOL_PAUSE;
    symbols["STOP"] = LV_SYMBOL_STOP;
    symbols["NEXT"] = LV_SYMBOL_NEXT;
    symbols["EJECT"] = LV_SYMBOL_EJECT;
    symbols["LEFT"] = LV_SYMBOL_LEFT;
    symbols["RIGHT"] = LV_SYMBOL_RIGHT;
    symbols["PLUS"] = LV_SYMBOL_PLUS;
    symbols["MINUS"] = LV_SYMBOL_MINUS;
    symbols["EYE_OPEN"] = LV_SYMBOL_EYE_OPEN;
    symbols["EYE_CLOSE"] = LV_SYMBOL_EYE_CLOSE;
    symbols["WARNING"] = LV_SYMBOL_WARNING;
    symbols["SHUFFLE"] = LV_SYMBOL_SHUFFLE;
    symbols["UP"] = LV_SYMBOL_UP;
    symbols["DOWN"] = LV_SYMBOL_DOWN;
    symbols["LOOP"] = LV_SYMBOL_LOOP;
    symbols["DIRECTORY"] = LV_SYMBOL_DIRECTORY;
    symbols["UPLOAD"] = LV_SYMBOL_UPLOAD;
    symbols["CALL"] = LV_SYMBOL_CALL;
    symbols["CUT"] = LV_SYMBOL_CUT;
    symbols["COPY"] = LV_SYMBOL_COPY;
    symbols["SAVE"] = LV_SYMBOL_SAVE;
    symbols["BARS"] = LV_SYMBOL_BARS;
    symbols["ENVELOPE"] = LV_SYMBOL_ENVELOPE;
    symbols["CHARGE"] = LV_SYMBOL_CHARGE;
    symbols["PASTE"] = LV_SYMBOL_PASTE;
    symbols["BELL"] = LV_SYMBOL_BELL;
    symbols["KEYBOARD"] = LV_SYMBOL_KEYBOARD;
    symbols["GPS"] = LV_SYMBOL_GPS;
    symbols["FILE"] = LV_SYMBOL_FILE;
    symbols["WIFI"] = LV_SYMBOL_WIFI;
    symbols["BATTERY_FULL"] = LV_SYMBOL_BATTERY_FULL;
    symbols["BATTERY_3"] = LV_SYMBOL_BATTERY_3;
    symbols["BATTERY_2"] = LV_SYMBOL_BATTERY_2;
    symbols["BATTERY_1"] = LV_SYMBOL_BATTERY_1;
    symbols["BATTERY_EMPTY"] = LV_SYMBOL_BATTERY_EMPTY;
    symbols["USB"] = LV_SYMBOL_USB;
    symbols["BLUETOOTH"] = LV_SYMBOL_BLUETOOTH;
    symbols["TRASH"] = LV_SYMBOL_TRASH;
    symbols["EDIT"] = LV_SYMBOL_EDIT;
    symbols["BACKSPACE"] = LV_SYMBOL_BACKSPACE;
    symbols["SD_CARD"] = LV_SYMBOL_SD_CARD;
    symbols["NEW_LINE"] = LV_SYMBOL_NEW_LINE;
    symbols["DUMMY"] = LV_SYMBOL_DUMMY;

    return symbols;
}

const char* uUI::getSymbolCode(const char* key)
{
    return getSymbolsDictionary()[key];
}

lv_obj_t *uUI::getObjectById(lv_obj_t *parent, const char *id)
{
    uint32_t cnt = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (!child) continue;

        char *ud = (char *)lv_obj_get_user_data(child);
        if (ud) {
            cJsonPtr json = cJsonParse(ud);
            if (json && cJSON_IsObject(json.get())) {
                const char *childId = uJSON::getString(json.get(), "id");
                bool match = childId && strcmp(childId, id) == 0;
                if (match) return child;
            }
        } else {
            // Container without user_data (e.g. columnsRow, mCols) — recurse
            lv_obj_t *found = getObjectById(child, id);
            if (found) return found;
        }
    }

    return nullptr;
}

void uUI::attachJsonData(lv_obj_t* obj, cJSON* json)
{
    cJsonStrPtr s = cJsonPrint(json);
    if (!s) { ESP_LOGE("uUI", "attachJsonData: cJSON_PrintUnformatted OOM"); return; }
    lv_obj_set_user_data(obj, s.release());
    lv_obj_add_event_cb(obj, [](lv_event_t* e) {
        void* data = lv_obj_get_user_data(lv_event_get_target_obj(e));
        if (data) free(data);
    }, LV_EVENT_DELETE, nullptr);
}

cJSON *uUI::getObjectDataJSON(lv_obj_t *object)
{
    if (object == nullptr) {
        ESP_LOGE("uUI", "Object target is null");

        return nullptr;
    }

    char* objectData = (char*)lv_obj_get_user_data(object);

     if (objectData == nullptr) {
        ESP_LOGD("uUI", "Object data is null");

        return nullptr;
    }

    cJsonPtr json = cJsonParse(objectData);

    if (!json || !cJSON_IsObject(json.get())) {
        ESP_LOGE("uUI", "Wrong object data: %s", objectData);

        return nullptr;
    }

    return json.release();
}

