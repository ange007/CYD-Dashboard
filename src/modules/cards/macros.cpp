#include "macros.h"
#include "macro_button.h"

#include "./constants.h"
#include "../hid/keyboard.h"

#include <string>
#include <map>
#include <esp_heap_caps.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "helpers.h"

#include "./ui/ui.h"

#include "./ui/ui_helpers.h"
#include "./utils/ui.h"
#include "./utils/memory.h"
#include "./utils/json.h"
#include "./utils/settings.h"
#include "./utils/heap_probe.h"
#include "../../utils/sd.h"
#include "../../utils/media_fs.h"
#include "../wifi/server.h"

#define MACROS_FILE "/macros.json"
#define MACROS_FILE_SD "/macros_backup.json"

using namespace Cards;

// ── Toggle state ──────────────────────────────────────────────────────────────

static std::map<std::string, bool> _toggleStates;

static void loadStates() {
    uMemory::read(MEMORY_STATES_KEY, [](Preferences prefs) {
        String s = prefs.isKey("items") ? prefs.getString("items") : "{}";
        cJsonPtr j = cJsonParse(s.c_str());
        if (!j) return;
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, j.get()) {
            if (item->string && cJSON_IsBool(item)) {
                _toggleStates[item->string] = cJSON_IsTrue(item);
            }
        }
    });
}

static void saveStates() {
    cJsonPtr j = cJsonCreateObject();
    for (auto& kv : _toggleStates) {
        cJSON_AddBoolToObject(j.get(), kv.first.c_str(), kv.second);
    }
    cJsonStrPtr s = cJsonPrint(j.get());
    if (s) {
        const char* raw = s.get();
        uMemory::write(MEMORY_STATES_KEY, [raw](Preferences prefs) {
            prefs.putString("items", raw);
        });
    }
}

bool Macros::getToggleState(const char* id) {
    if (!id) return false;
    auto it = _toggleStates.find(id);
    return it != _toggleStates.end() ? it->second : false;
}

void Macros::setToggleState(const char* id, bool val) {
    if (!id) return;
    _toggleStates[id] = val;
    saveStates();
}

void Macros::applyToggleBtnColor(lv_obj_t* btn, cJSON* json, bool state) {
    MacroButton::applyToggleColor(btn, json, state);
}

void Macros::refreshHidButtonStates() {
    if (!ui_cntMacros || !lv_obj_is_valid(ui_cntMacros)) return;
    bool connected = HidKeyboard::isConnected();
    _ui_enable_mutex(1);
    uint32_t cnt = lv_obj_get_child_count(ui_cntMacros);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(ui_cntMacros, i);
        lv_obj_t* btn   = child;
        if (!lv_obj_check_type(child, &lv_button_class)) {
            lv_obj_t* first = lv_obj_get_child(child, 0);
            if (first && lv_obj_check_type(first, &lv_button_class)) btn = first;
            else continue;
        }
        cJsonPtr json(uUI::getObjectDataJSON(btn), cJSON_Delete);
        if (!json) continue;
        if (MacroButton::needsHid(json.get())) {
            if (connected) lv_obj_clear_state(btn, LV_STATE_DISABLED);
            else           lv_obj_add_state  (btn, LV_STATE_DISABLED);
        }
    }
    // Show/hide keyboard tab button: visible only when HID connected AND setting enabled.
    // Single DISABLED state covers both gates — no hidden flag needed (hidden breaks tab nav).
    if (ui_tabsMain && lv_obj_is_valid(ui_tabsMain)) {
        lv_obj_t* kb_btn = lv_tabview_get_tab_button(ui_tabsMain, 2);
        if (kb_btn) {
            bool show = connected && uSettings::getKeyboardTabEnabled();
            if (show) {
                lv_obj_clear_state(kb_btn, LV_STATE_DISABLED);
            } else {
                if (lv_tabview_get_tab_act(ui_tabsMain) == 2)
                    lv_tabview_set_active(ui_tabsMain, 0, LV_ANIM_OFF);
                lv_obj_add_state(kb_btn, LV_STATE_DISABLED);
            }
        }
    }
    _ui_disable_mutex(1);
}

// ── Scene navigation state ────────────────────────────────────────────────────

