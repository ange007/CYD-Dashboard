
#include "api.h"
#include "server.h"
#include "server_base.h"
#include "../hid/keyboard.h"
#include "net_diag.h"

#include <esp_system.h>
#include <esp_heap_caps.h>

#include "../../constants.h"
#include "../../ui/ui.h"
#include "../../utils/memory.h"
#include "../../utils/json.h"
#include "../../utils/sd.h"

#include "../cards/macros.h"
#include "../cards/widgets.h"
#include "../cards/profiles.h"

#include "../../utils/settings.h"
#include "../../utils/backgrounds.h"
#include "../../utils/icons.h"

#include "helpers.h"
#include <WiFi.h>

#include <map>
#include <string>
#include <Arduino.h>

namespace WiFiModule {

    void API::attachReqId(cJSON* reply, const char* reqId) {
        if (!reply || !reqId || !*reqId) return;
        cJSON_AddStringToObject(reply, "req_id", reqId);
    }

    void API::sendSaveAck(uint32_t clientId, const char* reqId, bool ok, const char* error)
    {
        cJsonPtr ack = cJsonCreateObject();
        if (!ack) return;
        cJSON_AddStringToObject(ack.get(), "action", "save_ack");
        cJSON_AddBoolToObject(ack.get(), "ok", ok);
        if (!ok && error && *error)
            cJSON_AddStringToObject(ack.get(), "error", error);
        attachReqId(ack.get(), reqId);
        WSServer::sendCommandToClient(clientId, std::move(ack));
    }

    bool API::requireServiceMode(uint32_t clientId, const char* reqId)
    {
        if (WSServer::isServiceMode() && WSServer::getServiceModeClientId() == clientId)
            return true;
        sendSaveAck(clientId, reqId, false, "not_in_service_mode");
        return false;
    }

    // ── Proxy get_url backpressure ───────────────────────────────────────────
    // Limits concurrent in-flight proxy requests (WS frame pending, browser
    // fetching, no response yet). Prevents a boot-time burst of N widgets
    // from piling up WS frames that the browser cannot drain fast enough.
    //
    // Max = 6: browsers cap at 6 concurrent HTTP/1.1 connections per origin,
    // so additional parallel requests queue browser-side anyway. Keeping the
    // ESP-side cap aligned avoids RAM growth in the WS send queue.
    //
    // Per-widget dedup: if the same widgetId is already in flight, skip the
    // new request instead of piling up duplicates. Stale entries (>15 s)
    // are auto-cleared so a lost response can't block a widget forever.
    static constexpr uint32_t PROXY_MAX_INFLIGHT   = 2;
    static constexpr uint32_t PROXY_STALE_TIMEOUT  = 15000;   // ms
    static std::map<std::string, uint32_t> _proxyInflight;   // widgetId -> startMs

    static void proxyPurgeStale()
    {
        uint32_t now = millis();
        for (auto it = _proxyInflight.begin(); it != _proxyInflight.end(); ) {
            if ((uint32_t)(now - it->second) > PROXY_STALE_TIMEOUT) {
                it = _proxyInflight.erase(it);
            } else { ++it; }
        }
    }

