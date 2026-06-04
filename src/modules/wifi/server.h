#pragma once
#ifndef __WIFI_SERVER_H__
#define __WIFI_SERVER_H__

#include <cJSON.h>
#include "server_base.h"

namespace WiFiModule
{

// Static facade — callers (api.cpp, etc.) use only these static methods.
// The real work is delegated to a WebServerBase* _impl chosen at start().
class WSServer
{
public:
    static void start();
    static void loop();

    static bool sendCommandToClient(uint32_t clientId, const char *jsonStr);
    static bool sendCommandToClient(uint32_t clientId, cJsonPtr&& json);
    static bool sendBinaryToClient(uint32_t clientId, const uint8_t* buf, size_t len);
    static bool sendCommand(const char *json);
    static bool sendCommand(cJsonPtr&& json);

    // Convenience: sends {"action":"settings_update", "<key>": <value>}
    static void broadcastSettingInt(const char *key, int value);

    // Send to a single proxy client (used for get_url to avoid duplicate responses)
    static bool sendCommandToProxy(cJsonPtr&& json);
    static uint32_t getProxyClientId();
    static void clearProxyClientId();

    // Companion app client (desktop/system info provider)
    static uint32_t getCompanionClientId();
    static void setCompanionClientId(uint32_t id);
    static void triggerWidgetsRefresh();
    static void requestProfileSwitch(const char *profileId);
    static void applySettingsResult(const uSettings::SettingsApplyResult& r);
    static void triggerSceneBgReload();
    static void stageWifiConnect(const char* ssid, const char* pass);

    // Returns true if at least one WebSocket client is currently connected.
    static bool hasClients();

    // Returns true if a static web file was served in the last 5 seconds.
    static bool isWebServing();

    // Service mode — full web UI on demand, BLE + widgets suspended.
    static bool     isServiceMode();
    static bool        enterServiceMode(uint32_t clientId, const char* token = nullptr);
    static void        exitServiceMode();
    static uint32_t    getServiceModeClientId();
    static const char* getServiceModeToken();

    // Mark widget/macros state as edited DURING the current service-mode session.
    // Called by Cards::Macros::set / Cards::Widgets::set when isSave=true and
    // we're currently in service mode. exitServiceMode reads this flag to
    // decide whether to do the expensive full widget rebuild (lv_obj_clean +
    // recreate) or the cheap timer-only re-arm. See _widgets_dirty_in_svc.
    static void markWidgetsDirty();

    // Append a structured event to the ring buffer served by GET /api/log.
    static void pushLogEvent(cJsonPtr&& json);
    static void pushLogEvent(const char* jsonStr);

private:
    static WebServerBase *_impl;
};

} // namespace WiFiModule

#endif // __WIFI_SERVER_H__
