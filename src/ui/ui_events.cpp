#include <cJSON.h>
#include <string>

#include "../constants.h"
#include "ui.h"

#include "../modules/cards/macros.h"
#include "../modules/cards/widgets.h"
#include "../modules/cards/profiles.h"

#include "../modules/wifi/core.h"
#include "../modules/wifi/helpers.h"
#include "../modules/wifi/server.h"
#include "../modules/wifi/ui.h"

#include "../modules/hid/keyboard.h"

#include "../utils/memory.h"
#include "../utils/settings.h"
#include "../utils/ui.h"
#include "../utils/json.h"

static constexpr int MAX_MACRO_KEYS = 100;

void OnWidgetClicked(lv_event_t * e)
{
    lv_obj_t * widget = lv_event_get_target_obj(e);
    if (widget == nullptr) {
        ESP_LOGE("UI_EVENTS", "Event target is null");
        return;
    }

    Cards::Widgets::update(widget);
}

void OnWidgetLongPressed(lv_event_t * e)
{
    lv_obj_t * widget = lv_event_get_target_obj(e);
    if (widget == nullptr) return;
    Cards::Widgets::counterReset(widget);
}

void OnMacrosClicked(lv_event_t * e)
{
    static uint32_t _last_macro_ms = 0;
    uint32_t _now = millis();
    if (_now - _last_macro_ms < 400) return;
    _last_macro_ms = _now;

	lv_obj_t * btn = lv_event_get_target_obj(e);
    if (btn == nullptr) {
        ESP_LOGE("UI_EVENTS", "Event target is null");
        return;
    }

    char* objectData = (char*)lv_obj_get_user_data(btn);
    ESP_LOGI("UI_EVENTS", "obj.data: %s", objectData);

    cJsonPtr json(uUI::getObjectDataJSON(btn), cJSON_Delete);
    if (!json) {
        ESP_LOGE("UI_EVENTS", "Wrong macros data!");
        return;
    }

    const char* macrosId = uJSON::getString(json.get(), "id");
    const char* macrosType = uJSON::getString(json.get(), "type");

    // ── Keys helper (shared by MACROS_TYPE_KEYS and MACROS_TYPE_MULTI) ──────────
    auto processKeysArray = [&](cJSON* keys) {
        if (!HidKeyboard::isConnected()) {
            ESP_LOGE("UI_EVENTS", "HID not connected!");
            return;
        }
        if (!cJSON_IsArray(keys)) return;
        int keyIdx = 0;
        cJSON* key = NULL;
        cJSON_ArrayForEach(key, keys) {
            if (++keyIdx > MAX_MACRO_KEYS) {
                ESP_LOGW("UI_EVENTS", "Key array exceeds MAX_MACRO_KEYS=%d — truncating", MAX_MACRO_KEYS);
                break;
            }
            if (!key->valuestring) continue;
            // strdup: strsep() writes '\0' into the string it tokenises.
            // key->valuestring points directly into the cJSON tree — modifying
            // it in-place corrupts the JSON structure for any subsequent access.
            char* keyStr = strdup(key->valuestring);
            if (!keyStr) continue;
            char* keyStrOrig = keyStr;  // keep original pointer for free()

            ESP_LOGI("UI_EVENTS", "line: %s", keyStr);
            if (strchr(keyStr, '+')) {
                char* token = NULL;
                while ((token = strsep(&keyStr, "+"))) {
                    HidKeyboard::press(token);
                }
                HidKeyboard::releaseAll();
            } else if (strncmp(keyStr, MEDIA_KEY_PREFIX, 10) == 0) {
                HidKeyboard::press(keyStr); delay(10); HidKeyboard::releaseAll();
            } else if (strncmp(keyStr, KEY_PREFIX, 4) == 0) {
                HidKeyboard::press(keyStr); delay(10); HidKeyboard::releaseAll();
            } else if (strncmp(keyStr, "p:", 2) == 0) {
                strsep(&keyStr, ":");
                // Same freeze caveat as multi-action pause; cap at 3 s.
                int pause = constrain(atoi(keyStr ? keyStr : "0"), 0, 3);
                free(keyStrOrig);
                delay(pause * 1000);
                continue;
            } else {
                HidKeyboard::print(keyStr);
            }
            free(keyStrOrig);
        }
    };

    // Scene navigation — handle before any action processing
    if (strcmp(macrosType, MACROS_TYPE_BACK) == 0) {
        Cards::Macros::navigateBack();
        goto end;
    }
    if (strcmp(macrosType, MACROS_TYPE_SCENE) == 0) {
        // If target_id is set this is a shortcut navigator → go to target_id,
        // otherwise the button IS the scene folder → navigate by its own id.
        const char* targetId = uJSON::getString(json.get(), "target_id");
        const char* dest = (targetId && targetId[0] != '\0') ? targetId : macrosId;
        Cards::Macros::navigateToScene(dest ? dest : "");
        goto end;
    }

    if (strcmp(macrosType, MACROS_TYPE_KEYS) == 0) {
        if (!HidKeyboard::isConnected()) {
            ESP_LOGE("UI_EVENTS", "HID not connected!");
            goto end;
        }
        cJSON* keys = cJSON_GetObjectItem(json.get(), "keys");
        if (!cJSON_IsArray(keys)) { ESP_LOGE("UI_EVENTS", "Wrong keys list!"); goto end; }
        processKeysArray(keys);

    } else if (strcmp(macrosType, MACROS_TYPE_COMMAND) == 0) {

    } else if (strcmp(macrosType, MACROS_TYPE_URL) == 0) {
        const char* url = uJSON::getString(json.get(), "url");
        Cards::Macros::sendDataToUrl(macrosId, url);

    } else if (strcmp(macrosType, MACROS_TYPE_ACTION) == 0) {
        const char* actionId = uJSON::getString(json.get(), "action_id");
        if (actionId) {
            if (strcmp(actionId, "restart_device") == 0) {
                ESP.restart();
            } else if (strcmp(actionId, "sleep_display") == 0) {
                uSettings::displayBrightness(0);
            } else if (strcmp(actionId, "wake_display") == 0) {
                uSettings::displayBrightness(uSettings::getBrightness());
            } else {
                ESP_LOGW("UI_EVENTS", "Unknown action_id: %s", actionId);
            }
        }

    } else if (strcmp(macrosType, MACROS_TYPE_TOGGLE) == 0) {
        bool current = Cards::Macros::getToggleState(macrosId);
        bool next = !current;
        Cards::Macros::setToggleState(macrosId, next);
        const char* url = next ? uJSON::getString(json.get(), "on_url") : uJSON::getString(json.get(), "off_url");
        if (url && url[0]) Cards::Macros::sendDataToUrl(macrosId, url);
        Cards::Macros::applyToggleBtnColor(lv_event_get_target_obj(e), json.get(), next);

    } else if (strcmp(macrosType, MACROS_TYPE_MULTI) == 0) {
        cJSON* actions = cJSON_GetObjectItem(json.get(), "actions");
        if (cJSON_IsArray(actions)) {
            cJSON* act = NULL;
            cJSON_ArrayForEach(act, actions) {
                const char* aType = uJSON::getString(act, "type");
                const char* aVal  = uJSON::getString(act, "value");
                if (!aType) continue;
                if (strcmp(aType, "keys") == 0) {
                    // aVal is a single key string, wrap in temp array
                    if (aVal) {
                        cJsonPtr tmp = cJsonCreateArray();
                        cJsonAdoptIntoArray(tmp.get(), cJsonCreateString(aVal));
                        processKeysArray(tmp.get());
                    }
                } else if (strcmp(aType, "text") == 0) {
                    if (aVal && HidKeyboard::isConnected()) HidKeyboard::print((char*)aVal);
                } else if (strcmp(aType, "pause") == 0) {
                    int ms = uJSON::getInt(act, "ms", 1000);
                    // NOTE: delay() in Arduino-ESP32 internally calls vTaskDelay(),
                    // so BLE and AsyncTCP tasks continue to run during this pause.
                    // However lv_timer_handler() is NOT called (re-entrant call from
                    // inside an LVGL event callback is a no-op in LVGL 9), so the
                    // display freezes.  Cap at 3 s to limit freeze duration; a proper
                    // non-blocking implementation would need a state machine.
                    delay(constrain(ms, 0, 3000));
                } else if (strcmp(aType, "url") == 0) {
                    if (aVal) Cards::Macros::sendDataToUrl(macrosId, aVal);
                }
            }
        }
    }

    // Push to the polling log buffer (browser polls GET /api/log).
    {
        cJsonPtr log = cJsonCreateObject();
        cJSON_AddStringToObject(log.get(), "action",  "macro_pressed");
        cJSON_AddStringToObject(log.get(), "id",      macrosId    ? macrosId    : "");
        cJSON_AddStringToObject(log.get(), "type",    macrosType  ? macrosType  : "");
        WiFiModule::WSServer::pushLogEvent(std::move(log));
    }

    end:;
}