    void API::clearProxyInflight()
    {
        if (!_proxyInflight.empty()) {
            ESP_LOGD("API", "Proxy disconnected — clearing %u in-flight entries", (unsigned)_proxyInflight.size());
            _proxyInflight.clear();
        }
    }
    /**
     * Handles the incoming message from the client and performs actions based on the action type specified in the message.
     *
     * @param clientId The unique identifier of the client sending the message.
     * @param msg The message received from the client in JSON format.
     *
     * @return A boolean indicating the success of handling the message.
     */
    bool API::handleMessage(uint32_t clientId, const char* msg)
    {
        size_t msgLen = strlen(msg);
        if (msgLen > 256) {
            size_t need  = msgLen + msgLen / 4 + 256;
            size_t avail = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            if (avail < need) {
                bool isUrlResp = (strstr(msg, "\"return_url_response\"") != nullptr);
                ESP_LOGW("API", "%s (%u B) dropped: heap largest=%u < need=%u — add parse rules to reduce response size",
                         isUrlResp ? "return_url_response" : "WS message",
                         (unsigned)msgLen, (unsigned)avail, (unsigned)need);
                return false;
            }
        }

        bool state = false;
        cJsonPtr json = cJsonParse(msg);

        if (!json) {
            ESP_LOGE("API", "Failed to parse JSON");
            return false;
        }

        const char *action = uJSON::getString(json.get(), "action");
        const char *reqId  = uJSON::getString(json.get(), "req_id");
        ESP_LOGD("API", "Handle message. Action: %s req_id: %s", action, reqId ? reqId : "-");

        if (strcmp(action, "set") == 0) {
            state = onSet(clientId, json.get());
        } else if (strcmp(action, "get") == 0) {
            state = onGet(clientId, json.get(), reqId);
        } else if (strcmp(action, "return_system_info") == 0) {
            state = onReturnSystemInfo(clientId, json.get());
        } else if (strcmp(action, "return_url_response") == 0) {
            state = onReturnUrlResponse(clientId, json.get());
        } else if (strcmp(action, "ping") == 0) {
            state = onPing(clientId, json.get(), reqId);
        } else if (strcmp(action, "companion_hello") == 0) {
            state = onCompanionHello(clientId, json.get());
        } else if (strcmp(action, "switch_profile") == 0) {
            const char *id = uJSON::getString(json.get(), "id");
            WSServer::requestProfileSwitch(id ? id : "");
            state = true;
        } else if (strcmp(action, "enter_service_mode") == 0) {
            // token (optional): existing session token for re-claim on WS reconnect.
            // Browser stores it in sessionStorage after first enter and replays it
            // when the WS drops and reconnects, so service mode survives reloads.
            const char* token = uJSON::getString(json.get(), "token");
            if (WSServer::enterServiceMode(clientId, token)) {
                cJsonPtr resp = cJsonCreateObject();
                if (resp) {
                    cJSON_AddStringToObject(resp.get(), "action", "service_mode_entered");
                    const char* t = WSServer::getServiceModeToken();
                    if (t && *t) cJSON_AddStringToObject(resp.get(), "token", t);
                    attachReqId(resp.get(), reqId);
                    WSServer::sendCommandToClient(clientId, std::move(resp));
                }
            } else {
                cJsonPtr resp = cJsonCreateObject();
                if (resp) {
                    cJSON_AddStringToObject(resp.get(), "action", "service_mode_denied");
                    cJSON_AddStringToObject(resp.get(), "reason", "another_session_active");
                    cJSON_AddNumberToObject(resp.get(), "owner",
                        (double)WSServer::getServiceModeClientId());
                    attachReqId(resp.get(), reqId);
                    WSServer::sendCommandToClient(clientId, std::move(resp));
                }
            }
            state = true;
        } else if (strcmp(action, "exit_service_mode") == 0) {
            if (WSServer::isServiceMode()) {
                uint32_t owner = WSServer::getServiceModeClientId();
                if (owner == 0 || owner == clientId)
                    WSServer::exitServiceMode();
            }
            state = true;
        } else if (strcmp(action, "force_enter_service_mode") == 0) {
            // User explicitly takes over a session held by another client
            // (e.g. the previous browser tab crashed without releasing, or
            // a second device wants control). Exits current session cleanly
            // (triggering broadcast so the old owner redirects to proxy),
            // then re-enters fresh for this clientId with a new token.
            ESP_LOGW("SVC", "Force takeover by client %u (old owner=%u)",
                     clientId, (unsigned)WSServer::getServiceModeClientId());
            WSServer::exitServiceMode();
            if (WSServer::enterServiceMode(clientId, nullptr)) {
                cJsonPtr resp = cJsonCreateObject();
                if (resp) {
                    cJSON_AddStringToObject(resp.get(), "action", "service_mode_entered");
                    const char* t = WSServer::getServiceModeToken();
                    if (t && *t) cJSON_AddStringToObject(resp.get(), "token", t);
                    attachReqId(resp.get(), reqId);
                    WSServer::sendCommandToClient(clientId, std::move(resp));
                }
            }
            state = true;
        } else if (strcmp(action, "save") == 0) {
            state = onSave(clientId, json.get(), reqId);
        } else if (strcmp(action, "delete") == 0) {
            state = onDelete(clientId, json.get(), reqId);
        } else if (strcmp(action, "reboot") == 0) {
            state = onReboot(clientId, reqId);
        } else {
            ESP_LOGE("API", "Unknown action or empty: %s", action);
        }

        return state;
    }

    /**
     * Sends a "pong" message to the client.
     * @example {action:"pong"}
     *
     * @param clientId
     * @param reqId Optional request id to echo back.
     *
     * @return boolean
     */
    bool API::pong(uint32_t clientId, const char* reqId)
    {
        cJsonPtr commandJson = cJsonCreateObject();
        if (!commandJson) return false;
        cJsonAdoptInto(commandJson.get(), "action", cJsonCreateString("pong"));
        attachReqId(commandJson.get(), reqId);
        return WSServer::sendCommandToClient(clientId, std::move(commandJson));
    }

    /**
     * Handles the "ping" action for the API.
     * @example {action:"ping"}
     *
     * @param clientId
     * @param json The JSON object containing the request.
     * @param reqId Optional request id to echo back.
     *
     * @return boolean
     */
    bool API::onPing(uint32_t clientId, cJSON *json, const char* reqId)
    {
        return pong(clientId, reqId);
    }

