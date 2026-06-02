#include "constants.h"

/**************
 * 
***************/

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

/**************
 * UI
***************/

#include <esp32_smartdisplay.h>
#include <esp32_smartdisplay_dma.h>
#include <lv_conf.h>
#include "./ui/ui.h"

// lv_image_cache_resize() lives in the LVGL internal cache header; declare here
// to avoid dragging in the full internal include tree.
extern "C" void lv_image_cache_resize(uint32_t new_size, bool evict_now);

/**************
 * HID (BLE / USB)
***************/

#include "./modules/hid/keyboard.h"

/**************
 * Wifi
***************/

#include "./modules/wifi/helpers.h"
#include "./modules/wifi/server.h"
#include "./modules/wifi/ui.h"

#include "./modules/timers.h"
#include "./modules/cards/widgets.h"
#include "./modules/cards/profiles.h"

#include "./utils/settings.h"

// loopTask stack: 12 KB.  Empirically high-water mark is ~8 KB under
// steady-state LVGL + WiFi + HTTP; 12 KB gives 4 KB safety margin.
// Default Arduino 8 KB is insufficient (LVGL draw recursion can exceed it).
SET_LOOP_TASK_STACK_SIZE(12 * 1024);

// ── Mutex-protected serial logging ───────────────────────────────────────────
// Multiple tasks (loopTask, httpWorkerTask, esp_http_server) write ESP_LOG*
// concurrently, interleaving characters.  Route all log output through a mutex.
static SemaphoreHandle_t s_logMutex = nullptr;

