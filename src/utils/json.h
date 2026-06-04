#ifndef __JSON_H__
#define __JSON_H__

#include <cJSON.h>
#include <WString.h>
#include <memory>
#include <cstdlib>

// ── cJSON RAII ─────────────────────────────────────────────────────────────
// unique_ptr with cJSON_Delete deleter. Owns a parsed or created cJSON tree;
// frees on scope exit. Transfer ownership with std::move or .release().
using cJsonPtr    = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

// unique_ptr for strings returned by cJSON_PrintUnformatted (malloc'd; free'd).
using cJsonStrPtr = std::unique_ptr<char,  decltype(&free)>;

inline cJsonPtr cJsonParse(const char* str) {
    return cJsonPtr(cJSON_Parse(str), cJSON_Delete);
}
inline cJsonPtr cJsonCreateObject() {
    return cJsonPtr(cJSON_CreateObject(), cJSON_Delete);
}
inline cJsonPtr cJsonCreateArray() {
    return cJsonPtr(cJSON_CreateArray(), cJSON_Delete);
}
inline cJsonStrPtr cJsonPrint(const cJSON* node) {
    return cJsonStrPtr(cJSON_PrintUnformatted(node), free);
}
inline cJsonPtr cJsonCreateString(const char* str) {
    return cJsonPtr(cJSON_CreateString(str), cJSON_Delete);
}

// Adds `child` into `parent[key]`, transferring ownership to the parent tree.
// On success, child becomes empty (release()'d). On failure (null args or
// parent not an object), returns false and child retains ownership.
inline bool cJsonAdoptInto(cJSON* parent, const char* key, cJsonPtr&& child) {
    if (!parent || !child || !cJSON_IsObject(parent)) return false;
    cJSON* raw = child.release();
    cJSON_AddItemToObject(parent, key, raw);
    return true;
}

// Adds `child` into `parent[]` array, transferring ownership.
inline bool cJsonAdoptIntoArray(cJSON* parent, cJsonPtr&& child) {
    if (!parent || !child || !cJSON_IsArray(parent)) return false;
    cJSON* raw = child.release();
    cJSON_AddItemToArray(parent, raw);
    return true;
}

class uJSON {
public:
    static const char *getFromString(const char *jsonString, const char *key, const char *defaultValue);
    static int getFromString(const char *jsonString, const char *key, int defaultValue);
    static double getFromString(const char *jsonString, const char *key, double defaultValue);

    static cJSON *get(cJSON *json, const char *key);
    static const char *getString(cJSON *json, const char *key, const char *defaultValue = "");
    // Copies the value of json[key] into `out`. Returns true on success, false
    // on null / missing key / wrong type (in which case `out` is set to `fallback`).
    // Safe: does not return a pointer into the cJSON tree.
    static bool getStringCopy(const cJSON* json, const char* key, String& out, const char* fallback);
    static int getInt(cJSON *json, const char *key, int defaultValue = 0);
    static double getDouble(cJSON *json, const char *key, double defaultValue = 0.0);
};

#endif // __JSON_H__
