#ifndef __CARDS_MACROS_H__
#define __CARDS_MACROS_H__

#include <cJSON.h>
#include <lvgl.h>
#include <string>

namespace Cards {
    class Macros {
    public:
        static void init();
        static void set(cJSON *list, bool isSave);
        static void renderForScene(const char* sceneId);
        static void navigateToScene(const char* sceneId);
        static void navigateBack();        // go to profile-defined root (or "" if none)
        static void setRootScene(const char* sceneId);  // called by Profiles::apply()
        static void setActiveProfileId(const char* profileId); // called by Profiles::apply()
        static void rerender();  // re-render current scene (e.g. after theme change)
        static bool showPopupMsg();
        static bool sendDataToUrl(const char* macrosId, const char* url);
        static bool sendDataToUrlBySystem(const char* macrosId, const char* url);

        // ── Toggle state ───────────────────────────────────────────────────────
        // DEPRECATED: unsafe across tasks — HTTP callers must use getCachedJsonCopy().
        static const char* getCachedJson();
        // Thread-safe snapshot: copies cache under mutex. Returns "[]" if empty.
        static std::string getCachedJsonCopy();
        static bool getToggleState(const char* id);
        static void setToggleState(const char* id, bool val);
        static void applyToggleBtnColor(lv_obj_t* btn, cJSON* json, bool state);
    };
}

#endif // __CARDS_MACROS_H__