static std::string _currentMacroSceneId = "";  // "" = root
static std::string _macroRootSceneId    = "";  // profile-defined root (BACK stops here)
static std::string _activeProfileId     = "";  // active profile ID (for scene visibility filter)
// Stored as a PSRAM-backed JSON string to avoid fragmenting internal DMA heap.
// Parsed into a temporary cJSON tree only during renderForScene(), then freed.
static char* _cachedMacroJson = nullptr;
static SemaphoreHandle_t _macros_state_mutex = NULL;

static void ensureMacrosMutex() {
    if (!_macros_state_mutex)
        _macros_state_mutex = xSemaphoreCreateRecursiveMutex();
}

struct MacroLock {
    MacroLock()  { ensureMacrosMutex(); xSemaphoreTakeRecursive(_macros_state_mutex, portMAX_DELAY); }
    ~MacroLock() { xSemaphoreGiveRecursive(_macros_state_mutex); }
};

const char* Macros::getCachedJson() {
    return _cachedMacroJson ? _cachedMacroJson : "[]";
}

std::string Macros::getCachedJsonCopy() {
    MacroLock _ml;
    return _cachedMacroJson ? std::string(_cachedMacroJson) : "[]";
}

// Keeps the background image path string alive for LVGL (LVGL holds a raw pointer).
static std::string _macroBgSrcPath;

// ── Scene background helper ───────────────────────────────────────────────────

// Returns the background image filename for the given scene.
// Uses the in-memory cache populated by uSettings — no NVS access on the render path.
static String getSceneBgFilename(const char* sceneId)
{
    const char* safeId = sceneId ? sceneId : "";
    const char* json   = uSettings::getSceneBgsJson();
    ESP_LOGD("MacroBG", "lookup sceneId='%s' json='%s'", safeId, json ? json : "null");
    cJsonPtr map = cJsonParse(json);
    if (!map) return "";
    cJSON* entry = cJSON_GetObjectItemCaseSensitive(map.get(), safeId);
    String result;
    if (entry && cJSON_IsString(entry) && entry->valuestring[0] != '\0') {
        result = entry->valuestring;
    }
    ESP_LOGD("MacroBG", "result='%s'", result.c_str());
    return result;
}

// ── Scroll perf optimization ──────────────────────────────────────────────────
// During scroll: hide JPEG bg (replace with cheap solid fill) and disable button
// shadows (which otherwise force per-frame blur-mask compute, ~24KB per button).
// Restored asynchronously after scroll ends so the next idle frame does the work.

static bool _scrollHidBg = false;

