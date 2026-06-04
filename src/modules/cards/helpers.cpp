#include "helpers.h"

#include <HTTPClient.h>
#ifdef BOARD_HAS_PSRAM
#include <regex.h>
#endif

#include "../../utils/json.h"

#include "../wifi/server.h"
#include "../wifi/api.h"

using namespace Cards; 

bool Helpers::updateDataFromUrl(const char* url, cJSON* headers, UpdateDataCallback callback)
{
    bool state;

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(2000); // TCP SYN timeout (default ~10 s)
    http.setTimeout(4000);
    http.begin(url);

    if (headers) {
        cJSON* header = nullptr;
        cJSON_ArrayForEach(header, headers) {
            const char* key = cJSON_GetStringValue(cJSON_GetObjectItem(header, "key"));
            const char* val = cJSON_GetStringValue(cJSON_GetObjectItem(header, "value"));
            if (key && val) http.addHeader(key, val);
        }
    }

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        state = callback(httpCode, http.getString().c_str());
    } else {
        state = callback(httpCode, http.getString().c_str());
    }

    http.end();

    return state;
}

bool Helpers::updateDataFromUrlBySystem(const char* objectType, const char* widgetId, const char* url,
                                        cJSON* headers,
                                        const char* parseType, const char* parseTemplate,
                                        const char* parseRegex, cJSON* jsonKeys)
{
    WiFiModule::API::getUrl(objectType, widgetId, url, headers,
                            parseType, parseTemplate, parseRegex, jsonKeys);

    return true;
}

// Traverse a dot-notation path ("memory.used") through a cJSON object.
static cJSON* jsonGetPath(cJSON* root, const char* path)
{
    char buf[128];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    cJSON* node = root;
    char* token = strtok(buf, ".");
    while (token && node) {
        if (cJSON_IsArray(node)) {
            char* end;
            long idx = strtol(token, &end, 10);
            node = (*end == '\0') ? cJSON_GetArrayItem(node, (int)idx)
                                  : cJSON_GetObjectItem(node, token);
        } else {
            node = cJSON_GetObjectItem(node, token);
        }
        token = strtok(nullptr, ".");
    }
    return node;
}

std::string Helpers::formatJsonResponse(const char* tmplt, const cJSON* keysArray, const char* response)
{
    if (!tmplt || !keysArray || !response) return "";

    cJsonPtr json = cJsonParse(response);
    if (!json) {
        ESP_LOGE("Helpers", "formatJsonResponse: invalid JSON response");
        return "";
    }

    std::string content(tmplt);

    const cJSON* keyItem = nullptr;
    cJSON_ArrayForEach(keyItem, keysArray) {
        if (!cJSON_IsString(keyItem)) continue;
        const char* key = keyItem->valuestring;

        cJSON* value = jsonGetPath(json.get(), key);
        if (!value) {
            ESP_LOGW("Helpers", "jsonGetPath: key '%s' not found in response", key);
            continue;
        }

        // Build placeholder: "{key}"
        char placeholder[130];
        snprintf(placeholder, sizeof(placeholder), "{%s}", key);

        // Serialise value to string
        char numBuf[32];
        const char* valueStr = nullptr;
        if (cJSON_IsString(value)) {
            valueStr = value->valuestring;
        } else if (cJSON_IsNumber(value)) {
            if (value->valuedouble == (double)(long long)value->valuedouble)
                snprintf(numBuf, sizeof(numBuf), "%lld", (long long)value->valuedouble);
            else
                snprintf(numBuf, sizeof(numBuf), "%.2f", value->valuedouble);
            valueStr = numBuf;
        } else if (cJSON_IsBool(value)) {
            valueStr = cJSON_IsTrue(value) ? "true" : "false";
        }

        if (valueStr) {
            size_t pos = content.find(placeholder);
            while (pos != std::string::npos) {
                content.replace(pos, strlen(placeholder), valueStr);
                pos = content.find(placeholder, pos + strlen(valueStr));
            }
        }
    }

    return content;
}

std::string Helpers::formatUrlResponse(const char* tmplt, const char* regexp, const char* response)
{
    if (!tmplt || !regexp || !response) return "";

#ifdef BOARD_HAS_PSRAM
    regex_t re;
    if (regcomp(&re, regexp, REG_EXTENDED) != 0) {
        ESP_LOGE("Helpers", "formatUrlResponse: bad regexp: %s", regexp);
        return "";
    }

    const int MAX_GROUPS = 10;
    regmatch_t pm[MAX_GROUPS];

    int ret = regexec(&re, response, MAX_GROUPS, pm, 0);
    regfree(&re);

    if (ret != 0) return "";

    std::string content(tmplt);
    for (int i = 1; i < MAX_GROUPS; ++i) {
        if (pm[i].rm_so < 0) break;  // no more capture groups

        char placeholder[10];
        snprintf(placeholder, sizeof(placeholder), "{%d}", i);

        size_t pos = content.find(placeholder);
        if (pos != std::string::npos) {
            std::string matched(response + pm[i].rm_so, (size_t)(pm[i].rm_eo - pm[i].rm_so));
            content.replace(pos, strlen(placeholder), matched);
        }
    }

    return content;
#else
    // No-PSRAM: newlib regex pulls ~6.6KB BSS (collate tables) we can't fit.
    // Fallback: substitute {1} with full response, ignore regex.
    std::string content(tmplt);
    size_t pos = content.find("{1}");
    if (pos != std::string::npos) content.replace(pos, 3, response);
    return content;
#endif
}