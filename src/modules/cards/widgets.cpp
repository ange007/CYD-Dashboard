#include "widgets.h"
#include "widget_card.h"
#include "helpers.h"

#include <HTTPClient.h>
#include <map>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>

#define WIDGETS_FILE "/widgets.json"
#define WIDGETS_FILE_SD "/widgets_backup.json"

#include "../wifi/core.h"
#include "../wifi/server.h"
#include "../../utils/sd.h"
#include "../../utils/media_fs.h"
#include "../wifi/helpers.h"
#include "../wifi/api.h"

#include "./utils/ui.h"
#include "./utils/memory.h"
#include "./utils/json.h"
#include "./utils/settings.h"
#include "./utils/heap_probe.h"

#include "./modules/timers.h"
#include "./ui/ui.h"


#define WIDGET_STYLE_PROGRESS_HORIZONTAL 0
#define WIDGET_STYLE_PROGRESS_VERTICAL 1
#define WIDGET_STYLE_PROGRESS_ARC 2

using namespace Cards;

// Keeps the background image path string alive for LVGL (LVGL holds a raw pointer).
static std::string _widgetBgSrcPath;

// ── Scroll perf optimization ──────────────────────────────────────────────────
// Hide the JPEG bg during scroll — single solid-fill is much cheaper than
// blitting decoded pixels into every dirty rect. Widget cards have no shadows,
// so no per-button work needed.

static bool _widgetScrollHidBg = false;