    /**
     * Sends requested data to the client over WebSocket.
     * @example {action:"get"}                  — legacy: macros + widgets
     * @example {action:"get", what:"init"}     — full init payload (as multiple frames)
     * @example {action:"get", what:"settings"} — settings only
     * @example {action:"get", what:"profiles"} — profiles only
     * @example {action:"get", what:"macros"}   — macros only
     * @example {action:"get", what:"widgets"}  — widgets only
     *
     * Init is sent as a sequence of small frames (set_macros, set_widgets,
     * set_settings, set_profiles, set_info) rather than one monolithic payload.
     * Small frames avoid a 16 KB DRAM alloc for cJSON_PrintUnformatted and
     * preserve back-compat with existing set_macros / set_widgets handlers.
     *
     * @param clientId
     * @param json The JSON object containing the request.
     * @param reqId Optional request id to echo back in the init_ack terminator.
     *
     * @return boolean
     */
    bool API::onGet(uint32_t clientId, cJSON *json, const char* reqId)
    {
        const char* what = uJSON::getString(json, "what");
        if (!what || !*what) {
            setMacros(clientId);
            setWidgets(clientId);
            return true;
        }

        if (strcmp(what, "init") == 0) {
            // PsychicHttp's sendMessage is async via httpd_queue_work → UDP ctrl
            // socket with mailbox size 6. Sending 6 init frames + the 3
            // service_mode_* broadcasts back-to-back from the same handler
            // overflows the mailbox and the last 2-3 frames silently drop.
            // Bundle everything into ONE init_ack message → one mailbox slot.
            cJsonPtr ack = cJsonCreateObject();
            if (ack) {
                cJSON_AddStringToObject(ack.get(), "action", "init_ack");
                cJSON_AddBoolToObject(ack.get(), "ok", true);
                attachReqId(ack.get(), reqId);
                {
                    std::string snap = Cards::Macros::getCachedJsonCopy();
                    cJsonPtr arr = cJsonParse(snap.c_str());
                    cJSON_AddItemToObject(ack.get(), "macros",
                        arr ? arr.release() : cJSON_CreateArray());
                }
                {
                    std::string snap = Cards::Widgets::getCachedJsonCopy();
                    cJsonPtr arr = cJsonParse(snap.c_str());
                    cJSON_AddItemToObject(ack.get(), "widgets",
                        arr ? arr.release() : cJSON_CreateArray());
                }
                {
                    cJsonPtr s = WebServerBase::buildSettingsJson();
                    cJSON_AddItemToObject(ack.get(), "settings",
                        s ? s.release() : cJSON_CreateObject());
                }
                {
                    cJsonPtr list(Cards::Profiles::getList(), cJSON_Delete);
                    cJSON_AddItemToObject(ack.get(), "profiles",
                        list ? list.release() : cJSON_CreateArray());
                    cJSON_AddStringToObject(ack.get(), "active_profile",
                        Cards::Profiles::getActiveId().c_str());
                }
                cJSON_AddBoolToObject(ack.get(), "sd_available", uSD::isAvailable());
                cJSON* disp = cJSON_AddObjectToObject(ack.get(), "display");
#if defined(DISPLAY_WIDTH) && defined(DISPLAY_HEIGHT)
                cJSON_AddNumberToObject(disp, "w", DISPLAY_WIDTH);
                cJSON_AddNumberToObject(disp, "h", DISPLAY_HEIGHT);
#else
                cJSON_AddNumberToObject(disp, "w", 320);
                cJSON_AddNumberToObject(disp, "h", 240);
#endif
                WSServer::sendCommandToClient(clientId, std::move(ack));
            }
            return true;
        }
        if (strcmp(what, "macros")      == 0) return setMacros(clientId);
        if (strcmp(what, "widgets")     == 0) return setWidgets(clientId);
        if (strcmp(what, "settings")    == 0) return setSettings(clientId);
        if (strcmp(what, "profiles")    == 0) return setProfiles(clientId);
        if (strcmp(what, "info")        == 0) return setInfo(clientId);
        if (strcmp(what, "wifi_status") == 0) return setWifiStatus(clientId, reqId);
        if (strcmp(what, "diag") == 0) {
            String diagJson = NetDiag::toJson();
            cJsonPtr ack = cJsonParse(diagJson.c_str());
            if (!ack) return false;
            cJSON_AddStringToObject(ack.get(), "action", "diag_ack");
            attachReqId(ack.get(), reqId);
            return WSServer::sendCommandToClient(clientId, std::move(ack));
        }

        ESP_LOGW("API", "onGet: unknown what=%s", what);
        return false;
    }

    /**
     * Handles the "set" action for the API, sets macros and widgets based on the provided JSON data.
     * @example {action:"set", macros:"[]", widgets:"[]"}
     *
     * @param clientId
     * @param json the JSON data containing macros and widgets
     *
     * @return boolean
     */
    bool API::onSet(uint32_t clientId, cJSON *json)
    {
        // const char *profile = uJSON::getString(json, "profile");

        cJSON *macrosList = cJSON_GetObjectItem(json, "macros");
        Cards::Macros::set(macrosList, true);

        cJSON *widgetList = cJSON_GetObjectItem(json, "widgets");
        Cards::Widgets::set(widgetList, true);

        return true;
    }

