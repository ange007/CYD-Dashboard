#include <Preferences.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>

#include "memory.h"

// Serialise all NVS access — prevents concurrent begin/end from two tasks
// (HTTP task and LVGL task) corrupting the NVS partition or causing a deadlock
// inside the esp-idf nvs driver. 5 s timeout surfaces stuck callers as a log
// error rather than hanging indefinitely.
static SemaphoreHandle_t _nvs_mutex = NULL;

static SemaphoreHandle_t getNvsMutex() {
    if (!_nvs_mutex) _nvs_mutex = xSemaphoreCreateMutex();
    return _nvs_mutex;
}

static Preferences preferences;

void uMemory::write(const char *ns, MemoryCallback callback)
{
    SemaphoreHandle_t mx = getNvsMutex();
    if (xSemaphoreTake(mx, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE("NVS", "write(%s): mutex timeout — skipping", ns);
        return;
    }
    preferences.begin(ns, false);
    callback(preferences);
    preferences.end();
    xSemaphoreGive(mx);
}

void uMemory::read(const char *ns, MemoryCallback callback)
{
    // Open read-write so namespace is created if missing (avoids NOT_FOUND log on first run).
    // The callback only reads — no data is written.
    SemaphoreHandle_t mx = getNvsMutex();
    if (xSemaphoreTake(mx, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE("NVS", "read(%s): mutex timeout — skipping", ns);
        return;
    }
    preferences.begin(ns, false);
    callback(preferences);
    preferences.end();
    xSemaphoreGive(mx);
}

void uMemory::remove(const char *ns, const char *key)
{
    uMemory::write(ns, [&key](Preferences preferences) {
        preferences.remove(key);
    });
}

void uMemory::clear(const char *ns)
{
    uMemory::write(ns, [](Preferences preferences) {
        preferences.clear();
    });
}

void uMemory::clearAll()
{
    nvs_flash_erase(); // erase the NVS partition and...
    nvs_flash_init(); // initialize the NVS partition.
}