static int mutexVprintf(const char *fmt, va_list args) {
    if (!s_logMutex || xSemaphoreTake(s_logMutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return 0; // drop rather than block too long
    int ret = vprintf(fmt, args);
    xSemaphoreGive(s_logMutex);
    return ret;
}

static uint32_t lvgl_refresh_timestamp = 0u;
static uint32_t lvgl_tick_timestamp   = 0u;

// ── Boot splash (black screen + stage label) ─────────────────────────────────
// Shown between smartdisplay_init() finishing and WSServer::start() returning,
// so user sees progress instead of a blank/garbage LCD for 5-8 s of bring-up.
static lv_obj_t *_bootScreen = nullptr;
static lv_obj_t *_bootLabel  = nullptr;

static void bootSplashPump()
{
    // 3 frames × 15 ms lets the LVGL DMA worker complete one flush.
    uint32_t t = millis();
    for (int i = 0; i < 3; i++) {
        lv_tick_inc(millis() - t);
        t = millis();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

static void bootSplashInit()
{
    _bootScreen = lv_obj_create(NULL);  // screen object (parent = display)
    lv_obj_set_style_bg_color(_bootScreen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_bootScreen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_bootScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(_bootScreen);
    lv_label_set_text(title, "CYD Dashboard");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -16);

    _bootLabel = lv_label_create(_bootScreen);
    lv_label_set_text(_bootLabel, "Starting...");
    lv_obj_set_style_text_color(_bootLabel, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(_bootLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(_bootLabel, LV_ALIGN_CENTER, 0, 16);

    lv_disp_load_scr(_bootScreen);
    bootSplashPump();
}

static void bootSplashStage(const char *text)
{
    if (_bootLabel) {
        lv_label_set_text(_bootLabel, text);
        bootSplashPump();
    }
}

static void bootSplashDismiss()
{
    if (!_bootScreen) return;
    ESP_LOGI("SPLASH", "dismiss: ui_screenMain=%p  heap=%u", (void*)ui_screenMain, ESP.getFreeHeap());
    lv_disp_load_scr(ui_screenMain);
    // Drive initial ui_screenMain render inside setup — faster and more reliable
    // than deferring to loop(). 12 iters covers 8 bands + margin.
    {
        uint32_t t = millis();
        for (int i = 0; i < 12; i++) {
            lv_tick_inc(millis() - t);
            t = millis();
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
    lv_obj_del(_bootScreen);
    _bootScreen = nullptr;
    _bootLabel  = nullptr;
    ESP_LOGI("SPLASH", "dismissed  heap=%u", ESP.getFreeHeap());
}

// Sleep / power management state
static bool _isDimmed        = false;
static bool _isOff           = false;
static int  _savedBrightness = -1;  // brightness before dim (restored on wake)

// Private Function Definitions
/**
 * Pre-reset the GT911 touch IC (RST=GPIO38, INT=GPIO3) before the library
 * attempts its own initialisation.
 *
 * The GT911 I2C slave address is selected by the state of the INT pin during
 * the RST-release edge:
 *   INT = LOW  during RST↑  →  address 0x5D  (what esp32_smartdisplay expects)
 *   INT = HIGH during RST↑  →  address 0x14
 *
 * Without this pre-reset the IC can be left in an indeterminate state from a
 * previous warm-reset (esp_restart), causing the library's ESP_ERROR_CHECK to
 * abort with "GT911 read info failed".
 */
#if defined(TOUCH_GT911_I2C) && defined(GT911_TOUCH_CONFIG_RST) && defined(GT911_TOUCH_CONFIG_INT)
static void gt911_pre_reset()
{
    const gpio_num_t RST = (gpio_num_t)(GT911_TOUCH_CONFIG_RST);
    const gpio_num_t INT = (gpio_num_t)(GT911_TOUCH_CONFIG_INT);

    // Boards that hardwire RST (no controllable GPIO) define GPIO_NUM_NC — skip
    // the pulse sequence since we can't drive the reset line.
    if (RST == GPIO_NUM_NC || INT == GPIO_NUM_NC) {
        return;
    }

    // Drive both pins as outputs before the RST pulse
    gpio_set_direction(INT, GPIO_MODE_OUTPUT);
    gpio_set_direction(RST, GPIO_MODE_OUTPUT);

    gpio_set_level(INT, 0);          // INT LOW → select I2C address 0x5D
    gpio_set_level(RST, 0);          // Assert reset
    vTaskDelay(pdMS_TO_TICKS(20));   // RST low hold: spec ≥ 100 µs, we use 20 ms

    gpio_set_level(RST, 1);          // Release reset — address latched on this edge
    vTaskDelay(pdMS_TO_TICKS(120));  // GT911 startup: spec 55 ms typical, 120 ms for safety

    gpio_set_direction(INT, GPIO_MODE_INPUT); // Release INT (pull-up on board takes over)
    vTaskDelay(pdMS_TO_TICKS(50));   // Settle before library re-does its own init
}
#endif // TOUCH_GT911_I2C && GT911_TOUCH_CONFIG_RST && GT911_TOUCH_CONFIG_INT

static void displayInit(void)
{
#if defined(TOUCH_GT911_I2C) && defined(GT911_TOUCH_CONFIG_RST) && defined(GT911_TOUCH_CONFIG_INT)
    // Reset the GT911 to a known state so the library's I2C probe succeeds.
    // Pins come from the board JSON defines; boards with GPIO_NUM_NC skip the
    // pulse sequence. XPT2046, CST816S and CST328 boards lack TOUCH_GT911_I2C
    // so this whole block is absent there.
    gt911_pre_reset();
#endif

    smartdisplay_init();
    smartdisplay_lcd_set_backlight(1);

    //
    ui_init();

#if LV_USE_TJPGD
    lv_tjpgd_init();
    log_i("lv_tjpgd_init() called");
#endif

#ifdef BOARD_HAS_PSRAM
    // Image cache for decoded JPEG backgrounds + raw .bin icons (both live in PSRAM).
    // 600 KB gives headroom for one bg + several icons.
    lv_image_cache_resize(600u * 1024u, false);

    // Extend the LVGL TLSF heap with a 128 KB PSRAM pool.
    {
        const size_t LVGL_PSRAM_EXT = 128u * 1024u;
        void* psram_pool = heap_caps_malloc(LVGL_PSRAM_EXT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (psram_pool) {
            lv_mem_add_pool(psram_pool, LVGL_PSRAM_EXT);
        } else {
            log_e("LVGL PSRAM pool alloc failed!");
        }
    }
#else
    log_i("BOARD_HAS_PSRAM not set — skipping PSRAM pool extend and image cache");
#endif

    {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        log_i("LVGL heap: total=%u free=%u used_pct=%u",
              (unsigned)mon.total_size, (unsigned)mon.free_size, (unsigned)mon.used_pct);
    }

    //
    uint32_t now = millis();
    lvgl_refresh_timestamp = now;
    lvgl_tick_timestamp   = now;
}

static void LVGL_TaskMng(void)
{
    uint32_t now = millis();

    uint32_t dt = now - lvgl_tick_timestamp;
    lvgl_tick_timestamp = now;
    lv_tick_inc(dt);

    // LVGL Refresh — recursive FreeRTOS mutex coordinates with any async UI
    // mutation (_ui_enable_mutex/_ui_disable_mutex). Timeout 0: skip frame if
    // a mutation is in progress rather than race it.
    if ((now - lvgl_refresh_timestamp) >= LVGL_REFRESH_TIME) {
        // DRAM heap guard: bounce-buffer alloc for display DMA needs ≥ ~8 KB
        // internal. BLE connection setup transiently drops DRAM to ~6-7 KB,
        // which causes esp_lcd_panel_draw_bitmap → abort(). Skip the frame.
        if (ESP.getFreeHeap() < 8000) {
            lvgl_refresh_timestamp = now;
            return;
        }
        static uint32_t _lockFailMs = 0;
        if (_ui_try_lock(0)) {
            _lockFailMs = 0;
            lvgl_refresh_timestamp = now;
            lv_timer_handler();
            _ui_unlock();
        } else {
            // Log first occurrence + each 5s streak of consecutive lock failures
            if (now - _lockFailMs >= 5000) {
                _lockFailMs = now;
                log_w("[LVGL] _ui_try_lock(0) failed — LVGL skipped (held by other task?)");
            }
        }
    }
}

// Called from loop() — uses LVGL's built-in inactivity timer so no separate touch tracking is needed.
static void checkSleep()
{
    int dimTimeout = uSettings::getSleepDimTimeout();
    int offTimeout = uSettings::getSleepOffTimeout();

    // No sleep configured
    if (dimTimeout <= 0) {
        if (_isDimmed || _isOff) {
            _isDimmed = false;
            _isOff    = false;
            int restoreTo = (_savedBrightness > 0) ? _savedBrightness : uSettings::getBrightness();
            uSettings::displayBrightness(restoreTo);
            _savedBrightness = -1;
        }
        return;
    }

    uint32_t inactiveMs = lv_disp_get_inactive_time(NULL);

    if (_isDimmed || _isOff) {
        // Any new touch resets LVGL inactivity — detect wake
        if (inactiveMs < (uint32_t)dimTimeout * 60000UL) {
            _isDimmed = false;
            _isOff    = false;
            // Restore saved brightness; displayBrightness() updates the _brightness cache
            int restoreTo = (_savedBrightness > 0) ? _savedBrightness : uSettings::getBrightness();
            uSettings::displayBrightness(restoreTo);
            _savedBrightness = -1;
            return;
        }
    }

    if (!_isDimmed && inactiveMs >= (uint32_t)dimTimeout * 60000UL) {
        _savedBrightness = uSettings::getBrightness();   // snapshot before dimming
        _isDimmed = true;
        // Hardware-only: bypass displayBrightness() to preserve the cached brightness
        smartdisplay_lcd_set_backlight(uSettings::getSleepDimLevel() / 100.0f);
    }

    if (_isDimmed && !_isOff && offTimeout > 0 &&
        inactiveMs >= (uint32_t)(dimTimeout + offTimeout) * 60000UL) {
        _isOff = true;
        smartdisplay_lcd_set_backlight(0.0f);
    }
}

/**
 *
*/

void setup()
{
    #ifdef ARDUINO_USB_CDC_ON_BOOT
        delay(5000);
    #endif

    Serial.begin(115200);
    // Non-blocking serial: drop bytes when USB CDC buffer full instead of blocking.
    // Prevents Interrupt WDT when ESP_ERROR_CHECK prints a long message while USB
    // is not connected (e.g. GT911 touch init failure on boot).
    // Small timeout so vprintf can drain the USB CDC buffer without dropping bytes
    // mid-message, but short enough to avoid Interrupt WDT if USB is not connected.
    // setTxTimeoutMs exists only on HWCDC (USB CDC) — classic ESP32 UART
    // HardwareSerial has no such method. Guard with value check because the
    // macro is always defined (0 or 1) by the Arduino core.
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(5);
#endif
    Serial.setDebugOutput(true);

    // Activate mutex-protected log output to prevent garbled serial from
    // concurrent task writes (loopTask, httpWorkerTask, esp_http_server).
    s_logMutex = xSemaphoreCreateMutex();
    if (s_logMutex) esp_log_set_vprintf(mutexVprintf);

    // ── Boot reason logging + crash counter ──────────────────────────────
    {
        esp_reset_reason_t reason = esp_reset_reason();
        const char *reasonStr = "unknown";
        bool isCrash = false;
        switch (reason) {
            case ESP_RST_POWERON:   reasonStr = "power_on"; break;
            case ESP_RST_SW:        reasonStr = "software"; break;
            case ESP_RST_PANIC:     reasonStr = "panic"; isCrash = true; break;
            case ESP_RST_INT_WDT:   reasonStr = "int_wdt"; isCrash = true; break;
            case ESP_RST_TASK_WDT:  reasonStr = "task_wdt"; isCrash = true; break;
            case ESP_RST_WDT:       reasonStr = "wdt"; isCrash = true; break;
            case ESP_RST_BROWNOUT:  reasonStr = "brownout"; isCrash = true; break;
            case ESP_RST_DEEPSLEEP: reasonStr = "deepsleep"; break;
            default: break;
        }
        log_i("Reset reason: %s (%d)", reasonStr, (int)reason);
        if (isCrash) {
            Preferences prefs;
            prefs.begin("crash_log", false);
            uint32_t count = prefs.getUInt("count", 0) + 1;
            prefs.putUInt("count", count);
            prefs.putUInt("last_reason", (uint32_t)reason);
            prefs.putUInt("last_heap", ESP.getFreeHeap());
            prefs.end();
            log_e("CRASH RECOVERY: boot after %s (crash #%u)", reasonStr, count);
        }
    }

    log_i("Board: %s", BOARD_NAME);
    log_i("CPU: %s rev%d, CPU Freq: %d Mhz, %d core(s)", ESP.getChipModel(), ESP.getChipRevision(), getCpuFrequencyMhz(), ESP.getChipCores());
    log_i("Free heap: %d bytes", ESP.getFreeHeap());
    log_i("Free PSRAM: %d bytes", ESP.getPsramSize());
    log_i("SDK version: %s", ESP.getSdkVersion());

    // ── Stage 0: WiFi heap reservation (no-PSRAM only) ───────────────────
    // LV_STDLIB_CLIB routes all LVGL allocs through system heap. After
    // displayInit() fragments heap with many small objects, esp_wifi_init()
    // can't find its required ~50 KB contiguous block (ESP_ERR_NO_MEM).
    // Reserve the block now while heap is fresh; free it right before WiFi
    // init so the driver finds a guaranteed contiguous slot.
#ifndef BOARD_HAS_PSRAM
    void* _wifiHeapReserve = heap_caps_malloc(55000, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!_wifiHeapReserve) log_w("WiFi heap reserve failed — esp_wifi_init may fail");
    else log_i("WiFi heap reserve OK (55 KB)  heap=%u", ESP.getFreeHeap());
#endif

    // ── Stage 1: Display + UI ─────────────────────────────────────────────
    // Init UI mutex BEFORE anything that can mutate LVGL state concurrently
    // (HTTP server, BLE, timers). displayInit() triggers ui_init() which
    // creates initial LVGL objects; _ui_enable_mutex is not called during
    // setup, but we init here to ensure the handle is live before loopTask
    // and httpWorkerTask spin up.
    _ui_mutex_init();

    displayInit();

#ifndef BOARD_HAS_PSRAM
    // Release DMA manager (64 KB DMA buffer + task) — not needed on no-PSRAM boards.
    // smartdisplay_dma_flush_with_byteswap() falls back to direct SPI transfer when
    // g_dma_manager is NULL; that path is reliable and avoids obscure DMA failures
    // that appear after WiFi init fragments the heap. Gains ~64 KB heap at runtime.
    smartdisplay_dma_deinit();
    log_i("DMA deinitialized (no-PSRAM) — using direct SPI  heap=%u", ESP.getFreeHeap());
#endif

    bootSplashInit();
    bootSplashStage("Filesystem...");
    if (!LittleFS.begin(true)) {
        log_e("LittleFS mount failed — macros/widgets will fall back to NVS");
    } else {
        log_i("LittleFS mounted OK, total=%u used=%u",
              (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes());
    }

    bootSplashStage("Settings...");
    uSettings::load();

    bootSplashStage("Profiles...");
    Cards::Profiles::init();

    bootSplashStage("HID...");
    {
        uint32_t freeHeap = ESP.getFreeHeap();
        log_i("Free heap before HID init: %u bytes", freeHeap);
        if (freeHeap >= 40000) {
            HidKeyboard::init();
        } else {
            log_w("Skipping HID init — insufficient heap (%u B < 40000 B)", freeHeap);
        }
    }
    log_i("Free heap after BLE: %u bytes", ESP.getFreeHeap());

    bootSplashStage("WiFi...");
#ifndef BOARD_HAS_PSRAM
    if (_wifiHeapReserve) { heap_caps_free(_wifiHeapReserve); _wifiHeapReserve = nullptr; }
    log_i("WiFi heap reserve released  heap=%u", ESP.getFreeHeap());
#endif
    WiFiModule::Helper::init();

    // IMPORTANT (no-PSRAM): dismiss splash BEFORE WSServer::start().
    // Server start consumes ~23 KB heap; with tight heap afterward, the first
    // full-screen LVGL render of ui_screenMain during dismiss hangs inside
    // esp_lcd_panel_draw_bitmap. Rendering ui_screenMain while heap is still
    // ~95 KB avoids the hang. Post-dismiss updates are small partial refreshes
    // and tolerate tighter heap.
    bootSplashStage("Ready");
    bootSplashDismiss();

    WiFiModule::WSServer::start();
    log_i("Free heap after server: %u bytes", ESP.getFreeHeap());
}

ulong next_millis;

// Reset crash counter after 5 min of stable uptime (no crash in that window).
// Prevents the counter from growing indefinitely after a single bad firmware flash.
static void checkCrashCounterReset()
{
    static bool _crashCounterCleared = false;
    if (_crashCounterCleared) return;
    if (millis() < 5UL * 60UL * 1000UL) return;
    _crashCounterCleared = true;
    Preferences prefs;
    prefs.begin("crash_log", false);
    uint32_t count = prefs.getUInt("count", 0);
    if (count > 0) {
        prefs.remove("count");
        prefs.remove("last_reason");
        prefs.remove("last_heap");
        log_i("Stable uptime — crash counter reset (was %u)", count);
    }
    prefs.end();
}

void loop()
{
    WiFiModule::WSServer::loop();
    mTimers::loop();
    Cards::Widgets::loop();
    HidKeyboard::loop();
    checkSleep();
    uSettings::tickStatusBarAutohide();
    checkCrashCounterReset();

    /** Do your thing here, this just spams notifications to all connected clients */
    /**if(pServer->getConnectedCount()) {
        NimBLEService* pSvc = pServer->getServiceByUUID("BAAD");
        if(pSvc) {
            NimBLECharacteristic* pChr = pSvc->getCharacteristic("F00D");
            if(pChr) {
                pChr->notify(true);
            }
        }
    }
    **/

   /*
    auto const now = millis();
    if (now > next_millis) {
        next_millis = now + 500;

        char text_buffer[32];
        sprintf(text_buffer, "%lu", now);
        //lv_label_set_text(ui_lblMillisecondsValue, text_buffer);

        #ifdef BOARD_HAS_RGB_LED
            auto const rgb = (now / 2000) % 8;
            smartdisplay_led_set_rgb(rgb & 0x01, rgb & 0x02, rgb & 0x04);
        #endif

        #ifdef BOARD_HAS_CDS
            auto cdr = analogReadMilliVolts(CDS);
            sprintf(text_buffer, "%d", cdr);
            //lv_label_set_text(ui_lblCdrValue, text_buffer);
        #endif
    }

    lv_timer_handler();
    */

    //Display_Mng();
    LVGL_TaskMng();

    // Yield to the FreeRTOS scheduler so that BLE (NimBLE), AsyncTCP, and the
    // idle/watchdog tasks on both cores get CPU time between loop() iterations.
    // Without this, loopTask can spin at 100 % on Core 1, starving the NimBLE
    // stack task and causing repeated BLE disconnect → reconnect cycles.
    vTaskDelay(1);
}