    // Notify every connected client that a given data domain was mutated
    // (macros/widgets/profiles/settings/scene_bg/…). Clients use this to
    // invalidate their cached /api/init result and re-fetch fresh data.
    // Only a tiny envelope is sent; the client decides how to refresh so
    // we don't broadcast potentially large item lists.
    static void broadcastStateChanged(const char* what)
    {
        if (!what || !*what) return;
        cJsonPtr msg = cJsonCreateObject();
        if (!msg) return;
        cJSON_AddStringToObject(msg.get(), "action", "state_changed");
        cJSON_AddStringToObject(msg.get(), "what",   what);
        WSServer::sendCommand(std::move(msg));
    }

    bool API::onSave(uint32_t clientId, cJSON *json, const char* reqId)
    {
        if (!requireServiceMode(clientId, reqId)) return true;

        const char* what = uJSON::getString(json, "what");
        if (!what || !*what) { sendSaveAck(clientId, reqId, false, "what_required"); return true; }

        if (strcmp(what, "macros") == 0) {
            cJSON* items = cJSON_GetObjectItem(json, "items");
            if (!items || !cJSON_IsArray(items)) { sendSaveAck(clientId, reqId, false, "invalid_json"); return true; }
            Cards::Macros::set(items, true);
            sendSaveAck(clientId, reqId, true, nullptr);
            broadcastStateChanged("macros");
            return true;
        }
        if (strcmp(what, "widgets") == 0) {
            cJSON* items = cJSON_GetObjectItem(json, "items");
            if (!items || !cJSON_IsArray(items)) { sendSaveAck(clientId, reqId, false, "invalid_json"); return true; }
            Cards::Widgets::set(items, true);
            sendSaveAck(clientId, reqId, true, nullptr);
            broadcastStateChanged("widgets");
            return true;
        }

        if (strcmp(what, "settings") == 0) {
            cJSON* items = cJSON_GetObjectItem(json, "items");
            if (!items || !cJSON_IsObject(items)) { sendSaveAck(clientId, reqId, false, "invalid_json"); return true; }
            auto r = uSettings::applyFromJson(items);
            WSServer::applySettingsResult(r);
            if (r.update) WSServer::sendCommand(cJsonPtr(r.update, cJSON_Delete));
            sendSaveAck(clientId, reqId, true, nullptr);
            broadcastStateChanged("settings");
            return true;
        }
        if (strcmp(what, "profiles") == 0) {
            cJSON* items = cJSON_GetObjectItem(json, "items");
            if (!items || !cJSON_IsArray(items)) { sendSaveAck(clientId, reqId, false, "invalid_json"); return true; }
            Cards::Profiles::set(items);
            sendSaveAck(clientId, reqId, true, nullptr);
            broadcastStateChanged("profiles");
            return true;
        }
        if (strcmp(what, "active_profile") == 0) {
            const char* id = uJSON::getString(json, "id");
            if (!id || !*id) { sendSaveAck(clientId, reqId, false, "id_required"); return true; }
            WSServer::requestProfileSwitch(id);
            sendSaveAck(clientId, reqId, true, nullptr);
            return true;
        }
        if (strcmp(what, "scene_bg") == 0) {
            const char* sceneId = uJSON::getString(json, "scene_id");
            const char* image   = uJSON::getString(json, "image");
            if (!sceneId || !*sceneId) { sendSaveAck(clientId, reqId, false, "scene_id_required"); return true; }
            String err;
            String sid(sceneId), img(image ? image : "");
            if (!uBackgroundManager::setSceneBg(sid, img, err)) {
                sendSaveAck(clientId, reqId, false, err.c_str());
                return true;
            }
            WSServer::triggerSceneBgReload();
            sendSaveAck(clientId, reqId, true, nullptr);
            return true;
        }

        if (strcmp(what, "import") == 0) {
            cJSON* payload = cJSON_GetObjectItem(json, "payload");
            if (!payload || !cJSON_IsObject(payload)) { sendSaveAck(clientId, reqId, false, "invalid_json"); return true; }
            cJSON *m = cJSON_GetObjectItem(payload, "macros");
            if (m && cJSON_IsArray(m)) { Cards::Macros::set(m, true); broadcastStateChanged("macros"); }
            cJSON *w = cJSON_GetObjectItem(payload, "widgets");
            if (w && cJSON_IsArray(w)) { Cards::Widgets::set(w, true); broadcastStateChanged("widgets"); }
            cJSON *s = cJSON_GetObjectItem(payload, "settings");
            if (s && cJSON_IsObject(s)) {
                auto r = uSettings::applyFromJson(s);
                WSServer::applySettingsResult(r);
                if (r.update) WSServer::sendCommand(cJsonPtr(r.update, cJSON_Delete));
            }
            cJSON *p = cJSON_GetObjectItem(payload, "profiles");
            if (p && cJSON_IsArray(p)) { Cards::Profiles::set(p); broadcastStateChanged("profiles"); }
            cJSON *ap = cJSON_GetObjectItem(payload, "active_profile");
            if (cJSON_IsString(ap) && ap->valuestring) Cards::Profiles::setActive(ap->valuestring);
            sendSaveAck(clientId, reqId, true, nullptr);
            return true;
        }
        if (strcmp(what, "wifi") == 0) {
            const char* ssid = uJSON::getString(json, "ssid");
            const char* pass = uJSON::getString(json, "pass");
            if (!ssid || !*ssid) { sendSaveAck(clientId, reqId, false, "ssid_pass_required"); return true; }
            WSServer::stageWifiConnect(ssid, pass ? pass : "");
            sendSaveAck(clientId, reqId, true, nullptr);
            return true;
        }

        sendSaveAck(clientId, reqId, false, "unknown_what");
        return true;
    }

