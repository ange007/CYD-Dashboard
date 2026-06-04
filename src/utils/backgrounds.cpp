#include "backgrounds.h"
#include "sd.h"
#include "memory.h"
#include "constants.h"
#include "json.h"
#include "settings.h"

String uBackgroundManager::listJson() {
    String out;
    if (!uSD::isAvailable()) return "[]";

    out.reserve(512);  // conservative estimate: ~4-8 background entries
    out += '[';
    File dir = uSD::getFS().open("/backgrounds");
    if (dir && dir.isDirectory()) {
        bool first = true;
        File f = dir.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                if (!first) out += ',';
                const char *n  = f.name();
                const char *sl = strrchr(n, '/');
                const char *fname = sl ? sl + 1 : n;
                out += "{\"name\":\"";
                out += fname;
                out += "\",\"size\":";
                out += f.size();
                out += '}';
                first = false;
            }
            f = dir.openNextFile();
        }
    }
    out += ']';
    return out;
}

bool uBackgroundManager::validateFilename(const String& name, String& errorOut) {
    if (name.indexOf('/') >= 0 || name.indexOf("..") >= 0) {
        errorOut = "invalid name";
        return false;
    }
    return true;
}

bool uBackgroundManager::deleteBackground(const String& name, String& errorOut) {
    if (!uSD::isAvailable()) { errorOut = "no SD card"; return false; }
    if (!validateFilename(name, errorOut)) return false;

    char path[128];
    snprintf(path, sizeof(path), "/backgrounds/%s", name.c_str());
    if (uSD::getFS().exists(path)) uSD::getFS().remove(path);
    return true;
}

String uBackgroundManager::getSceneBgsJson() {
    String out;
    uMemory::read(MEMORY_SETTINGS_KEY, [&out](Preferences prefs) {
        out = prefs.isKey("scene_bgs") ? prefs.getString("scene_bgs") : "{}";
    });
    return out;
}

bool uBackgroundManager::setSceneBg(const String& sceneId, const String& image, String& errorOut) {
    // Read existing map, update, write back (cannot nest read+write in same Preferences object).
    String existing;
    uMemory::read(MEMORY_SETTINGS_KEY, [&existing](Preferences prefs) {
        existing = prefs.isKey("scene_bgs") ? prefs.getString("scene_bgs") : "{}";
    });

    cJsonPtr map = cJsonParse(existing.c_str());
    if (!map) map = cJsonCreateObject();

    cJSON_DeleteItemFromObjectCaseSensitive(map.get(), sceneId.c_str());
    if (image.length() > 0) {
        cJSON_AddStringToObject(map.get(), sceneId.c_str(), image.c_str());
    }

    cJsonStrPtr mapStr = cJsonPrint(map.get());
    if (!mapStr) { errorOut = "json error"; return false; }

    uMemory::write(MEMORY_SETTINGS_KEY, [&mapStr](Preferences prefs) {
        prefs.putString("scene_bgs", mapStr.get());
    });

    // NOTE: Caller (server handler) must set _pending_scene_bg_reload = true
    // so that uSettings::reloadSceneBgs() runs on the main task, not HTTP task.
    uSettings::bumpStateVersion();
    return true;
}
