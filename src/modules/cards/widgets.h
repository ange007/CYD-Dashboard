#ifndef __CARDS_WIDGETS_H__
#define __CARDS_WIDGETS_H__

#include <cJSON.h>
#include <string>

#include "constants.h"
#include "ui/ui.h"

namespace Cards {
    class Widgets {
    public:
        static void init();
        static void loop();
        static void set(cJSON *list, bool isSave);
        static void renderForScene(const char* sceneId);
        static void navigateToScene(const char* sceneId);
        static void navigateBack();        // go to profile-defined root (or "" if none)
        static void setRootScene(const char* sceneId);  // called by Profiles::apply()
        static void setActiveProfileId(const char* profileId); // for profile visibility filter
        static bool update(lv_obj_t *widget);
        static bool updateById(const char* widgetId);
        static bool updateDataFromUrl(const char* widgetId, const char* url, cJSON* headers = nullptr,
                                      const char* parseType     = nullptr,
                                      const char* parseTemplate = nullptr,
                                      const char* parseRegex    = nullptr,
                                      cJSON*      jsonKeys      = nullptr);
        static bool updateDataFromUrlBySystem(const char* widgetId, const char* url, cJSON* headers = nullptr,
                                              const char* parseType     = nullptr,
                                              const char* parseTemplate = nullptr,
                                              const char* parseRegex    = nullptr,
                                              cJSON*      jsonKeys      = nullptr);
        // Queue a direct HTTP fetch (bypassing proxy). Used as fallback when
        // browser proxy fails (e.g. CORS) or companion is unavailable.
        static bool retryDirectHttp(const char* widgetId, const char* url);
        static bool renderUrlResult(const char* widgetId, int httpCode, const char* httpResponse, bool proxyApplied = false);
        static bool renderData(const char* widgetId, const char* content);

        // ── Async-safe wrappers for async_tcp context ──────────────────────────
        // These push work onto _wsResultQueue; actual LVGL calls happen in loop().
        static void enqueueUrlResult(const char* widgetId, int code, const char* response, bool proxyApplied = false);
        static void enqueueData(const char* widgetId, const char* data);
        static bool updateList();

        // Fast path used by WebServerBase::exitServiceMode when no widget edits
        // happened during the service-mode session. Clears existing widget
        // timers and re-arms them from the cached widget JSON WITHOUT
        // destroying or recreating any LVGL objects. Saves ~3 KB of
        // largest_free_block per service-mode cycle on no-PSRAM boards by
        // avoiding the lv_obj_clean + recreate path that fragments DRAM.
        static void armTimersOnly();

        // ── Counter widget ─────────────────────────────────────────────────────
        static void counterReset(lv_obj_t* widget);  // called on long-press

        // ── Theme support ──────────────────────────────────────────────────────
        static void rerender();  // re-render current scene (e.g. after theme change)
        static void rerenderDeferred();  // schedule rerender on next LVGL idle (after layout settles)
        // DEPRECATED — caller gets a bare pointer that may be freed under a
        // concurrent cache-rebuild. Safe only from the LVGL task; prefer
        // getCachedJsonCopy() from cross-task callers (HTTP server, etc.).
        static const char* getCachedJson();

        // Thread-safe snapshot: takes the widget state mutex, copies the cache
        // into a caller-owned std::string, releases the lock. Returns "[]" if
        // no cache is populated.
        static std::string getCachedJsonCopy();
    };
}

#endif // __CARDS_WIDGETS_H__