    bool API::onDelete(uint32_t clientId, cJSON *json, const char* reqId)
    {
        if (!requireServiceMode(clientId, reqId)) return true;

        const char* what = uJSON::getString(json, "what");
        if (!what || !*what) { sendSaveAck(clientId, reqId, false, "what_required"); return true; }

        if (strcmp(what, "icon") == 0) {
            const char* name = uJSON::getString(json, "name");
            if (!name || !*name) { sendSaveAck(clientId, reqId, false, "name_required"); return true; }
            String err;
            if (!uIconManager::deleteIcon(String(name), err)) {
                sendSaveAck(clientId, reqId, false, err.c_str());
                return true;
            }
            sendSaveAck(clientId, reqId, true, nullptr);
            return true;
        }
        if (strcmp(what, "background") == 0) {
            const char* name = uJSON::getString(json, "name");
            if (!name || !*name) { sendSaveAck(clientId, reqId, false, "name_required"); return true; }
            String err;
            if (!uBackgroundManager::deleteBackground(String(name), err)) {
                sendSaveAck(clientId, reqId, false, err.c_str());
                return true;
            }
            sendSaveAck(clientId, reqId, true, nullptr);
            return true;
        }

        sendSaveAck(clientId, reqId, false, "unknown_what");
        return true;
    }

    bool API::onReboot(uint32_t clientId, const char* reqId)
    {
        if (!requireServiceMode(clientId, reqId)) return true;
        sendSaveAck(clientId, reqId, true, nullptr);
        delay(300);
        esp_restart();
        return true;  // never reached
    }

    /**
     * Send macros to client
     * @example {action:"set_macros", items:"[]"}
     *
     * @param clientId
     *
     * @return boolean
     */
    bool API::setMacros(uint32_t clientId)
    {
        // IMPORTANT: macros live in LittleFS (primary) and the PSRAM cache
        // kept in sync by Cards::Macros::set(). The legacy NVS "macros"/"items"
        // key is no longer updated after a save, so reading from NVS here
        // returns stale data — every SPA save would "come back" on the next
        // get:init because the client received the pre-migration snapshot.
        std::string snap = Cards::Macros::getCachedJsonCopy();

        cJsonPtr commandJson = cJsonCreateObject();
        if (!commandJson) return false;
        cJsonAdoptInto(commandJson.get(), "action", cJsonCreateString("set_macros"));
        cJsonAdoptInto(commandJson.get(), "items", cJsonPtr(cJSON_CreateRaw(snap.c_str()), cJSON_Delete));

        WSServer::sendCommandToClient(clientId, std::move(commandJson));
        return true;
    }

    /**
     * Send widgets to client
     * @example {action:"set_widgets", items:"[]"}
     *
     * @param clientId
     *
     * @return boolean
     */
    bool API::setWidgets(uint32_t clientId)
    {
        // Same rationale as setMacros — read from the authoritative in-memory
        // cache (kept in sync with LittleFS), NOT from legacy NVS.
        std::string snap = Cards::Widgets::getCachedJsonCopy();

        cJsonPtr commandJson = cJsonCreateObject();
        if (!commandJson) return false;
        cJsonAdoptInto(commandJson.get(), "action", cJsonCreateString("set_widgets"));
        cJsonAdoptInto(commandJson.get(), "items", cJsonPtr(cJSON_CreateRaw(snap.c_str()), cJSON_Delete));

        WSServer::sendCommandToClient(clientId, std::move(commandJson));
        return true;
    }

    /**
     * Send current device settings to client.
     * @example {action:"set_settings", items:{...}}
     */
    bool API::setSettings(uint32_t clientId)
    {
        cJsonPtr settings = WebServerBase::buildSettingsJson();
        if (!settings) return false;

        cJsonPtr cmd = cJsonCreateObject();
        if (!cmd) return false;
        cJsonAdoptInto(cmd.get(), "action", cJsonCreateString("set_settings"));
        cJsonAdoptInto(cmd.get(), "items",  std::move(settings));

        return WSServer::sendCommandToClient(clientId, std::move(cmd));
    }

    /**
     * Send profile list + active profile id to client.
     * @example {action:"set_profiles", items:[...], active_id:"..."}
     */
    bool API::setProfiles(uint32_t clientId)
    {
        cJsonPtr cmd = cJsonCreateObject();
        if (!cmd) return false;
        cJsonAdoptInto(cmd.get(), "action", cJsonCreateString("set_profiles"));
        cJsonPtr list(Cards::Profiles::getList(), cJSON_Delete);
        cJsonAdoptInto(cmd.get(), "items", list ? std::move(list) : cJsonCreateArray());
        cJSON_AddStringToObject(cmd.get(), "active_id", Cards::Profiles::getActiveId().c_str());

        return WSServer::sendCommandToClient(clientId, std::move(cmd));
    }