// Hide bg + shadows on the macros container (called from any scroll source).
static void macrosBgHide() {
    if (!_macroBgSrcPath.empty() && !_scrollHidBg) {
        _scrollHidBg = true;
        lv_obj_set_style_bg_image_opa(ui_cntMacros, LV_OPA_TRANSP,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    // Suppress shadows on all macro buttons.
    uint32_t n = lv_obj_get_child_count(ui_cntMacros);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* child = lv_obj_get_child(ui_cntMacros, i);
        if (!child) continue;
        lv_obj_t* btn = child;
        if (!lv_obj_check_type(child, &lv_button_class)) {
            lv_obj_t* first = lv_obj_get_child(child, 0);
            if (first && lv_obj_check_type(first, &lv_button_class)) btn = first;
            else continue;
        }
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void macrosBgRestore(void*) {
    if (_scrollHidBg) {
        lv_obj_set_style_bg_image_opa(ui_cntMacros, LV_OPA_COVER,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        _scrollHidBg = false;
    }
    MacroButton::restoreShadows(ui_cntMacros);
}

static void onMacrosScrollBegin(lv_event_t*) { macrosBgHide(); }
static void onMacrosScrollEnd(lv_event_t*)   { lv_async_call(macrosBgRestore, nullptr); }

// ── Public API ────────────────────────────────────────────────────────────────

void Macros::renderForScene(const char* sceneId)
{
    std::string snap;
    { MacroLock _ml; snap = _cachedMacroJson ? std::string(_cachedMacroJson) : ""; }
    if (snap.empty()) return;

    // Parse JSON into a temporary cJSON tree (internal heap, freed at end of function).
    // Keeping this temporary rather than a persistent cJSON tree preserves contiguous
    // internal SRAM blocks needed for display DMA byteswap buffers.
    cJsonPtr cachedList = cJsonParse(snap.c_str());
    if (!cachedList) return;

    {
    UiLock _ul;
    lv_obj_clean(ui_cntMacros);

    // Force layout so lv_obj_get_width returns the real pixel value for btnSize calc.
    lv_obj_update_layout(ui_cntMacros);

    int cols = uSettings::getMacroCols();
    if (cols <= 0) cols = SETTINGS_DEFAULT_MACRO_COLS;
    // Subtract cnt's own L/R padding — see widgets.cpp renderForScene comment.
    int cntPadL_m = lv_obj_get_style_pad_left(ui_cntMacros,  LV_PART_MAIN);
    int cntPadR_m = lv_obj_get_style_pad_right(ui_cntMacros, LV_PART_MAIN);
    int containerW = lv_obj_get_width(ui_cntMacros) - cntPadL_m - cntPadR_m;
    if (containerW <= 0) containerW = 320;

    {
        String bgFile = getSceneBgFilename(sceneId);
        if (bgFile.length() > 0) {
            char bgPrefix[20];
            snprintf(bgPrefix, sizeof(bgPrefix), "%c:/backgrounds/", uMediaFS::lvLetter());
            _macroBgSrcPath = std::string(bgPrefix) + std::string(bgFile.c_str());

            lv_obj_set_style_bg_image_src(ui_cntMacros, _macroBgSrcPath.c_str(),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_image_tiled(ui_cntMacros, false,
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_image_opa(ui_cntMacros, LV_OPA_COVER,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            _macroBgSrcPath = "";

            lv_obj_set_style_bg_image_src(ui_cntMacros, nullptr,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_image_opa(ui_cntMacros, LV_OPA_TRANSP,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    const int COL_GAP = 5;  // matches pad_column in ui_screenMain.c
    int btnSize = (containerW - COL_GAP * (cols - 1)) / cols;
    if (btnSize < 30) btnSize = 30;

    const char* safeId = sceneId ? sceneId : "";
    bool isRoot = (safeId[0] == '\0');  // true when at global root "" (used for level filter)
    bool atRoot = (strcmp(safeId, _macroRootSceneId.c_str()) == 0);  // true at profile root

    // Back button when deeper than the profile-defined root
    if (!atRoot) {
        lv_obj_t* backBtn = ui_actionButton_create(ui_cntMacros);
        lv_obj_set_width(backBtn, lv_pct(100));
        lv_obj_set_height(backBtn, 40);
        lv_obj_set_style_margin_bottom(backBtn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_label_set_text(
            ui_comp_get_child(backBtn, UI_COMP_ACTIONBUTTON_ACTIONBUTTONLABEL),
            LV_SYMBOL_LEFT " Back"
        );

        cJsonPtr backJson = cJsonCreateObject();
        cJSON_AddStringToObject(backJson.get(), "id",    "");
        cJSON_AddStringToObject(backJson.get(), "title", "Back");
        cJSON_AddStringToObject(backJson.get(), "type",  MACROS_TYPE_BACK);
        uUI::attachJsonData(backBtn, backJson.get());
    }

    // Render items for this level
    cJSON* item = NULL;
    cJSON_ArrayForEach(item, cachedList.get()) {
        const char* itemSceneId = uJSON::getString(item, "scene_id");
        const char* itemType    = uJSON::getString(item, "type");
        bool hasSceneId = (itemSceneId && itemSceneId[0] != '\0');

        // Level filter
        if (isRoot) {
            if (hasSceneId) continue;  // root: only items without scene_id

            // Profile visibility filter — only for bare scene definitions (folder buttons).
            // If a scene has a non-empty profile_ids array, show it only when the active
            // profile is listed.  Empty array = visible in all profiles (backward compatible).
            if (itemType && strcmp(itemType, MACROS_TYPE_SCENE) == 0) {
                const char* targetId = uJSON::getString(item, "target_id");
                bool isFolder = (!targetId || targetId[0] == '\0');
                if (isFolder) {
                    // "Show at root" toggle — hide_root:true keeps this folder out of
                    // the root list (reachable only as a sub-scene or navigator target).
                    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "hide_root"))) continue;
                    cJSON* profileIds = cJSON_GetObjectItemCaseSensitive(item, "profile_ids");
                    if (cJSON_IsArray(profileIds) && cJSON_GetArraySize(profileIds) > 0) {
                        bool match = false;
                        cJSON* pid = NULL;
                        cJSON_ArrayForEach(pid, profileIds) {
                            if (cJSON_IsString(pid) && _activeProfileId == pid->valuestring) {
                                match = true;
                                break;
                            }
                        }
                        if (!match) continue;
                    }
                }
            }
        } else {
            // Inside scene: only items belonging to this scene
            if (!hasSceneId || strcmp(itemSceneId, sceneId) != 0) continue;
            // Skip bare scene definitions (type=scene without target_id) —
            // these are folder roots, not shortcut buttons.
            if (itemType && strcmp(itemType, MACROS_TYPE_SCENE) == 0) {
                const char* targetId = uJSON::getString(item, "target_id");
                if (!targetId || targetId[0] == '\0') continue;  // it's a folder, not a shortcut
            }
        }

        lv_obj_t* btn = nullptr;
        MacroButton::createCell(ui_cntMacros, item, btnSize, &btn);

        // Scene navigator button: add visual border
        if (itemType && strcmp(itemType, MACROS_TYPE_SCENE) == 0) {
            MacroButton::applySceneStyle(btn);
        }
    }

    // Schedule shadow restoration for the next lv_timer_handler() tick so that
    // the initial frame renders with just the radius (cheaper), then shadows
    // are added one cycle later — avoids OOM on the 48KB LVGL heap.
    lv_async_call(MacroButton::restoreShadows, ui_cntMacros);

    } // UiLock released

    // cachedList freed by cJsonPtr destructor — internal heap is now fully released
}

void Macros::navigateToScene(const char* sceneId)
{
    _currentMacroSceneId = sceneId ? sceneId : "";
    renderForScene(_currentMacroSceneId.c_str());
}

void Macros::navigateBack()
{
    navigateToScene(_macroRootSceneId.c_str());
}

void Macros::setRootScene(const char* sceneId)
{
    std::string newId = sceneId ? sceneId : "";
    if (_macroRootSceneId == newId && _currentMacroSceneId == newId) return;
    _macroRootSceneId = newId;
    navigateToScene(newId.c_str());
}

void Macros::setActiveProfileId(const char* profileId)
{
    _activeProfileId = profileId ? profileId : "";
}

void Macros::init()
{
    HeapProbe _probe("macros.load");
    loadStates();

    // Primary storage: LittleFS /macros.json (no size limit, survives NVS full).
    // Fallback: NVS "macros"/"items" (legacy — migrated automatically on first save).
    String listStr = "[]";
    if (LittleFS.exists(MACROS_FILE)) {
        File f = LittleFS.open(MACROS_FILE, "r");
        if (f) {
            listStr = f.readString();
            f.close();
            ESP_LOGI("Macros", "Loaded %u bytes from " MACROS_FILE, (unsigned)listStr.length());
        }
    } else {
#ifdef BOARD_HAS_TF
        // Try SD card backup first — written on every save, survives LittleFS reflash.
        if (uSD::isAvailable() && uSD::getFS().exists(MACROS_FILE_SD)) {
            File sf = uSD::getFS().open(MACROS_FILE_SD, FILE_READ);
            if (sf) {
                listStr = sf.readString();
                sf.close();
                ESP_LOGI("Macros", "Loaded %u bytes from SD " MACROS_FILE_SD, (unsigned)listStr.length());
            }
        }
        if (listStr == "[]")
#endif
        {
            // Legacy NVS path — read once for migration; re-saved via LittleFS/SD on next user edit.
            uMemory::read("macros", [&listStr](Preferences preferences) {
                if (preferences.isKey("items")) listStr = preferences.getString("items");
            });
            ESP_LOGI("Macros", "Loaded from NVS (legacy) %u bytes", (unsigned)listStr.length());
        }
    }

    cJsonPtr list = cJsonParse(listStr.c_str());
    if (!list) {
        ESP_LOGE("Macros", "cJSON_Parse failed -- loading empty macro list (stored JSON may be corrupt)");
        list = cJsonCreateArray();
        if (!list) { ESP_LOGE("Macros", "cJSON_CreateArray OOM -- macros will be empty"); return; }
    }
    set(list.get(), false);

    // Scroll-perf: hide bg + shadows during scroll, restore on end.
    // 1) Within-tab vertical scroll (on the macros container itself).
    lv_obj_add_event_cb(ui_cntMacros, onMacrosScrollBegin, LV_EVENT_SCROLL_BEGIN, nullptr);
    lv_obj_add_event_cb(ui_cntMacros, onMacrosScrollEnd,   LV_EVENT_SCROLL_END,   nullptr);

    // 2) Tab-switch horizontal scroll — fires on the tabview's content panel.
    //    Disable swipe-to-switch (tabs still work via buttons) and add perf handlers.
    lv_obj_t* tvContent = lv_tabview_get_content(ui_tabsMain);
    if (tvContent) {
        lv_obj_clear_flag(tvContent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(tvContent, onMacrosScrollBegin, LV_EVENT_SCROLL_BEGIN, nullptr);
        lv_obj_add_event_cb(tvContent, onMacrosScrollEnd,   LV_EVENT_SCROLL_END,   nullptr);
    }
}

void Macros::set(cJSON *list, bool isSave)
{
    HeapProbe _probe("macros.save");
    if (list == nullptr || !cJSON_IsArray(list)) {
        ESP_LOGE("Macros", "Invalid macros list (null or non-array)!");
        return;
    }
    // Empty array is valid — means "clear all macros"

    // Serialize once; reuse for NVS save and PSRAM cache
    cJsonStrPtr jsonStr = cJsonPrint(list);
    if (!jsonStr) return;

    if (isSave) {
        // Mark widgets dirty (macros + widgets share the same rebuild path on
        // service-mode exit) so we get the full LVGL rebuild instead of the
        // fast-path armTimersOnly.
        if (WiFiModule::WSServer::isServiceMode())
            WiFiModule::WSServer::markWidgetsDirty();
        // Save to LittleFS — no NVS size limit, survives heavily fragmented NVS.
        File f = LittleFS.open(MACROS_FILE, "w");
        if (f) {
            size_t written = f.print(jsonStr.get());
            f.close();
            ESP_LOGI("Macros", "Saved %u bytes to " MACROS_FILE, (unsigned)written);
        } else {
            ESP_LOGE("Macros", "Failed to open " MACROS_FILE " for writing");
        }
#ifdef BOARD_HAS_TF
        // SD backup survives LittleFS reflash (firmware+FS update wipes LittleFS,
        // but never touches the SD card).
        if (uSD::isAvailable()) {
            File sf = uSD::getFS().open(MACROS_FILE_SD, FILE_WRITE);
            if (sf) { sf.print(jsonStr.get()); sf.close(); }
        }
#endif
    }

    // Cache as PSRAM-backed string — swap-pattern under lock prevents UAF when
    // HTTP task reads the pointer concurrently via getCachedJsonCopy().
    {
        size_t len = strlen(jsonStr.get()) + 1;
        char* fresh = (char*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!fresh) fresh = (char*)malloc(len);
        if (fresh) memcpy(fresh, jsonStr.get(), len);
        char* old;
        {
            MacroLock _ml;
            old = _cachedMacroJson;
            _cachedMacroJson = fresh;
        }
        free(old);
    }

    if (isSave) {
        // Called from the HTTP task (AsyncTCP, Core 0) — do NOT touch LVGL objects here.
        // Defer the re-render to the LVGL task via lv_async_call so it runs on Core 1
        // inside lv_timer_handler, avoiding cross-task use-after-free crashes.
        lv_async_call([](void*) { Macros::rerender(); }, nullptr);
        uSettings::bumpStateVersion();
    } else {
        // Called during init (main task / setup) — LVGL task not yet running, safe to render directly.
        renderForScene(_currentMacroSceneId.c_str());
    }
}

bool Macros::showPopupMsg()
{
    return false;
}

bool Macros::sendDataToUrl(const char* macrosId, const char* url)
{
   return Helpers::updateDataFromUrl(url, nullptr, [&macrosId, &url](int code, const char *response) {
        bool callbackState = (code == 200);

        if (callbackState) {
            showPopupMsg();
        } else {
            callbackState = sendDataToUrlBySystem(macrosId, url);
        }

        return callbackState;
    });
}

bool Macros::sendDataToUrlBySystem(const char* macrosId, const char* url)
{
    return Helpers::updateDataFromUrlBySystem("macros", macrosId, url);
}

void Macros::rerender()
{
    renderForScene(_currentMacroSceneId.c_str());
}
