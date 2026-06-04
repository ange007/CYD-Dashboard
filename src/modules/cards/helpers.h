#ifndef __CARDS_HELPERS_H__
#define __CARDS_HELPERS_H__

#include <Arduino.h>

#include <string>
#include <cstdio>
#include <cJSON.h>

namespace Cards {
    typedef std::function<bool(int code, const char *response)> UpdateDataCallback;

    class Helpers {
        public:
            static bool updateDataFromUrl(const char* url, cJSON* headers, UpdateDataCallback callback);
            static bool updateDataFromUrlBySystem(const char* objectType, const char* objectId, const char* url,
                                                  cJSON*      headers       = nullptr,
                                                  const char* parseType     = nullptr,
                                                  const char* parseTemplate = nullptr,
                                                  const char* parseRegex    = nullptr,
                                                  cJSON*      jsonKeys      = nullptr);

            // Regex mode: {1}, {2} placeholders → capture groups
            static std::string formatUrlResponse(const char* tmplt, const char* regexp, const char* response);

            // JSON mode: {keyname} placeholders → JSON values (dot-notation supported)
            static std::string formatJsonResponse(const char* tmplt, const cJSON* keysArray, const char* response);
    };
}

#endif // __CARDS_HELPERS_H__