    /**
     * Send device meta info (display dims + SD availability) to client.
     * @example {action:"set_info", display:{w,h}, sd_available:bool}
     */
    bool API::setInfo(uint32_t clientId)
    {
        cJsonPtr cmd = cJsonCreateObject();
        if (!cmd) return false;
        cJsonAdoptInto(cmd.get(), "action", cJsonCreateString("set_info"));
        cJSON_AddBoolToObject(cmd.get(), "sd_available", uSD::isAvailable());
        cJSON *disp = cJSON_AddObjectToObject(cmd.get(), "display");
#if defined(DISPLAY_WIDTH) && defined(DISPLAY_HEIGHT)
        cJSON_AddNumberToObject(disp, "w", DISPLAY_WIDTH);
        cJSON_AddNumberToObject(disp, "h", DISPLAY_HEIGHT);
#else
        cJSON_AddNumberToObject(disp, "w", 320);
        cJSON_AddNumberToObject(disp, "h", 240);
#endif
        return WSServer::sendCommandToClient(clientId, std::move(cmd));
    }

    bool API::setWifiStatus(uint32_t clientId, const char* reqId)
    {
        cJsonPtr cmd = cJsonCreateObject();
        if (!cmd) return false;
        cJSON_AddStringToObject(cmd.get(), "action", "set_wifi_status");
        cJSON_AddNumberToObject(cmd.get(), "state", (int)Helper::getState());
        if (Helper::getState() == State::Connected) {
            cJSON_AddStringToObject(cmd.get(), "ip",   WiFi.localIP().toString().c_str());
            cJSON_AddStringToObject(cmd.get(), "ssid", WiFi.SSID().c_str());
        }
        cJSON_AddStringToObject(cmd.get(), "ap_ip", Helper::getAPIP().c_str());
        attachReqId(cmd.get(), reqId);
        return WSServer::sendCommandToClient(clientId, std::move(cmd));
    }

    /**
     * Sends a request to retrieve a URL response.
     * @example {action:"get_url", target:"widget", id:"{11111-2222}", url:"https://google.com"}
     *
     * @param objectType @example widget | macros
     * @param objectId @example {11111-2222}
     * @param url @example https://google.com
     *
     * @return boolean
     */
    bool API::getUrl(const char* objectType, const char* objectId, const char* url, cJSON* headers,
                     const char* parseType, const char* parseTemplate, const char* parseRegex, cJSON* jsonKeys)
    {
        if (url == nullptr || strlen(url) == 0) {
            ESP_LOGE("API", "URL is empty");
            return false;
        }

        // ── Backpressure + dedup (widget target only) ────────────────────
        // Checked BEFORE building the JSON to avoid wasted allocations on skipped requests.
        if (objectType && strcmp(objectType, "widget") == 0 && objectId && *objectId) {
            proxyPurgeStale();
            std::string key(objectId);
            auto it = _proxyInflight.find(key);
            if (it != _proxyInflight.end()) {
                ESP_LOGD("API", "Proxy get_url dedup — skip %s (already in flight)", objectId);
                return true; // not an error — widget already has a request outstanding
            }
            if (_proxyInflight.size() >= PROXY_MAX_INFLIGHT) {
                ESP_LOGW("API", "Proxy in-flight cap (%u) reached — defer %s",
                         (unsigned)PROXY_MAX_INFLIGHT, objectId);
                return false;
            }
        }

        // Determine target before doing any work — bail early if nobody can handle it.
        uint32_t companionId = WSServer::getCompanionClientId();
        uint32_t proxyId     = WSServer::getProxyClientId();
        uint32_t targetId    = (companionId > 0) ? companionId : proxyId;
        if (targetId == 0) {
            ESP_LOGD("API", "No companion or proxy — skipping get_url for %s", objectId);
            return false;
        }

        // ── Build JSON into a static buffer — zero per-call heap allocations ─────
        // Replaces the cJSON-tree build + cJSON_PrintUnformatted pattern which
        // created 15-20 small heap blocks per call and fragmented DRAM on no-PSRAM
        // boards (observed: largest_free collapses to ~2 KB after ~20 min).
        // Buffer allocated once at first call and reused forever.
        static constexpr size_t BUF_SIZE = 1536;
        static char* buf = nullptr;
        if (!buf) {
            buf = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!buf) return false;
        }

        int n = snprintf(buf, BUF_SIZE - 4,
            "{\"action\":\"get_url\",\"target\":\"%s\",\"id\":\"%s\",\"url\":\"",
            objectType, objectId);

        // JSON-escape the URL (backslash and quote are the only chars requiring it).
        for (const char* p = url; *p && n < (int)BUF_SIZE - 6; p++) {
            if (*p == '"' || *p == '\\') buf[n++] = '\\';
            buf[n++] = *p;
        }
        buf[n++] = '"';