void OnScreenRotateChange(lv_event_t * e)
{
    uint16_t index = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    uSettings::setScreenRotate(index);
    uSettings::applyProfileLabelVisibility(index);
    // Display rotation changes ui_cntWidgets / ui_cntMacros dimensions —
    // existing widget cards keep absolute pixel heights and flex layout
    // recalculates with new container width. Defer rerender to next LVGL
    // idle so layout has settled before reading container dimensions.
    Cards::Macros::rerender();
    Cards::Widgets::rerenderDeferred();
    WiFiModule::WSServer::broadcastSettingInt("screen_rotate", index);
}

void OnReverseColorSwitchChange(lv_event_t * e)
{
    // Dropdown selection directly maps to theme index: 0=Light, 1=Dark, 2=Ocean, 3=Warm
    int theme = (int)lv_dropdown_get_selected(lv_event_get_target_obj(e));
    uSettings::setTheme(theme);
    uSettings::applyTheme(theme);
    Cards::Macros::rerender();
    // Theme propagation is async — style invalidations queue up in LVGL.
    // Reading container dimensions in renderForScene() right now returns
    // pre-theme values → masonry calc + widget heights end up wrong (visual
    // "flatten" on theme switch). Defer to next LVGL idle.
    Cards::Widgets::rerenderDeferred();
    WiFiModule::WSServer::broadcastSettingInt("theme", theme);
}

