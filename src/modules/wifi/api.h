#ifndef __WIFI_API_H__
#define __WIFI_API_H__

#include <Arduino.h>
#include <cJSON.h>

namespace WiFiModule {
    class API {
    public:
        static bool handleMessage(uint32_t clientId, const char* msg);

        static bool pong(uint32_t clientId, const char* reqId = nullptr);
        static bool onPing(uint32_t clientId, cJSON *json, const char* reqId = nullptr);

        static bool onGet(uint32_t clientId, cJSON *json, const char* reqId = nullptr);
        static bool onSet(uint32_t clientId, cJSON *json);
        static bool onSave(uint32_t clientId, cJSON *json, const char* reqId);
        static bool onDelete(uint32_t clientId, cJSON *json, const char* reqId);
        static bool onReboot(uint32_t clientId, const char* reqId);

        static bool setMacros(uint32_t clientId);
        static bool setWidgets(uint32_t clientId);
        static bool setSettings(uint32_t clientId);
        static bool setProfiles(uint32_t clientId);
        static bool setInfo(uint32_t clientId);
        static bool setWifiStatus(uint32_t clientId, const char* reqId = nullptr);

        // Parse fields are passed as raw strings (zero-alloc: built into static
        // buffer via snprintf). jsonKeys is a cJSON array borrowed from caller
        // — serialized via cJSON_PrintPreallocated, never duplicated.
        static bool getUrl(const char* objectType, const char* objectId, const char* url,
                           cJSON* headers = nullptr,
                           const char* parseType     = nullptr,
                           const char* parseTemplate = nullptr,
                           const char* parseRegex    = nullptr,
                           cJSON*      jsonKeys      = nullptr);
        static bool onReturnUrlResponse(uint32_t clientId, cJSON *json);
        static void clearProxyInflight();

        static bool getSystemInfo(uint32_t clientId, const char* objectType, const char* objectId, const char* metricId);
        static bool onReturnSystemInfo(uint32_t clientId, cJSON *json);

        static bool onCompanionHello(uint32_t clientId, cJSON *json);

    private:
        // Attach reqId to a reply object if present (no-op if nullptr/empty).
        static void attachReqId(cJSON* reply, const char* reqId);
        static void sendSaveAck(uint32_t clientId, const char* reqId, bool ok, const char* error);
        static bool requireServiceMode(uint32_t clientId, const char* reqId);
    };
}

#endif // __WIFI_API_H__