static void widgetsBgHide() {
    if (!_widgetBgSrcPath.empty() && !_widgetScrollHidBg) {
        _widgetScrollHidBg = true;
        lv_obj_set_style_bg_image_opa(ui_cntWidgets, LV_OPA_TRANSP,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void widgetsBgRestore(void*) {
    if (_widgetScrollHidBg) {
        lv_obj_set_style_bg_image_opa(ui_cntWidgets, LV_OPA_COVER,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        _widgetScrollHidBg = false;
    }
}

static void onWidgetsScrollBegin(lv_event_t*) { widgetsBgHide(); }
static void onWidgetsScrollEnd(lv_event_t*)   { lv_async_call(widgetsBgRestore, nullptr); }

// ── Scene background helper ───────────────────────────────────────────────────

// Uses the in-memory cache populated by uSettings — no NVS access on the render path.
static String getSceneBgFilename(const char* sceneId)
{
    const char* safeId = sceneId ? sceneId : "";
    cJsonPtr map = cJsonParse(uSettings::getSceneBgsJson());
    if (!map) return "";
    cJSON* entry = cJSON_GetObjectItemCaseSensitive(map.get(), safeId);
    String result;
    if (entry && cJSON_IsString(entry) && entry->valuestring[0] != '\0') {
        result = entry->valuestring;
    }
    return result;
}

// ── Error border helper ───────────────────────────────────────────────────────
// Toggles LV_STATE_USER_1 on the widget card; the themed bottom-border style
// for that state is pre-registered in ui_comp_widget.c.
static void setWidgetError(lv_obj_t* widget, bool hasError)
{
    if (!widget) return;
    if (hasError) lv_obj_add_state(widget, LV_STATE_USER_1);
    else          lv_obj_clear_state(widget, LV_STATE_USER_1);
}

// ── Async HTTP task support ───────────────────────────────────────────────────

struct HttpResult {
    char  widgetId[64];
    int   code;
    char* response; // heap-allocated, freed by Widgets::loop()
};

// WsResult: queued by async_tcp context (api.cpp onReturnUrlResponse /
// onReturnSystemInfo), drained by Widgets::loop() in loopTask.
// Separating async_tcp from LVGL calls eliminates the race that caused
// lv_label_set_text to fire while lv_obj_update_layout was running in loopTask,
// which kept re-dirtying scr_layout_inv and prevented layout from settling.
struct WsResult {
    char  widgetId[64];
    int   code;          // HTTP status for url_response; 200 for system_info
    char* data;          // heap-allocated copy, freed by Widgets::loop()
    bool  isUrl;         // true → renderUrlResult, false → renderData
    bool  proxyApplied;  // true → proxy already extracted value; skip on-device parsing
};

struct HttpJob {
    char   widgetId[64];
    char   url[512];
    cJSON* headers;  // owned; freed inside httpFetchTask
};

static QueueHandle_t _httpJobQueue    = nullptr; // HttpJob* pointers
static QueueHandle_t _httpResultQueue = nullptr;
static QueueHandle_t _wsResultQueue   = nullptr; // WsResult items from async_tcp

#define HTTP_WORKERS   1   // single worker — saves internal SRAM for DMA
#define HTTP_JOB_QUEUE 8   // max queued requests

// ── Host failure backoff ─────────────────────────────────────────────────────
// Track consecutive failures per host to implement exponential backoff.
// Prevents unreachable hosts (e.g. homeassistant.local) from draining heap
// with repeated connection attempts.
struct HostFailure {
    uint8_t  count;       // consecutive failures (capped at 10)
    uint32_t nextRetryMs; // millis() after which retry is allowed
};
static std::map<std::string, HostFailure> _hostFailures;
static SemaphoreHandle_t _hostFailMutex = nullptr;

// Extract "host:port" from a URL for failure tracking
static std::string extractHost(const char* url) {
    std::string s(url);
    auto pos = s.find("://");
    if (pos != std::string::npos) s = s.substr(pos + 3);
    pos = s.find('/');
    if (pos != std::string::npos) s = s.substr(0, pos);
    return s;
}

// Heap threshold below which direct ESP→URL HTTP fetches are skipped.
// Bumped from 8 KB → 12 KB after tests showed: when browser loads web UI
// under widget HTTP pressure, heap dips to 4-5 KB and lwIP refuses new
// TCP accepts for 90+ s (pool exhaustion). Widgets skipping at 12 KB
// reserves headroom so the server-side TCP accept path can always alloc
// its pbufs + PCB. Trade-off: widgets may pause during bursts, but pause
// is transparent (backoff retries a few seconds later).
static constexpr uint32_t HTTP_HEAP_MIN = 12000;

// Uncomment to allow ESP32 to fetch HTTPS URLs directly (TLS fallback).
// TLS handshake needs ~40-50 KB of free heap and is slow (~7 s).
// When disabled, HTTPS is always routed through browser/companion proxy.
// #define WIDGET_HTTPS_DIRECT_FALLBACK
#ifdef WIDGET_HTTPS_DIRECT_FALLBACK
static constexpr uint32_t HTTPS_HEAP_MIN = 50000;
#endif

// ── Pending request timeout ──────────────────────────────────────────────────
// Track when a request was sent for each widget so we can detect stale state
// (e.g. proxy died after receiving the request, widget never gets a response).
static std::map<std::string, uint32_t> _pendingRequest; // widgetId → millis() of send
static constexpr uint32_t WIDGET_RESPONSE_TIMEOUT = 15000; // 15 seconds

// ── Counter state ─────────────────────────────────────────────────────────────
static std::map<std::string, int> _counterValues;

static void loadCounterStates() {
    uMemory::read(MEMORY_STATES_KEY, [](Preferences prefs) {
        String s = prefs.isKey("cnt") ? prefs.getString("cnt") : "{}";
        cJsonPtr j = cJsonParse(s.c_str());
        if (!j) return;
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, j.get()) {
            if (item->string && cJSON_IsNumber(item)) {
                _counterValues[item->string] = (int)item->valuedouble;
            }
        }
    });
}

static void saveCounterState(const char* id, int val) {
    if (!id) return;
    _counterValues[id] = val;
    cJsonPtr j = cJsonCreateObject();
    for (auto& kv : _counterValues) {
        cJSON_AddNumberToObject(j.get(), kv.first.c_str(), kv.second);
    }
    cJsonStrPtr s = cJsonPrint(j.get());
    if (s) {
        uMemory::write(MEMORY_STATES_KEY, [&s](Preferences prefs) {
            prefs.putString("cnt", s.get());
        });
    }
}

// ── Timer state ───────────────────────────────────────────────────────────────
static std::map<std::string, int> _timerRemaining;  // id → seconds left, -1 = not running

static void formatTimerText(char* buf, size_t bufSize, int seconds) {
    if (seconds < 0) seconds = 0;
    snprintf(buf, bufSize, "%d:%02d", seconds / 60, seconds % 60);
}

// ── Scene navigation state ────────────────────────────────────────────────────
static std::string _currentWidgetSceneId = "";  // "" = root
static std::string _widgetRootSceneId    = "";  // profile-defined root (BACK stops here)
static std::string _activeProfileId      = "";  // for profile visibility filter
// Stored as a PSRAM-backed JSON string to avoid fragmenting internal DMA heap.
// Parsed into a temporary cJSON tree only during renderForScene(), then freed.
static char* _cachedWidgetJson = nullptr;

// ── Shared state mutex ────────────────────────────────────────────────────────
// Protects cross-task access to _pendingRequest, _counterValues, _timerRemaining,
// and _cachedWidgetJson. HTTP worker task (response callbacks) and LVGL loop task
// (render, event handlers, timeout scan) both read/write these structures; without
// the mutex std::map iterators could invalidate mid-iteration.
// Recursive so nested takes from the same task (e.g. within an event handler that
// calls into another helper) do not self-deadlock.
static SemaphoreHandle_t _widget_state_mutex = NULL;

static inline void ensureWidgetMutex() {
    if (!_widget_state_mutex) {
        _widget_state_mutex = xSemaphoreCreateRecursiveMutex();
    }
}

struct WidgetLock {
    WidgetLock()  { ensureWidgetMutex(); xSemaphoreTakeRecursive(_widget_state_mutex, portMAX_DELAY); }
    ~WidgetLock() { xSemaphoreGiveRecursive(_widget_state_mutex); }
};

// ── Static log buffer (zero-alloc widget log entries) ─────────────────────────
// Replaces per-call cJsonCreateObject + N × AddString + cJSON_Print pattern that
// fragmented DRAM on no-PSRAM boards (~8 small mallocs per widget update tick).
// Mutex protects from concurrent update() (LVGL task) vs renderUrlResult drain
// (loop task). Buffer sized 256 B; pushLogEvent truncates at 191 chars (same as
// existing cJSON path — no regression for URLs > 86 chars).
static char              _wLogBuf[256];
static SemaphoreHandle_t _wLogMux = nullptr;

static inline void ensureWLogMux() {
    if (!_wLogMux) _wLogMux = xSemaphoreCreateMutex();
}

// Append literal bytes; stops before NUL terminator slot.
static void _wLogAppendLit(char*& dst, char* end, const char* lit) {
    while (*lit && dst + 1 < end) *dst++ = *lit++;
}

// Append JSON-escaped string (escapes " and \\, matching api.cpp:getUrl policy).
static void _wLogAppendEscaped(char*& dst, char* end, const char* src) {
    if (!src) return;
    while (*src && dst + 2 < end) {
        if (*src == '"' || *src == '\\') *dst++ = '\\';
        *dst++ = *src++;
    }
}

const char* Widgets::getCachedJson() {
    // DEPRECATED cross-task reader (see header). Intended for LVGL-task callers
    // where the swap-on-rebuild in parseAndRebuild() cannot overlap because
    // parseAndRebuild's LVGL work is also pinned to the LVGL task via
    // lv_async_call. Cross-task callers (HTTP server) should use
    // getCachedJsonCopy() which takes the widget mutex and copies.
    return _cachedWidgetJson ? _cachedWidgetJson : "[]";
}

std::string Widgets::getCachedJsonCopy() {
    WidgetLock _wlk;
    return _cachedWidgetJson ? std::string(_cachedWidgetJson) : std::string("[]");
}

// ── Masonry layout ────────────────────────────────────────────────────────────

static int32_t masonryColCount(int32_t inner_w, int32_t gap)
{
    // Fixed override from settings (0 = auto).
    int32_t forced = uSettings::getWidgetColumns();
    if (forced >= 2 && forced <= 4) return forced;

    // Auto: reference width matches WIDGET_CARD_WIDTH (ui_comp_widget.c) so that
    // columns are wide enough for default widget cards.  Using the same
    // DISPLAY_WIDTH tiers avoids creating too many narrow columns on small
    // displays (e.g. 480×272 → 2 columns of ~227 px instead of 3 × 150 px).
#if defined(DISPLAY_WIDTH) && DISPLAY_WIDTH >= 800
    const int32_t ref_w = 200;
#elif defined(DISPLAY_WIDTH) && DISPLAY_WIDTH >= 480
    const int32_t ref_w = 165;
#else
    const int32_t ref_w = 130;
#endif
    int32_t ncols = (inner_w + gap) / (ref_w + gap);
    if (ncols < 2) ncols = 2;
    if (ncols > 4) ncols = 4;
    return ncols;
}

static int32_t estimateWidgetHeight(cJSON* item)
{
    int h = uJSON::getInt(item, "height", 0);
    if (h > 0) return h;
    const char* type = uJSON::getString(item, "type");
    if (!type) return 60;
    if (strcmp(type, WIDGET_TYPE_CHART) == 0) return 110;
    if (strcmp(type, WIDGET_TYPE_PROGRESS) == 0) {
        int style = uJSON::getInt(item, "style", 0);
        if (style == WIDGET_STYLE_PROGRESS_VERTICAL) return 230;
        if (style == WIDGET_STYLE_PROGRESS_ARC)      return 105;
        return 50;
    }
    return 50;
}

// One-shot masonry positioning — called via lv_async_call() after renderForScene()
// so that LVGL has already resolved widget sizes in a normal timer tick.
// NOT registered as an LVGL layout callback: registering it and letting LVGL call
// it from within lv_obj_update_layout()'s while(scr_layout_inv) loop causes an
// infinite loop, because lv_obj_set_style_x/y re-marks the parent layout dirty.
//
// span detection: s = round((cw + gap) / (col_w + gap)), clamped [1, ncols].
// full-width: span == ncols (or cw >= iw - 1).
// Multi-span: find the consecutive group of s columns with min max-height.
static void masonry_layout_cb(lv_obj_t* cont, void* /*user_data*/)
{
    const int32_t gap = 5;
    int32_t padL = lv_obj_get_style_pad_left(cont,  LV_PART_MAIN);
    int32_t padR = lv_obj_get_style_pad_right(cont, LV_PART_MAIN);
    int32_t padT = lv_obj_get_style_pad_top(cont,   LV_PART_MAIN);
    int32_t iw   = lv_obj_get_width(cont) - padL - padR;

    int32_t ncols = masonryColCount(iw, gap);
    int32_t col_w = (iw - gap * (ncols - 1)) / ncols;
    int32_t colH[4] = {};

    uint32_t cnt = lv_obj_get_child_cnt(cont);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(cont, i);
        if (!child) continue;

        int32_t cw = lv_obj_get_width(child);
        int32_t ch = lv_obj_get_height(child);

        // Detect span from widget width
        int32_t s = (col_w > 0) ? (cw + gap + col_w / 2) / (col_w + gap) : 1;
        if (s < 1)     s = 1;
        if (s > ncols) s = ncols;
        // Also treat near-full-width as ncols
        if (cw >= iw - 1) s = ncols;

        // Widget components are created with LV_ALIGN_CENTER (ui_comp_widget.c).
        // With that alignment active, style_x/y are treated as offsets from the
        // center — not absolute positions.  Reset to TOP_LEFT so our coordinates
        // are absolute within the container.
        lv_obj_set_align(child, LV_ALIGN_TOP_LEFT);

        if (s >= ncols) {
            // Full-width: align all columns to the same baseline
            int32_t maxH = 0;
            for (int32_t c = 0; c < ncols; c++) if (colH[c] > maxH) maxH = colH[c];
            lv_obj_set_style_x(child, padL, 0);
            lv_obj_set_style_y(child, padT + maxH, 0);
            int32_t nH = maxH + ch + gap;
            for (int32_t c = 0; c < ncols; c++) colH[c] = nH;
        } else if (s == 1) {
            // Single column: shortest column
            int32_t best = 0;
            for (int32_t c = 1; c < ncols; c++)
                if (colH[c] < colH[best]) best = c;
            lv_obj_set_style_x(child, padL + best * (col_w + gap), 0);
            lv_obj_set_style_y(child, padT + colH[best], 0);
            colH[best] += ch + gap;
        } else {
            // Multi-span: find consecutive group of s columns with lowest max-height
            int32_t bestStart = 0;
            int32_t bestMaxH  = 0x7FFFFFFF;
            for (int32_t c = 0; c <= ncols - s; c++) {
                int32_t groupMaxH = 0;
                for (int32_t k = 0; k < s; k++)
                    if (colH[c + k] > groupMaxH) groupMaxH = colH[c + k];
                if (groupMaxH < bestMaxH) {
                    bestMaxH  = groupMaxH;
                    bestStart = c;
                }
            }
            lv_obj_set_style_x(child, padL + bestStart * (col_w + gap), 0);
            lv_obj_set_style_y(child, padT + bestMaxH, 0);
            int32_t nH = bestMaxH + ch + gap;
            for (int32_t k = 0; k < s; k++) colH[bestStart + k] = nH;
        }
    }
}

// Recursively collect all leaf widget objects (those with user_data set).
// Containers without user_data (columnsRow, mCols in mode 1) are traversed but not collected.
static void collectWidgets(lv_obj_t* parent, std::vector<lv_obj_t*>& out)
{
    uint32_t cnt = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(parent, i);
        if (!child) continue;
        if (lv_obj_get_user_data(child)) {
            out.push_back(child);
        } else {
            collectWidgets(child, out);
        }
    }
}

// Persistent HTTP worker — runs forever, drains _httpJobQueue.
// Single worker pinned to Core 0.
static void httpWorkerTask(void* /*pvParam*/)
{
    for (;;) {
        HttpJob* job = nullptr;
        if (xQueueReceive(_httpJobQueue, &job, portMAX_DELAY) != pdTRUE || !job) continue;

        // Pause HTTP fetches during:
        //  - web UI page load (isWebServing)
        //  - SD file serve (isSdServing)
        //  - service mode (isServiceMode) — avoids NimBLE deinit vs HTTPClient
        //    alloc race on Core 0 that crashes the device mid-BLE-teardown
        while (WiFiModule::WSServer::isWebServing() ||
               uSD::isSdServing() ||
               WiFiModule::WSServer::isServiceMode()) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        // ── Heap guard: skip fetch when memory is critically low ──
#ifdef WIDGET_HTTPS_DIRECT_FALLBACK
        uint32_t heapMin = (strncmp(job->url, "https://", 8) == 0) ? HTTPS_HEAP_MIN : HTTP_HEAP_MIN;
#else
        uint32_t heapMin = HTTP_HEAP_MIN;
#endif
        if (ESP.getFreeHeap() < heapMin) {
            ESP_LOGW("Widgets", "Heap %u < %u — skipping HTTP fetch for %s",
                     ESP.getFreeHeap(), heapMin, job->widgetId);
            if (job->headers) cJSON_Delete(job->headers);
            free(job);
            vTaskDelay(pdMS_TO_TICKS(2000)); // let heap recover
            continue;
        }

        // ── Host backoff: skip if this host is still in cooldown ──
        std::string host = extractHost(job->url);
        if (!host.empty() && _hostFailMutex) {
            xSemaphoreTake(_hostFailMutex, portMAX_DELAY);
            auto it = _hostFailures.find(host);
            if (it != _hostFailures.end() && millis() < it->second.nextRetryMs) {
                uint32_t wait = it->second.nextRetryMs - millis();
                xSemaphoreGive(_hostFailMutex);
                ESP_LOGD("Widgets", "Host %s in backoff (%u ms left) — skipping %s",
                         host.c_str(), wait, job->widgetId);
                if (job->headers) cJSON_Delete(job->headers);
                free(job);
                continue;
            }
            xSemaphoreGive(_hostFailMutex);
        }

        bool success = false;
        Cards::Helpers::updateDataFromUrl(job->url, job->headers,
            [&job, &success](int code, const char* resp) -> bool {
                if (code == HTTP_CODE_OK) {
                    HttpResult r;
                    strncpy(r.widgetId, job->widgetId, sizeof(r.widgetId) - 1);
                    r.widgetId[sizeof(r.widgetId) - 1] = '\0';
                    r.code     = code;
                    r.response = strdup(resp ? resp : "");
                    if (xQueueSend(_httpResultQueue, &r, 0) != pdTRUE) {
                        free(r.response);
                    } else {
                        success = true;
                    }
                }
                return true;
            });

        // ── Update host failure tracking ──
        if (!host.empty() && _hostFailMutex) {
            xSemaphoreTake(_hostFailMutex, portMAX_DELAY);
            if (success) {
                _hostFailures.erase(host); // reset on success
            } else {
                auto& hf = _hostFailures[host];
                hf.count = (hf.count < 10) ? hf.count + 1 : 10;
                // Exponential backoff: 10s, 20s, 40s, 80s … max 5 min
                uint32_t delaySec = 10u << (hf.count - 1);
                if (delaySec > 300) delaySec = 300;
                hf.nextRetryMs = millis() + delaySec * 1000;
                ESP_LOGW("Widgets", "Host %s fail #%u — backoff %u s  (widget %s)",
                         host.c_str(), hf.count, delaySec, job->widgetId);
            }
            xSemaphoreGive(_hostFailMutex);
        }

        if (!success) {
            ESP_LOGW("Widgets", "HTTP fetch failed, falling back to url_system for %s", job->widgetId);
            Cards::Helpers::updateDataFromUrlBySystem("widget", job->widgetId, job->url, job->headers);
        }

        if (job->headers) cJSON_Delete(job->headers);
        free(job);
    }
}

// Initialize the WS-result queue early — cheap (~200 B), needed for any
// companion-proxy-based widget response, with or without direct HTTP.
static void ensureWsQueue()
{
    if (_wsResultQueue) return;
    ensureWidgetMutex();
    _wsResultQueue = xQueueCreate(16, sizeof(WsResult));
}

// Create HTTP worker task + job/result queues on first direct-HTTP request.
// Skipped entirely when all widgets use companion/proxy (url_system) or when
// the dashboard has no URL widgets at all — saves ~10 KB DRAM (8 KB worker
// stack + queue entries) on setups that don't need direct fetches.
static void ensureHttpWorkers()
{
    if (_httpJobQueue) return;
    ensureWsQueue();
    _httpResultQueue = xQueueCreate(8, sizeof(HttpResult));
    _httpJobQueue    = xQueueCreate(HTTP_JOB_QUEUE, sizeof(HttpJob*));
    _hostFailMutex   = xSemaphoreCreateMutex();
    for (int i = 0; i < HTTP_WORKERS; i++) {
        xTaskCreatePinnedToCore(httpWorkerTask, "http_work", 8192, nullptr, 1, nullptr, 0);
    }
    ESP_LOGI("Widgets", "HTTP worker task spawned on demand (direct URL widget present)");
}

// One-shot display event handler: runs masonry positioning AFTER the first
// lv_obj_update_layout() completes (LV_EVENT_UPDATE_LAYOUT_COMPLETED).
// Using lv_async_call ran BEFORE layout, so widget heights were still 0.
// This event fires AFTER layout, guaranteeing correct heights.
// Registered with ui_cntWidgets as user_data so rapid switches can cancel it.
static void masonry_layout_done_cb(lv_event_t* e)
{
    lv_display_t* disp = (lv_display_t*)lv_event_get_target(e);
    lv_obj_t* cnt = (lv_obj_t*)lv_event_get_user_data(e);
    // One-shot: remove self before running masonry (masonry sets style_x/y which
    // triggers another layout pass; we don't want to re-run masonry then).
    lv_display_remove_event_cb_with_user_data(disp, masonry_layout_done_cb, cnt);
    if(lv_obj_is_valid(cnt)) masonry_layout_cb(cnt, nullptr);
}

void Widgets::renderForScene(const char* sceneId)
{
    // Snapshot cache under the lock so a concurrent HTTP-side rebuild can't
    // free the buffer while cJSON_Parse is reading it. Duplicate into a local
    // string so the actual parse runs outside the critical section.
    std::string cacheSnapshot;
    {
        WidgetLock _wlk;
        if (!_cachedWidgetJson) return;
        cacheSnapshot = _cachedWidgetJson;
    }
    cJsonPtr cachedList = cJsonParse(cacheSnapshot.c_str());
    if (!cachedList) return;

    _ui_enable_mutex(1);
    lv_obj_clean(ui_cntWidgets);

    {
        String bgFile = getSceneBgFilename(sceneId);
        if (bgFile.length() > 0) {
            char bgPrefix[20];
            snprintf(bgPrefix, sizeof(bgPrefix), "%c:/backgrounds/", uMediaFS::lvLetter());
            _widgetBgSrcPath = std::string(bgPrefix) + std::string(bgFile.c_str());
            lv_obj_set_style_bg_image_src(ui_cntWidgets, _widgetBgSrcPath.c_str(),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_image_tiled(ui_cntWidgets, false,
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_image_opa(ui_cntWidgets, LV_OPA_COVER,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            _widgetBgSrcPath = "";
            lv_obj_set_style_bg_image_src(ui_cntWidgets, nullptr,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_image_opa(ui_cntWidgets, LV_OPA_TRANSP,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    // ── Layout mode setup ──────────────────────────────────────────────────────
    const int   masonryMode = uSettings::getMasonryStyle();
    const int32_t mGap      = 5;

    // Force layout resolution on the EMPTY container so lv_obj_get_width returns
    // the real pixel width.  Container has no children here (just cleaned), so
    // this completes in microseconds — no label text measurement involved.
    lv_obj_update_layout(ui_cntWidgets);
    // Subtract cnt's own L/R padding — it is part of the container rectangle
    // but NOT usable for child placement (tab-fix moved the 10 px inset from
    // ui_tabWidget to ui_cntWidgets; without the subtraction here masonry
    // would size widgets as if the full cnt width were usable → overflow).
    int32_t cntPadL = lv_obj_get_style_pad_left(ui_cntWidgets,  LV_PART_MAIN);
    int32_t cntPadR = lv_obj_get_style_pad_right(ui_cntWidgets, LV_PART_MAIN);
    int32_t cntW    = lv_obj_get_width(ui_cntWidgets) - cntPadL - cntPadR;

    // col_w is computed for all modes so span settings work universally
    int32_t ncols = masonryColCount(cntW, mGap);
    int32_t col_w = (ncols > 0) ? (cntW - mGap * (ncols - 1)) / ncols : 0;
    if (col_w <= 0) col_w = cntW;  // fallback if container not sized yet

    if (masonryMode == 0) {
        lv_obj_set_flex_flow(ui_cntWidgets, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(ui_cntWidgets, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    } else if (masonryMode == 1) {
        lv_obj_set_flex_flow(ui_cntWidgets, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(ui_cntWidgets, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    } else {
        // Mode 2: absolute-positioned masonry.
        // Use LV_LAYOUT_NONE so LVGL never calls masonry_layout_cb automatically.
        // Positioning is done in a manual one-shot call after widget creation below.
        lv_obj_set_layout(ui_cntWidgets, LV_LAYOUT_NONE);
    }

    const char* safeId = sceneId ? sceneId : "";
    bool isRoot = (safeId[0] == '\0');  // for level filter
    bool atRoot = (strcmp(safeId, _widgetRootSceneId.c_str()) == 0);  // for BACK button

    // Back button when deeper than the profile-defined root
    if (!atRoot) {
        lv_obj_t* backBtn = ui_actionButton_create(ui_cntWidgets);
        lv_obj_set_width(backBtn, lv_pct(100));
        lv_obj_set_height(backBtn, 40);
        lv_obj_set_style_margin_bottom(backBtn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(ui_comp_get_child(backBtn, UI_COMP_ACTIONBUTTON_ACTIONBUTTONLABEL), LV_SYMBOL_LEFT " Back");
        WidgetCard::applyColor(backBtn, 0x3d7ed4, false);

        cJsonPtr backJson = cJsonCreateObject();
        cJSON_AddStringToObject(backJson.get(), "id",    "");
        cJSON_AddStringToObject(backJson.get(), "title", "Back");
        cJSON_AddStringToObject(backJson.get(), "type",  MACROS_TYPE_BACK);
        uUI::attachJsonData(backBtn, backJson.get());
        // backJson freed automatically by cJsonPtr at scope exit
    }

    // ── Column containers for mode 1 (column-flex masonry) ────────────────────
    lv_obj_t* mCols[4] = {};
    int32_t   colHts[4] = {};

    if (masonryMode == 1 && ncols > 0) {
        lv_obj_t* columnsRow = lv_obj_create(ui_cntWidgets);
        lv_obj_remove_style_all(columnsRow);
        lv_obj_set_width(columnsRow, lv_pct(100));
        lv_obj_set_height(columnsRow, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(columnsRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(columnsRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(columnsRow, mGap, LV_PART_MAIN);
        lv_obj_clear_flag(columnsRow, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

        for (int32_t c = 0; c < ncols; c++) {
            mCols[c] = lv_obj_create(columnsRow);
            lv_obj_remove_style_all(mCols[c]);
            lv_obj_set_width(mCols[c], col_w);
            lv_obj_set_height(mCols[c], LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(mCols[c], LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(mCols[c], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_set_style_pad_row(mCols[c], mGap, LV_PART_MAIN);
            lv_obj_clear_flag(mCols[c], (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        }
    }

    // Helper: pick parent container for the next widget.
    // Mode 1: distributes to shortest column by estimated height.
    // Mode 0/2: always ui_cntWidgets.
    auto nextParent = [&](cJSON* wItem) -> lv_obj_t* {
        if (masonryMode != 1 || !mCols[0]) return ui_cntWidgets;
        int32_t h = estimateWidgetHeight(wItem);
        int32_t shortest = 0;
        for (int32_t c = 1; c < ncols; c++)
            if (colHts[c] < colHts[shortest]) shortest = c;
        colHts[shortest] += h + mGap;
        return mCols[shortest];
    };

    // Render widgets for this level
    cJSON* item = NULL;
    cJSON_ArrayForEach(item, cachedList.get()) {
        const char* itemSceneId = uJSON::getString(item, "scene_id");
        const char* itemType    = uJSON::getString(item, "type");
        bool hasSceneId = (itemSceneId && itemSceneId[0] != '\0');

        // Level filter
        if (isRoot) {
            if (hasSceneId) continue;  // root: only items without scene_id
        } else {
            // Inside scene: only items belonging to this scene
            if (!hasSceneId || strcmp(itemSceneId, sceneId) != 0) continue;
            // Skip bare scene definitions (type=scene without target_id) —
            // these are folder roots, not shortcut cards.
            if (itemType && strcmp(itemType, MACROS_TYPE_SCENE) == 0) {
                const char* targetId = uJSON::getString(item, "target_id");
                if (!targetId || targetId[0] == '\0') continue;  // it's a folder, not a shortcut
            }
        }

        // Profile visibility filter — only for bare scene folders at root level
        if (isRoot && itemType && strcmp(itemType, MACROS_TYPE_SCENE) == 0) {
            const char* targetId = uJSON::getString(item, "target_id");
            bool isFolder = (!targetId || targetId[0] == '\0');
            if (isFolder) {
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
                    if (!match) continue;  // skip — not visible for current profile
                }
            }
        }

        // Scene navigator widget — shown as a text widget card with thick border
        if (itemType && strcmp(itemType, MACROS_TYPE_SCENE) == 0) {
            const char* title = uJSON::getString(item, "title", "-");
            lv_obj_t* sceneWidget = ui_widgetText_create(nextParent(item));

            // Span/height (masonry-aware)
            // span=0: full width; span=1: 1 col; span=2: 2 cols; span=3: 3 cols
            int span   = uJSON::getInt(item, "span", 1);
            int height = uJSON::getInt(item, "height", 0);
            if (masonryMode == 1) {
                lv_obj_set_width(sceneWidget, lv_pct(100));
            } else if (span == 0 || span >= ncols) {
                lv_obj_set_width(sceneWidget, lv_pct(100));
            } else if (span > 1) {
                lv_obj_set_width(sceneWidget, col_w * span + mGap * (span - 1));
            } else {
                lv_obj_set_width(sceneWidget, col_w);
            }
            if (height > 0) lv_obj_set_height(sceneWidget, height);

            // Tile appearance: no divider/content below — just the title with a nav arrow
            lv_obj_t* sceneTitle   = ui_comp_get_child(sceneWidget, UI_COMP_WIDGET_TEXT_TITLE);
            lv_obj_t* sceneContent = ui_comp_get_child(sceneWidget, UI_COMP_WIDGET_TEXT_CONTENT);
            char sceneTitleBuf[72];
            snprintf(sceneTitleBuf, sizeof(sceneTitleBuf), "%s  " LV_SYMBOL_RIGHT, title);
            lv_label_set_text(sceneTitle, sceneTitleBuf);
            if (sceneContent) lv_obj_add_flag(sceneContent, LV_OBJ_FLAG_HIDDEN);
            if (sceneTitle) {
                lv_obj_set_style_border_width(sceneTitle, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_pad_bottom(sceneTitle, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            }

            // Color + scene border
            WidgetCard::applyItemColor(sceneWidget, item);
            WidgetCard::applySceneStyle(sceneWidget);

            uUI::attachJsonData(sceneWidget, item);
            continue;
        }

        // Regular widget — skip unknown types before assigning a parent
        if (!itemType ||
            (strcmp(itemType, WIDGET_TYPE_TEXT)     != 0 &&
             strcmp(itemType, WIDGET_TYPE_CHART)    != 0 &&
             strcmp(itemType, WIDGET_TYPE_PROGRESS) != 0 &&
             strcmp(itemType, WIDGET_TYPE_COUNTER)  != 0 &&
             strcmp(itemType, WIDGET_TYPE_TIMER)    != 0 &&
             strcmp(itemType, WIDGET_TYPE_IMAGE)    != 0)) {
            ESP_LOGE("Widgets", "Unknown widget type: %s", itemType ? itemType : "null");
            continue;
        }

        const char* title = "-";
        const char* icon  = uJSON::getString(item, "icon");
        if (!icon || !icon[0]) {
            title = uJSON::getString(item, "title", "-");
        }

        lv_obj_t* wParent = nextParent(item);
        lv_obj_t* ui_widget = nullptr;
        int childTitleIndex = 0;

        if (strcmp(itemType, WIDGET_TYPE_TEXT) == 0) {
            ui_widget = ui_widgetText_create(wParent);
            childTitleIndex = UI_COMP_WIDGET_TEXT_TITLE;
            if (cJSON_HasObjectItem(item, "template")) {
                lv_label_set_text(
                    ui_comp_get_child(ui_widget, UI_COMP_WIDGET_TEXT_CONTENT),
                    uJSON::getString(item, "template")
                );
            }
        } else if (strcmp(itemType, WIDGET_TYPE_CHART) == 0) {
            ui_widget = ui_widgetChart_create(wParent);
            childTitleIndex = UI_COMP_WIDGET_CHART_TITLE;
        } else if (strcmp(itemType, WIDGET_TYPE_PROGRESS) == 0) {
            int style = uJSON::getInt(item, "style");
            if (style == 2) {
                ui_widget = ui_widgetArc_create(wParent);
                childTitleIndex = UI_COMP_WIDGET_ARC_TITLE;
            } else {
                ui_widget = ui_widgetProgress_create(wParent, style == 1);
                childTitleIndex = UI_COMP_WIDGET_PROGRESS_TITLE;
            }
        } else if (strcmp(itemType, WIDGET_TYPE_COUNTER) == 0) {
            ui_widget = ui_widgetText_create(wParent);
            childTitleIndex = UI_COMP_WIDGET_TEXT_TITLE;
            // Restore current counter value
            const char* cid = uJSON::getString(item, "id");
            int curVal = 0;
            if (cid) {
                auto it = _counterValues.find(cid);
                if (it != _counterValues.end()) curVal = it->second;
            }
            char valBuf[16];
            snprintf(valBuf, sizeof(valBuf), "%d", curVal);
            lv_label_set_text(ui_comp_get_child(ui_widget, UI_COMP_WIDGET_TEXT_CONTENT), valBuf);
        } else if (strcmp(itemType, WIDGET_TYPE_TIMER) == 0) {
            ui_widget = ui_widgetText_create(wParent);
            childTitleIndex = UI_COMP_WIDGET_TEXT_TITLE;
            // Show current timer state or "Tap to start"
            const char* tid = uJSON::getString(item, "id");
            if (tid) {
                auto it = _timerRemaining.find(tid);
                if (it != _timerRemaining.end() && it->second >= 0) {
                    char timeBuf[16];
                    formatTimerText(timeBuf, sizeof(timeBuf), it->second);
                    lv_label_set_text(ui_comp_get_child(ui_widget, UI_COMP_WIDGET_TEXT_CONTENT), timeBuf);
                } else {
                    lv_label_set_text(ui_comp_get_child(ui_widget, UI_COMP_WIDGET_TEXT_CONTENT), "Tap to start");
                }
            } else {
                lv_label_set_text(ui_comp_get_child(ui_widget, UI_COMP_WIDGET_TEXT_CONTENT), "Tap to start");
            }
        } else if (strcmp(itemType, WIDGET_TYPE_IMAGE) == 0) {
            ui_widget = ui_widgetImage_create(wParent);
            childTitleIndex = UI_COMP_WIDGET_IMAGE_TITLE;
            // Set image source from "image_src" field (filename in /icons/ directory)
            const char* imgSrc = uJSON::getString(item, "image_src", "");
            if (imgSrc && imgSrc[0] != '\0') {
                char iconPrefixBuf[12];
                snprintf(iconPrefixBuf, sizeof(iconPrefixBuf), "%c:/icons/", uMediaFS::lvLetter());
                const char* prefix = iconPrefixBuf;
                // Build path and store it — LVGL holds a pointer, so we allocate
                size_t pathLen = strlen(prefix) + strlen(imgSrc) + 1;
                char* imgPath = (char*)malloc(pathLen);
                if (imgPath) {
                    snprintf(imgPath, pathLen, "%s%s", prefix, imgSrc);
                    lv_image_set_src(ui_comp_get_child(ui_widget, UI_COMP_WIDGET_IMAGE_CONTENT), imgPath);
                    // Store in user-data so it can be freed on delete
                    // (set after the normal user_data assignment below — overwrite with combined)
                    // For simplicity, attach a delete callback to free the path string
                    lv_obj_t* imgObj = ui_comp_get_child(ui_widget, UI_COMP_WIDGET_IMAGE_CONTENT);
                    lv_obj_set_user_data(imgObj, imgPath);
                    lv_obj_add_event_cb(imgObj, [](lv_event_t *e) {
                        void *p = lv_obj_get_user_data(lv_event_get_target_obj(e));
                        if (p) free(p);
                    }, LV_EVENT_DELETE, nullptr);
                }
            }
        }

        if (!ui_widget) continue;

        // span=0: full width; span=1: 1 col; span=2: 2 cols; span=3: 3 cols
        int span   = uJSON::getInt(item, "span", 1);
        int height = uJSON::getInt(item, "height", 0);

        if (masonryMode == 1) {
            lv_obj_set_width(ui_widget, lv_pct(100));
        } else if (span == 0 || span >= ncols) {
            lv_obj_set_width(ui_widget, lv_pct(100));
        } else if (span > 1) {
            lv_obj_set_width(ui_widget, col_w * span + mGap * (span - 1));
        } else {
            lv_obj_set_width(ui_widget, col_w);
        }
        if (height > 0) lv_obj_set_height(ui_widget, height);

        if (childTitleIndex > 0) {
            lv_label_set_text(ui_comp_get_child(ui_widget, childTitleIndex), title);
        }

        // ── Icon / title positioning ──────────────────────────────────────────
        // Supported values: "top" (default), "left", "right", "bottom"
        // The title label doubles as the icon container (shows LVGL symbol or title text).
        {
            const char* iconPos = uJSON::getString(item, "icon_pos", "top");
            lv_obj_t* titleObj = childTitleIndex > 0 ? ui_comp_get_child(ui_widget, childTitleIndex) : nullptr;

            if (titleObj && iconPos && strcmp(iconPos, "top") != 0) {
                if (strcmp(iconPos, "bottom") == 0) {
                    // Move title label to last position (after content children)
                    lv_obj_move_to_index(titleObj, -1);
                    // Change border side to top
                    lv_obj_set_style_border_side(titleObj, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(titleObj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_top(titleObj, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
                } else if (strcmp(iconPos, "left") == 0 || strcmp(iconPos, "right") == 0) {
                    // Switch widget flex to horizontal row layout
                    lv_obj_set_flex_flow(ui_widget, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(ui_widget, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
                    // Title: shrink to content, full height, right border as separator
                    lv_obj_set_width(titleObj, LV_SIZE_CONTENT);
                    lv_obj_set_height(titleObj, lv_pct(100));
                    lv_obj_set_style_border_side(titleObj, LV_BORDER_SIDE_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(titleObj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_right(titleObj, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_align(titleObj, LV_ALIGN_LEFT_MID, LV_PART_MAIN | LV_STATE_DEFAULT);
                    // All content children: flex_grow so they fill remaining space
                    int32_t childCount = lv_obj_get_child_count(ui_widget);
                    for (int32_t ci = 0; ci < childCount; ci++) {
                        lv_obj_t* ch = lv_obj_get_child(ui_widget, ci);
                        if (ch == titleObj) continue;
                        lv_obj_set_style_flex_grow(ch, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                        lv_obj_set_width(ch, LV_SIZE_CONTENT);
                    }
                    if (strcmp(iconPos, "right") == 0) {
                        // Move title to last (right side)
                        lv_obj_move_to_index(titleObj, -1);
                        lv_obj_set_style_border_side(titleObj, LV_BORDER_SIDE_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
                        lv_obj_set_style_pad_right(titleObj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                        lv_obj_set_style_pad_left(titleObj, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
                    }
                }
            }
        }

        WidgetCard::applyItemColor(ui_widget, item);
        uUI::attachJsonData(ui_widget, item);
    }

    if (masonryMode == 2) {
        // Register a one-shot handler that fires AFTER the next lv_obj_update_layout()
        // completes, so masonry_layout_cb() sees correct widget heights.
        // Cancel any pending handler from a previous rapid profile switch first.
        lv_display_t* disp = lv_obj_get_display(ui_cntWidgets);
        lv_display_remove_event_cb_with_user_data(disp, masonry_layout_done_cb, ui_cntWidgets);
        lv_display_add_event_cb(disp, masonry_layout_done_cb,
                                LV_EVENT_UPDATE_LAYOUT_COMPLETED, ui_cntWidgets);
    }
    // Modes 0/1: LVGL resolves flex layout automatically in the next lv_timer_handler().

    _ui_disable_mutex(1);
    // cachedList freed automatically by cJsonPtr at scope exit
}

void Widgets::navigateToScene(const char* sceneId)
{
    _currentWidgetSceneId = sceneId ? sceneId : "";
    renderForScene(_currentWidgetSceneId.c_str());

    // Fetch fresh data for all newly visible widgets
    updateList();
}

void Widgets::navigateBack()
{
    navigateToScene(_widgetRootSceneId.c_str());
}

void Widgets::setRootScene(const char* sceneId)
{
    std::string newId = sceneId ? sceneId : "";
    if (_widgetRootSceneId == newId && _currentWidgetSceneId == newId) return;
    _widgetRootSceneId = newId;
    navigateToScene(newId.c_str());
}

void Widgets::setActiveProfileId(const char* profileId)
{
    _activeProfileId = profileId ? profileId : "";
}

void Widgets::init()
{
    HeapProbe _probe("widgets.load");
    loadCounterStates();
    // Init only the WS queue at boot — companion-proxy widgets feed through it.
    // HTTP worker spawns lazily via ensureHttpWorkers() on first direct URL fetch.
    ensureWsQueue();

    String listStr = "[]";
    if (LittleFS.exists(WIDGETS_FILE)) {
        File f = LittleFS.open(WIDGETS_FILE, "r");
        if (f) {
            listStr = f.readString();
            f.close();
            ESP_LOGI("Widgets", "Loaded %u bytes from " WIDGETS_FILE, (unsigned)listStr.length());
        }
    } else {
#ifdef BOARD_HAS_TF
        if (uSD::isAvailable() && uSD::getFS().exists(WIDGETS_FILE_SD)) {
            File sf = uSD::getFS().open(WIDGETS_FILE_SD, FILE_READ);
            if (sf) {
                listStr = sf.readString();
                sf.close();
                ESP_LOGI("Widgets", "Loaded %u bytes from SD " WIDGETS_FILE_SD, (unsigned)listStr.length());
            }
        }
        if (listStr == "[]")
#endif
        {
            uMemory::read("widgets", [&listStr](Preferences preferences) {
                if (preferences.isKey("items")) listStr = preferences.getString("items");
            });
            ESP_LOGI("Widgets", "Loaded from NVS (legacy) %u bytes", (unsigned)listStr.length());
        }
    }

    cJsonPtr list = cJsonParse(listStr.c_str());
    if (!list) {
        ESP_LOGE("Widgets", "cJSON_Parse failed -- loading empty widget list (stored JSON may be corrupt)");
        list = cJsonCreateArray();
        if (!list) { ESP_LOGE("Widgets", "cJSON_CreateArray OOM -- widgets will be empty"); return; }
    }
    set(list.get(), false);
    // list freed automatically by cJsonPtr at scope exit

    // Scroll-perf: hide bg during scroll, restore on end.
    lv_obj_add_event_cb(ui_cntWidgets, onWidgetsScrollBegin, LV_EVENT_SCROLL_BEGIN, nullptr);
    lv_obj_add_event_cb(ui_cntWidgets, onWidgetsScrollEnd,   LV_EVENT_SCROLL_END,   nullptr);

    // Tab-switch horizontal scroll — also triggers bg hide/restore for widgets.
    lv_obj_t* tvContent = lv_tabview_get_content(ui_tabsMain);
    if (tvContent) {
        lv_obj_add_event_cb(tvContent, onWidgetsScrollBegin, LV_EVENT_SCROLL_BEGIN, nullptr);
        lv_obj_add_event_cb(tvContent, onWidgetsScrollEnd,   LV_EVENT_SCROLL_END,   nullptr);
    }

    // First render above ran before LVGL had its first paint of ui_screenMain
    // — ui_cntWidgets dimensions reported by lv_obj_get_width() were stale,
    // leading to slight horizontal offset that only resolves on user scroll.
    // Schedule a deferred re-render so the second pass uses settled dimensions.
    rerenderDeferred();

    // Tabview content scroll_x was computed during settings.load() when widget
    // tab was empty — content height/width changed after widgets were added,
    // leaving scroll_x off by a few pixels (visible as a slight horizontal
    // shift on first paint, which a user scroll re-snaps). Re-snap after a
    // short delay (not lv_async_call — async callbacks run BEFORE layout update
    // in the same tick, so the snap position gets overridden by the layout pass
    // that follows). A 300ms one-shot timer fires after LVGL has completed
    // several full render+layout cycles and the scroll extent is stable.
    lv_timer_t* snapTimer = lv_timer_create([](lv_timer_t*) {
        uint32_t cur = lv_tabview_get_tab_active(ui_tabsMain);
        lv_tabview_set_active(ui_tabsMain, cur, LV_ANIM_OFF);
    }, 300, nullptr);
    lv_timer_set_repeat_count(snapTimer, 1);  // auto_delete=true fires once then frees
}

// ── Widget rebuild callback — runs on LVGL task (Core 1) via lv_async_call ──
// Snapshots old values, clears timers, re-renders, sets up new timers, restores data.
// All LVGL object access is safe here because we're inside lv_timer_handler().
static void _widgetRebuildCb(void*)
{
    // Guard: under heavy heap fragmentation (e.g. after many HTTP requests +
    // service-mode cycles on no-PSRAM boards) the std::string snap copy,
    // cJSON parse tree, and LVGL lv_malloc calls below can collectively
    // exhaust the largest contiguous free block and throw std::bad_alloc
    // or trigger an LVGL assertion — both crash the LVGL task.
    // Skip the rebuild if the largest allocatable block is below a safe
    // floor; the widgets will be rebuilt on the next trigger (e.g. exit
    // service-mode or next armTimersOnly cycle).
    // 16 KB threshold covers: 2× std::string JSON copies (~800 B each),
    // 2× cJSON parse trees (~800 B each), LVGL widget creation (~1500 B/widget
    // × up to 4), timer setup, and std containers — with margin for concurrent
    // allocs from lwIP / WiFi driver on Core 0.
    if (ESP.getMaxAllocHeap() < 16384) {
        ESP_LOGW("Widgets", "_widgetRebuildCb: heap fragmented (max_alloc=%u B) — skipping rebuild",
                 (unsigned)ESP.getMaxAllocHeap());
        return;
    }

    // Lock-snapshot the cache so HTTP-side save cannot free it during parse.
    std::string snap;
    {
        WidgetLock _wlk;
        if (!_cachedWidgetJson) return;
        snap = _cachedWidgetJson;
    }
    cJsonPtr list = cJsonParse(snap.c_str());
    if (!list) return;

    _ui_enable_mutex(2);

    // Snapshot currently displayed values keyed by widget id, before destroying objects
    std::map<std::string, std::string> prevData;
    {
        std::vector<lv_obj_t*> visible;
        collectWidgets(ui_cntWidgets, visible);
        for (lv_obj_t* w : visible) {
            cJsonPtr data = cJsonPtr(uUI::getObjectDataJSON(w), cJSON_Delete);
            if (!data) continue;
            const char* wid   = uJSON::getString(data.get(), "id");
            const char* wtype = uJSON::getString(data.get(), "type");
            if (wid && wtype) {
                if (strcmp(wtype, MACROS_TYPE_SCENE) == 0 ||
                    strcmp(wtype, MACROS_TYPE_BACK)  == 0) {
                    continue;
                }
                const char* val = nullptr;
                char buf[16];
                if (strcmp(wtype, WIDGET_TYPE_TEXT) == 0) {
                    lv_obj_t* lbl = ui_comp_get_child(w, UI_COMP_WIDGET_TEXT_CONTENT);
                    if (lbl) val = lv_label_get_text(lbl);
                } else if (strcmp(wtype, WIDGET_TYPE_PROGRESS) == 0) {
                    int style = uJSON::getInt(data.get(), "style");
                    if (style == WIDGET_STYLE_PROGRESS_ARC) {
                        lv_obj_t* arc = ui_comp_get_child(w, UI_COMP_WIDGET_ARC_PROGRESS);
                        if (arc) { snprintf(buf, sizeof(buf), "%d", (int)lv_arc_get_value(arc)); val = buf; }
                    } else if (style == WIDGET_STYLE_PROGRESS_HORIZONTAL) {
                        lv_obj_t* bar = ui_comp_get_child(w, UI_COMP_WIDGET_PROGRESS_CONTENT);
                        if (bar) { snprintf(buf, sizeof(buf), "%d", (int)lv_bar_get_value(bar)); val = buf; }
                    }
                }
                if (val && val[0] != '\0' && strcmp(val, "-") != 0) {
                    prevData[wid] = val;
                }
            }
            // data freed automatically by cJsonPtr at end of loop iteration
        }
    }

    // Clear timers
    mTimers::clearByType("widget");
    mTimers::clearByType("timer_tick");
    _timerRemaining.clear();

    // Render current scene (mutex 1 calls inside are no-ops while mutex 2 is held)
    Widgets::renderForScene(_currentWidgetSceneId.c_str());

    _ui_disable_mutex(2);

    // Set up timers for ALL widgets (including those not currently visible)
    cJSON* widgetJSON = NULL;
    cJSON_ArrayForEach(widgetJSON, list.get()) {
        const char* widgetId = uJSON::getString(widgetJSON, "id");
        const char* type     = uJSON::getString(widgetJSON, "type");
        if (!type || strcmp(type, MACROS_TYPE_SCENE) == 0 ||
                     strcmp(type, MACROS_TYPE_BACK)  == 0) continue;
        int updateInterval = uJSON::getInt(widgetJSON, "update", 0);
        ESP_LOGI("Widgets", "Timer for widget (%s): %is", widgetId, updateInterval);
        if (updateInterval > 0) {
            // Jitter the first fire across 0..updateInterval so N widgets with
            // identical update=X don't all burst at t=X and flood the WS/HTTP
            // paths simultaneously. esp_random() used (random() seed
            // undefined until randomSeed() is called).
            long jitter = (long)(esp_random() % (uint32_t)(updateInterval * 1000));
            mTimers::addJittered(jitter, updateInterval * 1000, "widget", widgetId,
                [](reactesp::RepeatReaction*, std::string, std::string objectId) {
                    Widgets::updateById(objectId.c_str());
                });
        }
    }

    // Restore cached data or fetch fresh data for visible widgets
    {
        std::vector<lv_obj_t*> visible;
        collectWidgets(ui_cntWidgets, visible);
        for (lv_obj_t* widget : visible) {
            cJsonPtr data = cJsonPtr(uUI::getObjectDataJSON(widget), cJSON_Delete);
            if (!data) continue;
            const char* wid   = uJSON::getString(data.get(), "id");
            const char* wtype = uJSON::getString(data.get(), "type");
            if (wtype && (strcmp(wtype, MACROS_TYPE_SCENE)   == 0 ||
                          strcmp(wtype, MACROS_TYPE_BACK)    == 0 ||
                          strcmp(wtype, WIDGET_TYPE_COUNTER) == 0 ||
                          strcmp(wtype, WIDGET_TYPE_TIMER)   == 0)) {
                continue;
            }
            if (wid) {
                auto it = prevData.find(wid);
                if (it != prevData.end()) {
                    Widgets::renderData(wid, it->second.c_str());
                } else {
                    Widgets::update(widget);
                }
            }
            // data freed automatically by cJsonPtr at end of loop iteration
        }
    }
    // list freed automatically by cJsonPtr at scope exit
}

// Fast path for service-mode exit when no widget edits happened. Re-arms
// the per-widget update timers (which were cleared on service-mode entry)
// from the cached widget JSON, WITHOUT destroying or recreating any LVGL
// objects. Compare to _widgetRebuildCb above which always lv_obj_clean +
// recreates the entire widget container, fragmenting DRAM.
void Widgets::armTimersOnly()
{
    HeapProbe _probe("widgets.armTimersOnly");
    std::string snap;
    {
        WidgetLock _wlk;
        if (!_cachedWidgetJson) return;
        snap = _cachedWidgetJson;
    }
    cJsonPtr list = cJsonParse(snap.c_str());
    if (!list) return;

    mTimers::clearByType("widget");
    mTimers::clearByType("timer_tick");
    _timerRemaining.clear();

    cJSON* widgetJSON = NULL;
    cJSON_ArrayForEach(widgetJSON, list.get()) {
        const char* widgetId = uJSON::getString(widgetJSON, "id");
        const char* type     = uJSON::getString(widgetJSON, "type");
        if (!type || strcmp(type, MACROS_TYPE_SCENE) == 0 ||
                     strcmp(type, MACROS_TYPE_BACK)  == 0) continue;
        int updateInterval = uJSON::getInt(widgetJSON, "update", 0);
        if (updateInterval > 0) {
            long jitter = (long)(esp_random() % (uint32_t)(updateInterval * 1000));
            mTimers::addJittered(jitter, updateInterval * 1000, "widget", widgetId,
                [](reactesp::RepeatReaction*, std::string, std::string objectId) {
                    Widgets::updateById(objectId.c_str());
                });
        }
    }
}

void Widgets::set(cJSON *list, bool isSave)
{
    HeapProbe _probe("widgets.save");
    if (list == nullptr || !cJSON_IsArray(list)) {
        ESP_LOGE("Widgets", "Invalid widgets list (null or non-array)!");
        return;
    }
    // Empty array is valid — means "clear all widgets"

    // ── Validate / coerce data_target ────────────────────────────────────
    // Unknown data_target values fail silently in update() (widget never
    // refreshes). Coerce unknowns to "url_system" (safe default — routes
    // via companion/proxy, no direct HTTP on ESP) and log the fix so the
    // author notices. Also catches typos from hand-edited imports.
    int widgetCount = cJSON_GetArraySize(list);
    for (int i = 0; i < widgetCount; i++) {
        cJSON* w = cJSON_GetArrayItem(list, i);
        if (!w) continue;
        cJSON* dtItem = cJSON_GetObjectItem(w, "data_target");
        if (!dtItem) continue;  // widget without data_target (e.g. SCENE) is OK
        const char* dt = cJSON_GetStringValue(dtItem);
        if (!dt) continue;
        bool valid = (strcmp(dt, WIDGET_DATA_TARGET_URL) == 0)
                  || (strcmp(dt, WIDGET_DATA_TARGET_URL_SYSTEM) == 0)
                  || (strcmp(dt, WIDGET_DATA_TARGET_SYSTEM) == 0);
        if (!valid) {
            const char* wid = cJSON_GetStringValue(cJSON_GetObjectItem(w, "id"));
            ESP_LOGW("Widgets", "Invalid data_target '%s' on widget %s — coercing to '%s'",
                     dt, wid ? wid : "?", WIDGET_DATA_TARGET_URL_SYSTEM);
            cJSON_ReplaceItemInObject(w, "data_target",
                cJSON_CreateString(WIDGET_DATA_TARGET_URL_SYSTEM));
        }
    }

    // Serialize once; reuse for NVS save and PSRAM cache
    cJsonStrPtr jsonStrPtr = cJsonPrint(list);
    if (!jsonStrPtr) { log_e("widgets: cJsonPrint OOM"); return; }
    const char* jsonStr = jsonStrPtr.get();

    if (isSave) {
        // Mark widgets dirty so exitServiceMode performs the full LVGL rebuild
        // (otherwise it takes the fast-path armTimersOnly which assumes the
        // current LVGL widget tree still matches the saved JSON).
        if (WiFiModule::WSServer::isServiceMode())
            WiFiModule::WSServer::markWidgetsDirty();
        File f = LittleFS.open(WIDGETS_FILE, "w");
        if (f) {
            size_t written = f.print(jsonStr);
            f.close();
            ESP_LOGI("Widgets", "Saved %u bytes to " WIDGETS_FILE, (unsigned)written);
        } else {
            ESP_LOGE("Widgets", "Failed to open " WIDGETS_FILE " for writing");
        }
#ifdef BOARD_HAS_TF
        if (uSD::isAvailable()) {
            File sf = uSD::getFS().open(WIDGETS_FILE_SD, FILE_WRITE);
            if (sf) { sf.print(jsonStr); sf.close(); }
        }
#endif
    }

    // Cache as PSRAM-backed string — avoids permanently occupying internal SRAM
    // which the display DMA driver needs for byteswap buffers (up to 64 KB each flush).
    // Swap order: prepare the fresh buffer first, then atomically swap the pointer,
    // then free the old one. This closes the UAF window that existed when readers
    // (getCachedJson() → cJSON_Parse() called from the HTTP server task without
    // taking _widget_state_mutex) could be mid-parse when the old buffer was freed.
    {
        size_t len = strlen(jsonStr) + 1;
        char* fresh = (char*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!fresh) fresh = (char*)malloc(len);
        if (fresh) memcpy(fresh, jsonStr, len);

        WidgetLock _wlk;
        char* old = _cachedWidgetJson;
        _cachedWidgetJson = fresh;          // publish (or null if alloc failed)
        free(old);                          // old buffer is safe to free once the pointer no longer references it
        if (!fresh) {
            ESP_LOGE("Widgets", "Cache alloc failed (%u B) — next render will use empty list", (unsigned)len);
        }
    }
    // jsonStrPtr freed automatically at scope exit

    if (isSave) {
        // Called from HTTP task (AsyncTCP, Core 0) — defer all LVGL work to Core 1
        // via lv_async_call to avoid cross-task use-after-free crashes.
        lv_async_call(_widgetRebuildCb, nullptr);
        uSettings::bumpStateVersion();
    } else {
        // Called during init — LVGL task not running yet, safe to run directly.
        _widgetRebuildCb(nullptr);
    }
}

bool Widgets::update(lv_obj_t *widget)
{
    cJsonPtr json = cJsonPtr(uUI::getObjectDataJSON(widget), cJSON_Delete);
    if (!json) {
        return false;
    }

    const char* widgetType = uJSON::getString(json.get(), "type");
    const char* widgetId   = uJSON::getString(json.get(), "id");

    // Scene navigation buttons — handle click, don't fetch data (always allowed,
    // even in service mode, so on-device touch nav still works).
    if (widgetType && strcmp(widgetType, MACROS_TYPE_BACK) == 0) {
        navigateBack();
        return true;
    }
    if (widgetType && strcmp(widgetType, MACROS_TYPE_SCENE) == 0) {
        const char* targetId = uJSON::getString(json.get(), "target_id");
        std::string dest = (targetId && targetId[0] != '\0') ? targetId : (widgetId ? widgetId : "");
        navigateToScene(dest.c_str());
        return true;
    }

    // Counter click — increment
    if (widgetType && strcmp(widgetType, WIDGET_TYPE_COUNTER) == 0) {
        int step = uJSON::getInt(json.get(), "step", 1);
        int maxV = uJSON::getInt(json.get(), "max", 99);
        int cur = 0;
        if (widgetId) {
            auto it = _counterValues.find(widgetId);
            if (it != _counterValues.end()) cur = it->second;
        }
        int next = cur + step;
        if (next > maxV) next = maxV;
        if (widgetId) saveCounterState(widgetId, next);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", next);
        _ui_enable_mutex(1);
        lv_label_set_text(ui_comp_get_child(widget, UI_COMP_WIDGET_TEXT_CONTENT), buf);
        _ui_disable_mutex(1);
        return true;
    }

    // Timer click — start/stop toggle
    if (widgetType && strcmp(widgetType, WIDGET_TYPE_TIMER) == 0) {
        int duration = uJSON::getInt(json.get(), "duration", 60);
        std::string sid = widgetId ? widgetId : "";
        auto it = _timerRemaining.find(sid);
        bool isRunning = (it != _timerRemaining.end() && it->second >= 0);

        if (isRunning) {
            _timerRemaining[sid] = -1;
            mTimers::remove("timer_tick", sid.c_str());
            _ui_enable_mutex(1);
            lv_label_set_text(ui_comp_get_child(widget, UI_COMP_WIDGET_TEXT_CONTENT), "Tap to start");
            _ui_disable_mutex(1);
        } else {
            _timerRemaining[sid] = duration;
            char buf[16];
            formatTimerText(buf, sizeof(buf), duration);
            _ui_enable_mutex(1);
            lv_label_set_text(ui_comp_get_child(widget, UI_COMP_WIDGET_TEXT_CONTENT), buf);
            _ui_disable_mutex(1);

            mTimers::add(1000, "timer_tick", sid.c_str(),
                [](reactesp::RepeatReaction*, std::string, std::string id) {
                    auto it2 = _timerRemaining.find(id);
                    if (it2 == _timerRemaining.end() || it2->second < 0) {
                        mTimers::remove("timer_tick", id.c_str());
                        return;
                    }
                    it2->second--;

                    lv_obj_t* w = uUI::getObjectById(ui_cntWidgets, id.c_str());
                    if (!w) {
                        mTimers::remove("timer_tick", id.c_str());
                        return;
                    }

                    _ui_enable_mutex(1);
                    if (it2->second <= 0) {
                        it2->second = -1;
                        cJsonPtr data = cJsonPtr(uUI::getObjectDataJSON(w), cJSON_Delete);
                        const char* doneUrl = data ? uJSON::getString(data.get(), "done_url") : nullptr;
                        std::string doneUrlCopy = doneUrl ? doneUrl : "";
                        lv_label_set_text(ui_comp_get_child(w, UI_COMP_WIDGET_TEXT_CONTENT), "Tap to start");
                        _ui_disable_mutex(1);
                        if (!doneUrlCopy.empty()) {
                            Helpers::updateDataFromUrl(doneUrlCopy.c_str(), nullptr,
                                [](int, const char*) -> bool { return true; });
                        }
                        mTimers::remove("timer_tick", id.c_str());
                    } else {
                        char buf[16];
                        formatTimerText(buf, sizeof(buf), it2->second);
                        lv_label_set_text(ui_comp_get_child(w, UI_COMP_WIDGET_TEXT_CONTENT), buf);
                        _ui_disable_mutex(1);
                    }
                }
            );
        }
        return true;
    }

    // Skip background data refresh when user is editing (service mode),
    // when the device is actively serving the web UI, or when an SD write
    // is in flight. Touch-driven actions above (nav / counter / timer toggle)
    // already returned; only data polls reach here, so it's safe to bail.
    // Suppresses WS proxy frames + direct HTTP jobs alike.
    if (WiFiModule::WSServer::isServiceMode() ||
        WiFiModule::WSServer::isWebServing() ||
        uSD::isSdServing()) {
        return false;
    }

    // URL-fetching widget types require WiFi
    if (WiFiModule::Helper::getState() != WiFiModule::State::Connected) {
        ESP_LOGE("Widgets", "WiFi not connected!");
        setWidgetError(widget, true);
        return false;
    }

    // Bail before any cJSON alloc / pushLogEvent / std::map insert when no
    // companion or proxy is available to actually fetch the URL. The downstream
    // get_url path returns "skipping" without sending, but the path up to that
    // check still allocates a logMsg cJSON tree, an optional parseConfig tree,
    // and inserts into _pendingRequest. Over an hour of idle polls these
    // alloc/free pairs fragment the DRAM heap (largest_free_block can drop
    // from ~28 KB to ~1.5 KB on no-PSRAM Rv2) to the point where TCP can no
    // longer allocate pbufs for incoming connections. Bail early — there is
    // literally nothing useful to do for url-based widgets in that state.
    if (WiFiModule::WSServer::getCompanionClientId() == 0 &&
        WiFiModule::WSServer::getProxyClientId()     == 0) {
        // System metric widgets without URL still need the proxy/companion;
        // skip uniformly. Counter / Timer widget types short-circuited above.
        return false;
    }

    const char* dataTarget = uJSON::getString(json.get(), "data_target");
    const char* url = uJSON::getString(json.get(), "url");
    cJSON* headers = cJSON_GetObjectItem(json.get(), "headers");

    ESP_LOGI("Widgets", "widgetId: %s, widgetType: %s, dataTarget: %s", widgetId, widgetType, dataTarget);

    bool state = false;

    if (!dataTarget) {
        ESP_LOGE("Widgets", "Missing data_target for widget %s!", widgetId ? widgetId : "?");
    } else if (strcmp(dataTarget, WIDGET_DATA_TARGET_SYSTEM) == 0) {
        // Companion system metric — no URL required
        const char* metricId = uJSON::getString(json.get(), "metric_id");
        if (metricId && strlen(metricId) > 0) {
            state = WiFiModule::API::getSystemInfo(0, "widget", widgetId, metricId);
            if (state && widgetId) { WidgetLock _wlk; _pendingRequest[widgetId] = millis(); }
        } else {
            ESP_LOGE("Widgets", "data_target=system but metric_id missing for widget %s", widgetId ? widgetId : "?");
        }
    } else if (!url || strlen(url) == 0) {
        ESP_LOGE("Widgets", "URL is empty!");
    } else {
        // Push to the polling log buffer (browser polls GET /api/log instead of WS).
        // Zero-alloc: build JSON directly into static _wLogBuf via snprintf+escape.
        {
            ensureWLogMux();
            xSemaphoreTake(_wLogMux, portMAX_DELAY);
            char* p = _wLogBuf;
            char* e = _wLogBuf + sizeof(_wLogBuf);
            _wLogAppendLit(p, e, "{\"action\":\"widget_update\",\"id\":\"");
            _wLogAppendEscaped(p, e, widgetId ? widgetId : "");
            _wLogAppendLit(p, e, "\",\"data_target\":\"");
            _wLogAppendEscaped(p, e, dataTarget);
            _wLogAppendLit(p, e, "\",\"url\":\"");
            _wLogAppendEscaped(p, e, url);
            _wLogAppendLit(p, e, "\"}");
            if (p < e) *p = '\0'; else _wLogBuf[sizeof(_wLogBuf) - 1] = '\0';
            WiFiModule::WSServer::pushLogEvent(_wLogBuf);
            xSemaphoreGive(_wLogMux);
        }

        // Extract parse fields as raw pointers into the existing widget JSON
        // tree — zero allocation. getUrl builds the ,"parse":{...} block
        // inline into its static buffer (replaces prior cJSON tree + Duplicate
        // pattern that allocated ~5-8 nodes per widget update tick).
        const char* parseType     = uJSON::getString(json.get(), "parse_type");
        const char* parseTemplate = uJSON::getString(json.get(), "template");
        const char* parseRegex    = uJSON::getString(json.get(), "regex");
        cJSON*      jsonKeys      = cJSON_GetObjectItem(json.get(), "json_keys");

        if (strcmp(dataTarget, WIDGET_DATA_TARGET_URL) == 0) {
            state = updateDataFromUrl(widgetId, url, headers,
                                      parseType, parseTemplate, parseRegex, jsonKeys);
        } else if (strcmp(dataTarget, WIDGET_DATA_TARGET_URL_SYSTEM) == 0) {
            state = updateDataFromUrlBySystem(widgetId, url, headers,
                                              parseType, parseTemplate, parseRegex, jsonKeys);
        } else {
            ESP_LOGE("Widgets", "Wrong data target - '%s'", dataTarget);
        }
        if (state && widgetId) { WidgetLock _wlk; _pendingRequest[widgetId] = millis(); }
    }

    // If the request could not be sent (no companion, no proxy, etc.)
    // show the error indicator immediately instead of waiting for a timeout.
    if (!state && widgetId) {
        lv_obj_t* w = uUI::getObjectById(ui_cntWidgets, widgetId);
        if (w) setWidgetError(w, true);
    }
    // json freed automatically by cJsonPtr at scope exit

    return state;
}

bool Widgets::updateById(const char* widgetId)
{
    lv_obj_t *widget = uUI::getObjectById(ui_cntWidgets, widgetId);

    if (widget == nullptr) {
        // Widget exists in config but not in the current scene — skip silently.
        // Do NOT remove the timer: the widget may become visible after a scene change.
        return false;
    }

    return update(widget);
}

bool Widgets::updateDataFromUrl(const char* widgetId, const char* url, cJSON* headers,
                                const char* parseType, const char* parseTemplate,
                                const char* parseRegex, cJSON* jsonKeys)
{
    // HTTPS requires TLS which is slow and memory-heavy on ESP32.
    // Prefer browser/companion proxy; optionally fall back to direct fetch.
    if (url && strncmp(url, "https://", 8) == 0) {
#ifdef WIDGET_HTTPS_DIRECT_FALLBACK
        if (updateDataFromUrlBySystem(widgetId, url, headers, parseType, parseTemplate, parseRegex, jsonKeys)) return true;
        ESP_LOGW("Widgets", "No proxy for HTTPS — direct fetch fallback for %s", widgetId);
        // fall through to HTTP worker below
#else
        ESP_LOGI("Widgets", "HTTPS URL — routing to browser/companion proxy");
        return updateDataFromUrlBySystem(widgetId, url, headers, parseType, parseTemplate, parseRegex, jsonKeys);
#endif
    }

    ensureHttpWorkers();

    auto* job = static_cast<HttpJob*>(malloc(sizeof(HttpJob)));
    if (!job) {
        ESP_LOGE("Widgets", "OOM for HTTP job, falling back to url_system");
        return updateDataFromUrlBySystem(widgetId, url, headers, parseType, parseTemplate, parseRegex, jsonKeys);
    }

    strncpy(job->widgetId, widgetId ? widgetId : "", sizeof(job->widgetId) - 1);
    job->widgetId[sizeof(job->widgetId) - 1] = '\0';
    strncpy(job->url, url, sizeof(job->url) - 1);
    job->url[sizeof(job->url) - 1] = '\0';
    job->headers = headers ? cJSON_Duplicate(headers, true) : nullptr;

    if (xQueueSend(_httpJobQueue, &job, 0) != pdTRUE) {
        // Queue full — drop silently rather than spawning another task
        if (job->headers) cJSON_Delete(job->headers);
        free(job);
        return true; // already have pending work in flight
    }

    return true; // result will arrive via _httpResultQueue → Widgets::loop()
}

bool Widgets::updateDataFromUrlBySystem(const char* widgetId, const char* url, cJSON* headers,
                                        const char* parseType, const char* parseTemplate,
                                        const char* parseRegex, cJSON* jsonKeys)
{
    return Helpers::updateDataFromUrlBySystem("widget", widgetId, url, headers,
                                              parseType, parseTemplate, parseRegex, jsonKeys);
}

bool Widgets::retryDirectHttp(const char* widgetId, const char* url)
{
    if (!widgetId || !url || strlen(url) == 0) return false;

#ifndef WIDGET_HTTPS_DIRECT_FALLBACK
    // HTTPS is too heavy for ESP32 (TLS memory + slow handshake) — don't retry
    if (strncmp(url, "https://", 8) == 0) {
        ESP_LOGW("Widgets", "Cannot retry HTTPS directly on ESP32: %s", widgetId);
        return false;
    }
#endif

    ensureHttpWorkers();

    auto* job = static_cast<HttpJob*>(malloc(sizeof(HttpJob)));
    if (!job) return false;

    strncpy(job->widgetId, widgetId, sizeof(job->widgetId) - 1);
    job->widgetId[sizeof(job->widgetId) - 1] = '\0';
    strncpy(job->url, url, sizeof(job->url) - 1);
    job->url[sizeof(job->url) - 1] = '\0';
    job->headers = nullptr;

    if (xQueueSend(_httpJobQueue, &job, 0) != pdTRUE) {
        free(job);
        return false;
    }

    ESP_LOGI("Widgets", "Retry direct HTTP for %s → %s", widgetId, url);
    { WidgetLock _wlk; _pendingRequest[widgetId] = millis(); }
    return true;
}

bool Widgets::renderUrlResult(const char* widgetId, int httpCode, const char* httpResponse, bool proxyApplied)
{
    lv_obj_t *widget = uUI::getObjectById(ui_cntWidgets, widgetId);

    if (widget == nullptr) {
        ESP_LOGE("Widgets", "Widget not found, id: %s", widgetId);

        return false;
    }

    cJsonPtr widgetData = cJsonPtr(uUI::getObjectDataJSON(widget), cJSON_Delete);

    if (!widgetData) {
        ESP_LOGE("Widgets", "Wrong widget data!");

        return false;
    }

    bool state = false;

    if (httpCode == HTTP_CODE_OK) {
        ESP_LOGI("Widgets", "[HTTP] GET... code: %d, resp_len: %d, proxy_applied: %d", httpCode,
                 httpResponse ? (int)strlen(httpResponse) : 0, (int)proxyApplied);

        std::string result;

        if (proxyApplied) {
            // Proxy/companion already extracted the value — use directly
            result = httpResponse ? httpResponse : "";
        } else {
            const char* tmplt     = uJSON::getString(widgetData.get(), "template");
            const char* parseType = uJSON::getString(widgetData.get(), "parse_type");

            if (parseType && strcmp(parseType, "json") == 0) {
                cJSON* jsonKeys = cJSON_GetObjectItem(widgetData.get(), "json_keys");
                ESP_LOGI("Widgets", "parse_type=json, template='%s', json_keys=%s",
                         tmplt ? tmplt : "(null)",
                         jsonKeys ? (cJSON_IsArray(jsonKeys) ? "array" : "not-array") : "(null)");
                result = Helpers::formatJsonResponse(tmplt, jsonKeys, httpResponse);
            } else {
                const char* regex = uJSON::getString(widgetData.get(), "regex");
                result = Helpers::formatUrlResponse(tmplt, regex, httpResponse);
            }
        }

        // Push to the polling log buffer (zero-alloc via static _wLogBuf).
        {
            ensureWLogMux();
            xSemaphoreTake(_wLogMux, portMAX_DELAY);
            char  codeStr[12];
            snprintf(codeStr, sizeof(codeStr), "%d", httpCode);
            char* p = _wLogBuf;
            char* e = _wLogBuf + sizeof(_wLogBuf);
            _wLogAppendLit(p, e, "{\"action\":\"widget_result\",\"id\":\"");
            _wLogAppendEscaped(p, e, widgetId);
            _wLogAppendLit(p, e, "\",\"code\":");
            _wLogAppendLit(p, e, codeStr);
            if (!result.empty()) {
                _wLogAppendLit(p, e, ",\"value\":\"");
                _wLogAppendEscaped(p, e, result.c_str());
                _wLogAppendLit(p, e, "\"}");
            } else {
                _wLogAppendLit(p, e, ",\"error\":\"parse returned empty (check regex/keys/template)\"}");
            }
            if (p < e) *p = '\0'; else _wLogBuf[sizeof(_wLogBuf) - 1] = '\0';
            WiFiModule::WSServer::pushLogEvent(_wLogBuf);
            xSemaphoreGive(_wLogMux);
        }

        state = renderData(widgetId, result.c_str());
        if (state) {
            _ui_enable_mutex(1);
            setWidgetError(widget, false);
            _ui_disable_mutex(1);
        }
    } else {
        ESP_LOGE("Widgets", "[HTTP] GET... failed, code: %d\n", httpCode);

        // Keep existing data — only show the error dot
        _ui_enable_mutex(1);
        setWidgetError(widget, true);
        _ui_disable_mutex(1);

        // Push to the polling log buffer (zero-alloc via static _wLogBuf).
        {
            ensureWLogMux();
            xSemaphoreTake(_wLogMux, portMAX_DELAY);
            char  codeStr[12];
            snprintf(codeStr, sizeof(codeStr), "%d", httpCode);
            char* p = _wLogBuf;
            char* e = _wLogBuf + sizeof(_wLogBuf);
            _wLogAppendLit(p, e, "{\"action\":\"widget_result\",\"id\":\"");
            _wLogAppendEscaped(p, e, widgetId);
            _wLogAppendLit(p, e, "\",\"code\":");
            _wLogAppendLit(p, e, codeStr);
            _wLogAppendLit(p, e, ",\"error\":\"HTTP request failed\"}");
            if (p < e) *p = '\0'; else _wLogBuf[sizeof(_wLogBuf) - 1] = '\0';
            WiFiModule::WSServer::pushLogEvent(_wLogBuf);
            xSemaphoreGive(_wLogMux);
        }

        state = false;
    }
    // widgetData freed automatically by cJsonPtr at scope exit

    return state;
}

bool Widgets::renderData(const char* widgetId, const char* content)
{
    lv_obj_t *widget = uUI::getObjectById(ui_cntWidgets, widgetId);

    if (widget == nullptr) {
        ESP_LOGE("Widgets", "Widget not found, id: %s", widgetId);

        return false;
    }

    cJsonPtr widgetData = cJsonPtr(uUI::getObjectDataJSON(widget), cJSON_Delete);

    if (!widgetData) {
        ESP_LOGE("Widgets", "Wrong widget data!");

        return false;
    }

    bool state = false;

    if (content && content[0] != '\0') {
        const char* widgetType = uJSON::getString(widgetData.get(), "type");

        ESP_LOGI("", "After replace: %s", content);

        {
        UiLock _ul;
        if (strcmp(widgetType, WIDGET_TYPE_TEXT) == 0) { // Text
            lv_label_set_text(
                ui_comp_get_child(widget, UI_COMP_WIDGET_TEXT_CONTENT),
                content
            );
            setWidgetError(widget, false);  // clear error indicator on successful data
            state = true;
        } else if (strcmp(widgetType, WIDGET_TYPE_CHART) == 0) { // Chart
            lv_obj_t *chart = ui_comp_get_child(widget, UI_COMP_WIDGET_CHART_CHART);
            if (chart) {
                lv_chart_series_t *ser = lv_chart_get_series_next(chart, nullptr);
                if (ser) {
                    lv_chart_set_next_value(chart, ser, (lv_coord_t)atoi(content));
                    lv_chart_refresh(chart);
                    setWidgetError(widget, false);
                    state = true;
                }
            }
        } else if (strcmp(widgetType, WIDGET_TYPE_PROGRESS) == 0) { // Progress
            int widgetStyle = uJSON::getInt(widgetData.get(), "style");

            if (widgetStyle == WIDGET_STYLE_PROGRESS_HORIZONTAL) {
                lv_bar_set_value(
                    ui_comp_get_child(widget, UI_COMP_WIDGET_PROGRESS_CONTENT),
                    atoi(content),
                    LV_ANIM_OFF
                );
                setWidgetError(widget, false);
                state = true;
            } else if (widgetStyle == WIDGET_STYLE_PROGRESS_VERTICAL) {
                setWidgetError(widget, false);
                state = true;
            } else if (widgetStyle == WIDGET_STYLE_PROGRESS_ARC) {
                lv_obj_t *arc = ui_comp_get_child(widget, UI_COMP_WIDGET_ARC_PROGRESS);
                if (arc) lv_arc_set_value(arc, atoi(content));

                // caption is a child of arc — iterate to find the label safely
                if (arc) {
                    uint32_t n = lv_obj_get_child_cnt(arc);
                    for (uint32_t ci = 0; ci < n; ci++) {
                        lv_obj_t *child = lv_obj_get_child(arc, ci);
                        if (child && lv_obj_check_type(child, &lv_label_class)) {
                            lv_label_set_text(child, content);
                            break;
                        }
                    }
                }
                setWidgetError(widget, false);
                state = true;
            }
        }

        } // UiLock released
    } else {
        ESP_LOGI("Widgets", "No match found");
    }
    // widgetData freed automatically by cJsonPtr at scope exit

    return state;
}

bool Widgets::updateList()
{
    if (WiFiModule::Helper::getState() != WiFiModule::State::Connected) {
        ESP_LOGE("Widgets", "WiFi not connected!");
        return false;
    }

    // No outer mutex — update() is non-blocking (spawns an HTTP task).
    // The LVGL mutex is taken only for the brief renderData() call.
    std::vector<lv_obj_t*> visible;
    collectWidgets(ui_cntWidgets, visible);
    for (lv_obj_t* widget : visible) {
        cJsonPtr d = cJsonPtr(uUI::getObjectDataJSON(widget), cJSON_Delete);
        const char* t = d ? uJSON::getString(d.get(), "type") : nullptr;
        bool isNav = t && (strcmp(t, MACROS_TYPE_SCENE) == 0 || strcmp(t, MACROS_TYPE_BACK) == 0);
        if (isNav) continue;
        update(widget);
    }

    return true;
}

void Widgets::counterReset(lv_obj_t* widget)
{
    cJsonPtr json = cJsonPtr(uUI::getObjectDataJSON(widget), cJSON_Delete);
    if (!json) return;
    const char* widgetType = uJSON::getString(json.get(), "type");
    if (!widgetType || strcmp(widgetType, WIDGET_TYPE_COUNTER) != 0) {
        return;
    }
    const char* widgetId = uJSON::getString(json.get(), "id");
    int minVal = uJSON::getInt(json.get(), "min", 0);
    if (widgetId) saveCounterState(widgetId, minVal);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", minVal);
    _ui_enable_mutex(1);
    lv_label_set_text(ui_comp_get_child(widget, UI_COMP_WIDGET_TEXT_CONTENT), buf);
    _ui_disable_mutex(1);
    // json freed automatically by cJsonPtr at scope exit
}

void Widgets::loop()
{
    if (!_httpResultQueue) return;

    bool anyRendered = false;  // tracks whether any widget content changed this tick

    HttpResult r;
    while (xQueueReceive(_httpResultQueue, &r, 0) == pdTRUE) {
        { WidgetLock _wlk; _pendingRequest.erase(r.widgetId); }
        if (renderUrlResult(r.widgetId, r.code, r.response)) anyRendered = true;
        free(r.response);
    }

    // Drain WebSocket results queued by async_tcp context (onReturnUrlResponse,
    // onReturnSystemInfo).  All LVGL calls happen here in loopTask, never inside
    // the async_tcp task, so they can't race with lv_timer_handler's layout pass.
    if (_wsResultQueue) {
        WsResult wr;
        while (xQueueReceive(_wsResultQueue, &wr, 0) == pdTRUE) {
            { WidgetLock _wlk; _pendingRequest.erase(wr.widgetId); }
            bool rendered = wr.isUrl ? renderUrlResult(wr.widgetId, wr.code, wr.data, wr.proxyApplied)
                                     : renderData(wr.widgetId, wr.data);
            if (rendered) anyRendered = true;
            free(wr.data);
        }
    }

    // In masonry mode 2 (absolute positioning), widget height changes caused by
    // content updates (text wrapping differently) don't trigger automatic reflow.
    // Re-register the one-shot layout handler so masonry is recalculated once
    // the next lv_obj_update_layout() completes.
    if (anyRendered && uSettings::getMasonryStyle() == 2) {
        lv_display_t* disp = lv_obj_get_display(ui_cntWidgets);
        if (disp) {
            lv_display_remove_event_cb_with_user_data(disp, masonry_layout_done_cb, ui_cntWidgets);
            lv_display_add_event_cb(disp, masonry_layout_done_cb,
                                    LV_EVENT_UPDATE_LAYOUT_COMPLETED, ui_cntWidgets);
        }
    }

    // Check for timed-out proxy/companion requests — show error dot on widgets
    // that never received a response within WIDGET_RESPONSE_TIMEOUT.
    // Lock is held across the full iterate+erase so HTTP worker task cannot
    // invalidate the iterator via enqueueUrlResult / enqueueWidgetResult.
    {
        WidgetLock _wlk;
        if (!_pendingRequest.empty()) {
            uint32_t now = millis();
            for (auto it = _pendingRequest.begin(); it != _pendingRequest.end(); ) {
                if (now - it->second > WIDGET_RESPONSE_TIMEOUT) {
                    ESP_LOGW("Widgets", "Widget %s response timeout (%lus)", it->first.c_str(),
                             (unsigned long)(WIDGET_RESPONSE_TIMEOUT / 1000));
                    lv_obj_t* w = uUI::getObjectById(ui_cntWidgets, it->first.c_str());
                    if (w) setWidgetError(w, true);
                    it = _pendingRequest.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
}

void Widgets::enqueueUrlResult(const char* widgetId, int code, const char* response, bool proxyApplied)
{
    if (!_wsResultQueue || !widgetId) return;
    { WidgetLock _wlk; _pendingRequest.erase(widgetId); } // response arrived — cancel timeout
    WsResult wr;
    strncpy(wr.widgetId, widgetId, sizeof(wr.widgetId) - 1);
    wr.widgetId[sizeof(wr.widgetId) - 1] = '\0';
    wr.code         = code;
    wr.data         = response ? strdup(response) : strdup("");
    wr.isUrl        = true;
    wr.proxyApplied = proxyApplied;
    if (xQueueSend(_wsResultQueue, &wr, 0) != pdTRUE) {
        free(wr.data); // queue full, drop
    }
}

void Widgets::enqueueData(const char* widgetId, const char* data)
{
    if (!_wsResultQueue || !widgetId) return;
    { WidgetLock _wlk; _pendingRequest.erase(widgetId); } // response arrived — cancel timeout
    WsResult wr;
    strncpy(wr.widgetId, widgetId, sizeof(wr.widgetId) - 1);
    wr.widgetId[sizeof(wr.widgetId) - 1] = '\0';
    wr.code   = 200;
    wr.data   = data ? strdup(data) : strdup("");
    wr.isUrl  = false;
    if (xQueueSend(_wsResultQueue, &wr, 0) != pdTRUE) {
        free(wr.data); // queue full, drop
    }
}

void Widgets::rerender()
{
    renderForScene(_currentWidgetSceneId.c_str());
}

// Schedule a rerender to run after the current LVGL frame completes.
// Use when rerender is triggered from a context where layout hasn't settled
// (theme change, screen rotation, initial boot before first paint). Reading
// container dimensions via lv_obj_get_width() in those contexts returns
// stale values → masonry calc + widget heights end up wrong.
static void _widgetsRerenderAsyncCb(void*) { Widgets::rerender(); }
void Widgets::rerenderDeferred()
{
    lv_async_call(_widgetsRerenderAsyncCb, nullptr);
}