void OnCardSizeChange(lv_event_t * e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    if (obj == nullptr) return;

    // Dropdown re-purposed: selects macro column count (2..6 = idx 0..4).
    uint16_t index = lv_dropdown_get_selected(obj);
    int cols = (int)index + 2;
    if (cols < 2) cols = 2;
    if (cols > 6) cols = 6;

    uSettings::setMacroCols(cols);
    Cards::Macros::rerender();

    WiFiModule::WSServer::broadcastSettingInt("macro_cols", cols);
}

void OnWifiChangeState(lv_event_t * e)
{
	WiFiModule::State wifiState = WiFiModule::Helper::getState();

    if (wifiState == WiFiModule::State::Connected) {
        WiFiModule::Helper::disconnect();
    } else if(wifiState == WiFiModule::State::Disconnected) {
        char ssid[WIFI_SSID_BUFFER_SIZE];
        lv_dropdown_get_selected_str(ui_ddWifiSSID, ssid, WIFI_SSID_BUFFER_SIZE);

        const char *pass     = lv_textarea_get_text(ui_txtWifiPass);
        const char *hostname = lv_textarea_get_text(ui_txtWifiHostname);

        // Save hostname before connect() so MDNS.begin() picks up the new value
        if (hostname && strlen(hostname) > 0) {
            uSettings::setMdnsHost(hostname);
        }

        if (WiFiModule::Helper::connect(ssid, pass)) {

        }
    }
}

