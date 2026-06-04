#include "profiles.h"
#include "macros.h"
#include "widgets.h"

#include "./constants.h"
#include "./ui/ui.h"
#include "./utils/memory.h"
#include "./utils/json.h"
#include "../../utils/heap_probe.h"
#include "../../utils/settings.h"

#include <esp_heap_caps.h>

using namespace Cards;

static char*       _cachedProfilesJson = nullptr;  // PSRAM-backed JSON string
static std::string _activeProfileId    = "";

// ── Update the profile button label to reflect the active profile ─────────────

static void rebuildProfileButton()
{
    if (!ui_ddProfile) return;

    // The button's label is its first child (created in ui_screenMain.c)
    lv_obj_t* lbl = lv_obj_get_child(ui_ddProfile, 0);
    if (!lbl) return;

    if (_activeProfileId.empty()) {
        lv_label_set_text(lbl, "All");
        return;
    }

    cJsonPtr list = _cachedProfilesJson ? cJsonParse(_cachedProfilesJson) : cJsonPtr(nullptr, cJSON_Delete);
    bool found = false;
    if (list && cJSON_IsArray(list.get())) {
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, list.get()) {
            const char* pid  = uJSON::getString(item, "id");
            const char* name = uJSON::getString(item, "name", "?");
            if (pid && _activeProfileId == pid) {
                lv_label_set_text(lbl, name);
                found = true;
                break;
            }
        }
    }
    if (!found) lv_label_set_text(lbl, "All");
}

// ── Cache a JSON string to PSRAM ──────────────────────────────────────────────

static void cacheJson(const char* jsonStr)
{
    size_t len = strlen(jsonStr) + 1;
    free(_cachedProfilesJson);
    _cachedProfilesJson = (char*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_cachedProfilesJson) _cachedProfilesJson = (char*)malloc(len);
    if (_cachedProfilesJson) memcpy(_cachedProfilesJson, jsonStr, len);
}

// ── Public API ────────────────────────────────────────────────────────────────

void Profiles::init()
{
    HeapProbe _probe("profiles.load");

    uMemory::read(MEMORY_PROFILES_KEY, [](Preferences prefs) {
        String s = prefs.isKey("items") ? prefs.getString("items") : "[]";
        cacheJson(s.c_str());
    });

    uMemory::read(MEMORY_SETTINGS_KEY, [](Preferences prefs) {
        String active = prefs.isKey("active_profile") ? prefs.getString("active_profile") : "";
        _activeProfileId = active.c_str();
    });

    // Set Macros/Widgets scene roots from the saved profile so that when their
    // JSON loads (in WSServer::start later in setup), renderForScene() starts
    // at the correct scene.  We do NOT write NVS here — we just read it above.
    {
        cJsonPtr list = _cachedProfilesJson ? cJsonParse(_cachedProfilesJson) : cJsonPtr(nullptr, cJSON_Delete);
        if (list && !_activeProfileId.empty()) {
            cJSON* item = NULL;
            cJSON_ArrayForEach(item, list.get()) {
                const char* pid = uJSON::getString(item, "id");
                if (pid && _activeProfileId == pid) {
                    const char* macroScene  = uJSON::getString(item, "macro_scene",  "");
                    const char* widgetScene = uJSON::getString(item, "widget_scene", "");
                    Macros::setActiveProfileId(_activeProfileId.c_str());
                    Widgets::setActiveProfileId(_activeProfileId.c_str());
                    Macros::setRootScene(macroScene  && macroScene[0]  ? macroScene  : "");
                    Widgets::setRootScene(widgetScene && widgetScene[0] ? widgetScene : "");
                    break;
                }
            }
        }
    }

    rebuildProfileButton();
}

void Profiles::apply(const char* profileId)
{
    if (!profileId) profileId = "";
    _activeProfileId = profileId;

    // ── 1. NVS save — no LVGL mutex needed, must happen BEFORE acquiring it ──
    // NVS erase+write can take 5-50 ms; holding the LVGL mutex during that time
    // blocks lv_timer_handler() and visibly freezes the display.
    uMemory::write(MEMORY_SETTINGS_KEY, [profileId](Preferences prefs) {
        prefs.putString("active_profile", profileId);
    });

    // ── 2. Resolve scene IDs from profile list (pure CPU, no LVGL) ────────────
    std::string macroScene, widgetScene;
    bool found = false;
    cJsonPtr list = _cachedProfilesJson ? cJsonParse(_cachedProfilesJson) : cJsonPtr(nullptr, cJSON_Delete);
    if (list && cJSON_IsArray(list.get())) {
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, list.get()) {
            const char* pid = uJSON::getString(item, "id");
            if (pid && _activeProfileId == pid) {
                const char* ms = uJSON::getString(item, "macro_scene",  "");
                const char* ws = uJSON::getString(item, "widget_scene", "");
                macroScene  = (ms && ms[0]) ? ms : "";
                widgetScene = (ws && ws[0]) ? ws : "";
                found = true;
                break;
            }
        }
    }

    // ── 3. Navigate tabs — each setRootScene manages its own LVGL mutex ───────
    // Do NOT wrap these with an outer _ui_enable_mutex: the bool-based mutex is
    // not reentrant, so a nested enable/disable pair inside renderForScene would
    // prematurely release any outer lock, leaving subsequent LVGL calls unguarded.
    Macros::setActiveProfileId(profileId);
    Widgets::setActiveProfileId(profileId);
    Macros::setRootScene(found ? macroScene.c_str()  : "");
    Widgets::setRootScene(found ? widgetScene.c_str() : "");

    // ── 4. Update the profile button label (LVGL) ─────────────────────────────
    _ui_enable_mutex(1);
    rebuildProfileButton();
    _ui_disable_mutex(1);
}

cJSON* Profiles::getList()
{
    return _cachedProfilesJson ? cJsonParse(_cachedProfilesJson).release() : cJsonCreateArray().release();
}

void Profiles::set(cJSON* list)
{
    cJsonStrPtr s = cJsonPrint(list);
    if (!s) { log_e("profiles.set: cJSON_PrintUnformatted failed"); return; }

    uMemory::write(MEMORY_PROFILES_KEY, [&s](Preferences prefs) {
        prefs.putString("items", s.get());
    });

    cacheJson(s.get());

    rebuildProfileButton();
    uSettings::bumpStateVersion();
}

std::string Profiles::getActiveId()
{
    return _activeProfileId;
}

void Profiles::setActive(const char* id)
{
    _activeProfileId = id ? id : "";
    uMemory::write(MEMORY_SETTINGS_KEY, [id](Preferences prefs) {
        prefs.putString("active_profile", id ? id : "");
    });
    uSettings::bumpStateVersion();
}
