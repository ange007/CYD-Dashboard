#include "json.h"

#include <Arduino.h>

// ── Static scratch for the const char* overload ────────────────────────────
// getFromString(str, key, const char*) returns a C string pointer. To avoid
// returning a dangling pointer into a freed cJSON tree, we copy the value
// into this static String and return its c_str(). Limitation: not reentrant.
// Callers that need to persist the string should copy it immediately.
static String _s_getFromStringScratch;

// ── Item lookup (unchanged) ───────────────────────────────────────────────
cJSON* uJSON::get(cJSON* json, const char* key)
{
    if (!cJSON_IsObject(json)) {
        return nullptr;
    }
    if (!cJSON_HasObjectItem(json, key)) {
        return nullptr;
    }
    return cJSON_GetObjectItem(json, key);
}

// ── Value accessors on an already-alive tree (unchanged contract) ─────────
const char* uJSON::getString(cJSON* json, const char* key, const char* defaultValue)
{
    cJSON* item = get(json, key);
    if (item == nullptr || !cJSON_IsString(item)) {
        return defaultValue;
    }
    return item->valuestring;
}

int uJSON::getInt(cJSON* json, const char* key, int defaultValue)
{
    cJSON* item = get(json, key);
    if (item == nullptr || !cJSON_IsNumber(item)) {
        return defaultValue;
    }
    return item->valueint;
}

double uJSON::getDouble(cJSON* json, const char* key, double defaultValue)
{
    cJSON* item = get(json, key);
    if (item == nullptr || !cJSON_IsNumber(item)) {
        return defaultValue;
    }
    return item->valuedouble;
}

// ── Safe value-copy: writes into caller-owned String ──────────────────────
bool uJSON::getStringCopy(const cJSON* json, const char* key, String& out, const char* fallback)
{
    // cJSON API is not const-correct — cast away const for the lookup helpers.
    cJSON* item = get(const_cast<cJSON*>(json), key);
    if (item == nullptr || !cJSON_IsString(item) || item->valuestring == nullptr) {
        out = (fallback != nullptr) ? fallback : "";
        return false;
    }
    out = item->valuestring;
    return true;
}

// ── Parse-then-lookup convenience functions (FIXED: no UAF) ───────────────
// These parse a string, extract one value, and return it as a primitive or
// a stable reference. The parsed tree is destroyed before return, so we
// must NOT return a pointer into it — we either return a primitive (int/
// double) or copy the string into the static scratch above.

const char* uJSON::getFromString(const char* jsonString, const char* key, const char* defaultValue)
{
    cJsonPtr root = cJsonParse(jsonString);
    if (!root) {
        _s_getFromStringScratch = (defaultValue != nullptr) ? defaultValue : "";
        return _s_getFromStringScratch.c_str();
    }
    cJSON* item = get(root.get(), key);
    if (item == nullptr || !cJSON_IsString(item) || item->valuestring == nullptr) {
        _s_getFromStringScratch = (defaultValue != nullptr) ? defaultValue : "";
        return _s_getFromStringScratch.c_str();
    }
    _s_getFromStringScratch = item->valuestring;   // COPY while tree alive
    // root destructs here, but _s_getFromStringScratch now owns the data.
    return _s_getFromStringScratch.c_str();
}

int uJSON::getFromString(const char* jsonString, const char* key, int defaultValue)
{
    cJsonPtr root = cJsonParse(jsonString);
    if (!root) return defaultValue;
    cJSON* item = get(root.get(), key);
    if (item == nullptr || !cJSON_IsNumber(item)) return defaultValue;
    return item->valueint;   // primitive — safe to return after root destructs
}

double uJSON::getFromString(const char* jsonString, const char* key, double defaultValue)
{
    cJsonPtr root = cJsonParse(jsonString);
    if (!root) return defaultValue;
    cJSON* item = get(root.get(), key);
    if (item == nullptr || !cJSON_IsNumber(item)) return defaultValue;
    return item->valuedouble;
}