void OnWifiScan(lv_event_t * e)
{
    WiFiModule::Helper::changeScanState(WiFiModule::ScanState::Scanned);

	if (WiFiModule::Helper::scanSSID()) {
        WiFiModule::UI::setSSIDList(WiFiModule::Helper::getSSIDList());
        WiFiModule::Helper::changeScanState(WiFiModule::ScanState::None);
    } else {
        WiFiModule::Helper::changeScanState(WiFiModule::ScanState::ScanError);
    }
}

void OnProfileBtnClicked(lv_event_t * e)
{
    // Create a semi-transparent full-screen overlay on the top layer
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Tap outside the dialog closes the popup
    lv_obj_add_event_cb(overlay, [](lv_event_t* ev) {
        lv_obj_del_async(lv_event_get_target_obj(ev));
    }, LV_EVENT_CLICKED, NULL);

    // Dialog panel
    lv_obj_t* dialog = lv_obj_create(overlay);
    lv_obj_set_width(dialog, 200);
    lv_obj_set_height(dialog, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(dialog, lv_disp_get_ver_res(NULL) - 80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(dialog);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(dialog, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(dialog, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(dialog, LV_SCROLLBAR_MODE_AUTO);

    // Prevent taps on the dialog from closing the overlay
    lv_obj_add_event_cb(dialog, [](lv_event_t* ev) {
        lv_event_stop_bubbling(ev);
    }, LV_EVENT_CLICKED, NULL);

    const std::string activeId = Cards::Profiles::getActiveId();

    // Helper lambda: add one profile button to the dialog
    auto addProfileBtn = [&](const char* label, const char* profileId, bool isActive) {
        lv_obj_t* btn = lv_btn_create(dialog);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 40);
        lv_obj_set_style_radius(btn, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        if (isActive) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x1448A3), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_pad_left(lbl, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(lbl);

        char* idCopy = strdup(profileId ? profileId : "");
        lv_obj_set_user_data(btn, idCopy);

        lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
            lv_obj_t* b  = lv_event_get_target_obj(ev);
            char* id     = (char*)lv_obj_get_user_data(b);
            char  idBuf[64] = {};
            if (id) strncpy(idBuf, id, sizeof(idBuf) - 1);
            lv_obj_t* dlg = lv_obj_get_parent(b);
            lv_obj_t* ovl = lv_obj_get_parent(dlg);
            lv_obj_del_async(ovl);
            Cards::Profiles::apply(idBuf);
        }, LV_EVENT_CLICKED, NULL);

        lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
            void* data = lv_obj_get_user_data(lv_event_get_target_obj(ev));
            if (data) free(data);
        }, LV_EVENT_DELETE, NULL);
    };

    // "All" entry
    addProfileBtn("All", "", activeId.empty());

    // One button per profile
    cJsonPtr list(Cards::Profiles::getList(), cJSON_Delete);
    if (list) {
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, list.get()) {
            const char* pid  = uJSON::getString(item, "id");
            const char* name = uJSON::getString(item, "name", "?");
            if (!pid) continue;
            addProfileBtn(name, pid, activeId == pid);
        }
    }
}

void OnBrightnessChange(lv_event_t * e) {
    lv_event_code_t code  = lv_event_get_code(e);
    int             value = (int)lv_slider_get_value(lv_event_get_target_obj(e));

    // Live preview on every move — no NVS write (avoids flash wear from rapid drags)
    uSettings::displayBrightness(value);

    // Save to NVS + broadcast only when the user releases the slider
    if (code == LV_EVENT_RELEASED) {
        uSettings::setBrightness(value);
        WiFiModule::WSServer::broadcastSettingInt("brightness", value);
    }
}

void ui_event_swBluetooth(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    // Skip if value matches cache — fires during boot when load() syncs switch
    // state via lv_obj_add_state (which triggers value_changed).
    if (on == uSettings::getBluetoothEnabled()) return;
    uSettings::setBluetoothEnabled(on);
    HidKeyboard::applyBluetoothEnabled(on);
    WiFiModule::WSServer::broadcastSettingInt("bt_en", on ? 1 : 0);
}