        if (headers && cJSON_GetArraySize(headers) > 0 && n + 14 < (int)BUF_SIZE) {
            memcpy(buf + n, ",\"headers\":", 11); n += 11;
            int avail = (int)BUF_SIZE - n - 4;
            if (cJSON_PrintPreallocated(headers, buf + n, avail, false))
                n += (int)strlen(buf + n);
            else
                n -= 11; // rollback prefix if headers didn't fit
        }

        // Build ,"parse":{...} inline from raw fields — zero alloc.
        // Replaces the prior cJSON-tree parseConfig that the caller used to
        // build via cJsonCreateObject + AddString * N + cJSON_Duplicate(jsonKeys).
        if (parseType && *parseType && n + 20 < (int)BUF_SIZE) {
            int saved = n;
            memcpy(buf + n, ",\"parse\":{\"type\":\"", 18); n += 18;
            // escape type
            for (const char* p = parseType; *p && n < (int)BUF_SIZE - 8; p++) {
                if (*p == '"' || *p == '\\') buf[n++] = '\\';
                buf[n++] = *p;
            }
            buf[n++] = '"';
            if (parseTemplate && *parseTemplate && n + 14 < (int)BUF_SIZE) {
                memcpy(buf + n, ",\"template\":\"", 13); n += 13;
                for (const char* p = parseTemplate; *p && n < (int)BUF_SIZE - 8; p++) {
                    if (*p == '"' || *p == '\\') buf[n++] = '\\';
                    buf[n++] = *p;
                }
                buf[n++] = '"';
            }
            if (strcmp(parseType, "json") == 0 && jsonKeys && cJSON_GetArraySize(jsonKeys) > 0
                && n + 14 < (int)BUF_SIZE) {
                memcpy(buf + n, ",\"json_keys\":", 13); n += 13;
                int avail = (int)BUF_SIZE - n - 6;
                if (cJSON_PrintPreallocated(jsonKeys, buf + n, avail, false))
                    n += (int)strlen(buf + n);
                else { n = saved; goto parse_done; } // rollback whole block
            } else if (parseRegex && *parseRegex && n + 11 < (int)BUF_SIZE) {
                memcpy(buf + n, ",\"regex\":\"", 10); n += 10;
                for (const char* p = parseRegex; *p && n < (int)BUF_SIZE - 8; p++) {
                    if (*p == '"' || *p == '\\') buf[n++] = '\\';
                    buf[n++] = *p;
                }
                buf[n++] = '"';
            }
            buf[n++] = '}';
        }
        parse_done:;

        buf[n++] = '}';
        buf[n]   = '\0';

        // Mark in-flight after target confirmed, before send.
        if (objectType && strcmp(objectType, "widget") == 0 && objectId && *objectId) {
            _proxyInflight[std::string(objectId)] = millis();
        }

        bool sent = WSServer::sendCommandToClient(targetId, buf);
        if (!sent && objectType && strcmp(objectType, "widget") == 0 && objectId && *objectId) {
            _proxyInflight.erase(std::string(objectId));
        }
        return sent;
    }

    /**
     * Handles the response from a URL request.
     * @example {action:"return_url_response", target:"widget", id:"{11111-2222}", code:200, response:"..."}
     *
     * @param clientId
     * @param json The JSON object containing the response.
     *
     * @return boolean
     */
    bool API::onReturnUrlResponse(uint32_t clientId, cJSON *json)
    {
        const char *target = uJSON::getString(json, "target");
        const char *id = uJSON::getString(json, "id");

        // Clear proxy in-flight tracking as soon as a response arrives — the
        // widget is free to fire again on its next timer cycle. Even on
        // errors / retries further down we still want the slot back.
        if (target && strcmp(target, "widget") == 0 && id && *id) {
            _proxyInflight.erase(std::string(id));
        }

        int code = uJSON::getInt(json, "code");
        const char *response   = uJSON::getString(json, "response");
        const char *fetch_err  = uJSON::getString(json, "fetch_error");  // set by browser on network error

        if (target == nullptr || id == nullptr) {
            ESP_LOGE("API", "Empty data in url response");
            return false;
        }

        // Browser fetch failed (typically CORS or network error) — try direct
        // HTTP on the ESP32 as a fallback instead of showing an error to the user.
        if (fetch_err && strlen(fetch_err) > 0) {
            ESP_LOGW("API", "Browser fetch error for %s/%s: %s — retrying direct",
                     target, id, fetch_err);
            const char *url = uJSON::getString(json, "url");
            if (url && strlen(url) > 0 && strcmp(target, "widget") == 0) {
                if (Cards::Widgets::retryDirectHttp(id, url))
                    return true;
                // retry not possible (HTTPS without direct-fallback flag) — fall through
                // so enqueueUrlResult runs with code=0 and the widget shows an error state
            }
        }

        if (response == nullptr) response = "";

        bool proxyApplied = cJSON_IsTrue(cJSON_GetObjectItem(json, "proxy_applied"));

        bool state = false;

        if (strcmp(target, "widget") == 0) {
            // Enqueue instead of calling renderUrlResult directly — this function
            // runs in the async_tcp task; direct LVGL calls from here race with
            // lv_timer_handler's layout loop in loopTask. The queue is drained
            // safely from Widgets::loop() in loopTask.
            Cards::Widgets::enqueueUrlResult(id, code, response, proxyApplied);
            state = true;
        } else {
            ESP_LOGE("API", "Unknown target for url response: %s", target);
        }

        return state;
    }

    /**
     * Sends a request to retrieve system information for a specific object.
     * @example {action:"get_system_info", target:"widget", id:"{11111-2222}", metric_id:"temperature"}
     *
     * @param clientId
     * @param objectType
     * @param objectId
     * @param metricId
     *
     * @return boolean
     */
    bool API::getSystemInfo(uint32_t clientId, const char* objectType, const char* objectId, const char* metricId)
    {
        cJsonPtr commandJson = cJsonCreateObject();
        if (!commandJson) return false;
        cJsonAdoptInto(commandJson.get(), "action",    cJsonCreateString("get_system_info"));
        cJsonAdoptInto(commandJson.get(), "target",    cJsonCreateString(objectType));
        cJsonAdoptInto(commandJson.get(), "id",        cJsonCreateString(objectId));
        cJsonAdoptInto(commandJson.get(), "metric_id", cJsonCreateString(metricId));

        // Only companion app can provide system metrics (CPU, RAM, etc.).
        // Browser cannot supply these — no fallback.
        uint32_t companionId = WSServer::getCompanionClientId();
        if (companionId == 0) {
            ESP_LOGD("API", "No companion — skipping get_system_info for %s", objectId);
            // commandJson destructor handles cleanup automatically
            return false;
        }
        return WSServer::sendCommandToClient(companionId, std::move(commandJson));
    }

    bool API::onCompanionHello(uint32_t clientId, cJSON *json)
    {
        const char *ver      = uJSON::getString(json, "version");
        const char *os       = uJSON::getString(json, "os");
        const char *hostname = uJSON::getString(json, "hostname");
        ESP_LOGI("API", "Companion connected: client #%u  v%s  %s@%s",
                 clientId,
                 ver      ? ver      : "?",
                 os       ? os       : "?",
                 hostname ? hostname : "?");
        WSServer::setCompanionClientId(clientId);

        // Check for hid_relay capability — activate companion HID backend if present
        bool hasHidRelay = false;
        cJSON* caps = cJSON_GetObjectItem(json, "capabilities");
        if (cJSON_IsArray(caps)) {
            cJSON* cap = nullptr;
            cJSON_ArrayForEach(cap, caps) {
                if (cJSON_IsString(cap) && strcmp(cap->valuestring, "hid_relay") == 0) {
                    hasHidRelay = true;
                    break;
                }
            }
        }
        if (hasHidRelay) {
            HidKeyboard::setCompanionHidReady(true, clientId);
            ESP_LOGI("API", "Companion #%u has hid_relay capability", clientId);
        }

        // The companion may have been mis-identified as the browser proxy
        // (it was the first WS client).  Clear the proxy slot so a real
        // browser tab can claim it later.
        if (WSServer::getProxyClientId() == clientId) {
            WSServer::clearProxyClientId();
        }
        // Defence-in-depth: if somehow companion got assigned as service
        // mode owner (e.g. from an earlier grace-transfer bug), exit now —
        // companion must never own service mode; it would hold it open
        // indefinitely and block widget refresh.
        if (WSServer::isServiceMode() && WSServer::getServiceModeClientId() == clientId) {
            ESP_LOGW("SVC", "Companion #%u was stale service-mode owner — forcing exit", clientId);
            WSServer::exitServiceMode();
        }
        // System widget timers are already running — they will route to the
        // companion on their next tick automatically.  No need to trigger a
        // full widget refresh here (it was causing race conditions where the
        // re-render made widgets temporarily invisible to timer callbacks).
        return pong(clientId);
    }

    /**
     * Handles the system information response and renders the data for a specific target.
     * @example {action:"return_system_info", target:"widget", id:"{11111-2222}", info:"..."}
     *
     * @param clientId The unique identifier of the client.
     * @param json The JSON object containing the system information response.
     *
     * @return boolean
     */
    bool API::onReturnSystemInfo(uint32_t clientId, cJSON *json)
    {
        const char *target = uJSON::getString(json, "target");
        const char *id = uJSON::getString(json, "id");
        const char *info = uJSON::getString(json, "info");

        if (target == nullptr || id == nullptr || info == nullptr) {
        ESP_LOGE("API", "Empty data in system info response");

        return false;
        }

        bool state = false;

        if (strcmp(target, "widget") == 0) {
            // Enqueue — same reasoning as enqueueUrlResult above.
            Cards::Widgets::enqueueData(id, info);
            state = true;
        } else {
            ESP_LOGE("API", "Unknown target for system info: %s", target);
        }

        return state;
    }
}
