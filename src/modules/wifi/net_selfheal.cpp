#include "net_selfheal.h"
#include "net_diag.h"
#include "helpers.h"

#include <WiFi.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include "../hid/keyboard.h"

namespace WiFiModule
{

uint32_t SelfHeal::_gwPingFailConsecutive  = 0;
uint32_t SelfHeal::_lastGwPingOkSnapshot   = 0;
uint32_t SelfHeal::_lastGwPingFailSnapshot = 0;
uint32_t SelfHeal::_lastHttpEnter          = 0;
uint32_t SelfHeal::_lastMdnsAnnounceMs     = 0;
uint32_t SelfHeal::_heapLowSinceMs         = 0;

// Consecutive WiFi.reconnect() attempts that did NOT restore WL_CONNECTED.
// Reset on successful reconnect. If this climbs too high, arduino-esp32's
// cached association is probably stuck in a bad state and only a full
// radio bounce (or esp_restart) will recover.
static uint32_t _reconnectFailStreak = 0;
static constexpr uint32_t RECONNECT_RADIO_RESET_AFTER  = 5;   // esp_wifi_stop + start
static constexpr uint32_t RECONNECT_REBOOT_AFTER       = 10;  // nuclear option

// Non-blocking reconnect: uses Arduino's built-in credential cache.
// Avoids the 15s blocking loop in Helper::connect() and the NVS mutex
// re-entrancy that would occur if Helper::connect() called uMemory::write()
// from inside a uMemory::read() callback on the same task.
static void reconnectWifi()
{
    ESP_LOGW("NETDIAG", "SelfHeal: triggering WiFi reconnect (streak=%u)",
             (unsigned)_reconnectFailStreak);
    HidKeyboard::pauseAdvertising();
    WiFi.disconnect(false, false);  // drop association, keep credentials cached
    WiFi.reconnect();               // non-blocking; result arrives via STA_GOT_IP event
}

// Full radio bounce — stops and restarts the WiFi driver. Recovers from
// states where the arduino-esp32 reconnect path can't rejoin (stuck
// auth timeout, AP side blocking duplicate connections, etc).
static void radioBounce()
{
    ESP_LOGW("NETDIAG", "SelfHeal: radio bounce (esp_wifi_stop + start)");
    HidKeyboard::pauseAdvertising();
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_wifi_start();
    vTaskDelay(pdMS_TO_TICKS(500));
    WiFi.reconnect();
}

void SelfHeal::check()
{
    uint32_t now = millis();

    // -- Trigger 1: WiFi not connected ---------------------------------------
    if (WiFi.status() != WL_CONNECTED)
    {
        // No station credentials configured (fresh flash / NVS erased by a
        // web/USB install) → there is nothing to reconnect to. Rebooting can't
        // conjure a password, so DON'T escalate to esp_restart — that just
        // reboot-loops every 5 min. Stay in AP config mode (already up via
        // APSTA) so the user can enter Wi-Fi. Reset the streak so it starts
        // fresh once credentials are saved.
        wifi_config_t staCfg = {};
        bool hasCreds = (esp_wifi_get_config(WIFI_IF_STA, &staCfg) == ESP_OK &&
                         staCfg.sta.ssid[0] != 0);
        if (!hasCreds)
        {
            _reconnectFailStreak = 0;
            static uint32_t _lastNoCredLog = 0;
            if (now - _lastNoCredLog > 60000) {
                ESP_LOGW("NETDIAG", "SelfHeal: no Wi-Fi credentials — staying in AP config mode (no reboot)");
                _lastNoCredLog = now;
            }
            return;
        }

        _reconnectFailStreak++;
        ESP_LOGW("NETDIAG", "SelfHeal: WiFi status=%d not connected (streak=%u)",
                 (int)WiFi.status(), (unsigned)_reconnectFailStreak);

        // Escalate: try full radio bounce after N failed attempts, reboot
        // as the last resort so a long-stuck device recovers unattended.
        if (_reconnectFailStreak >= RECONNECT_REBOOT_AFTER) {
            ESP_LOGE("NETDIAG", "SelfHeal: %u failed reconnects — esp_restart()",
                     (unsigned)_reconnectFailStreak);
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
        if (_reconnectFailStreak >= RECONNECT_RADIO_RESET_AFTER &&
            _reconnectFailStreak < RECONNECT_REBOOT_AFTER) {
            radioBounce();
        } else {
            reconnectWifi();
        }
        _gwPingFailConsecutive = 0;
        return;
    }
    // Healthy — clear the streak so future disconnects get a fresh ladder.
    if (_reconnectFailStreak > 0) HidKeyboard::resumeAdvertising();
    _reconnectFailStreak = 0;

    // -- Trigger 2: GW ping fails 3x consecutively ---------------------------
    uint32_t okNow   = NetDiag::gwPingOk.load();
    uint32_t failNow = NetDiag::gwPingFail.load();
    if (okNow > _lastGwPingOkSnapshot) {
        _gwPingFailConsecutive = 0;
    } else if (failNow > _lastGwPingFailSnapshot) {
        _gwPingFailConsecutive++;
    }
    _lastGwPingOkSnapshot   = okNow;
    _lastGwPingFailSnapshot = failNow;

    if (_gwPingFailConsecutive >= 6) {
        ESP_LOGW("NETDIAG", "SelfHeal: GW unreachable 6 ticks -- reconnecting WiFi");
        reconnectWifi();
        _gwPingFailConsecutive = 0;
        return;
    }

    // -- Trigger 3: No HTTP traffic + IP stale > 15 min + gateway unreachable
    // Only reconnect if the network actually looks broken. If GW ping has been
    // succeeding (_gwPingFailConsecutive == 0), the network is alive and the
    // device is just idle (WS keepalive, companion connection etc. don't
    // bump httpEnter). A forced reconnect in that case costs ~500 ms of DRAM
    // pressure during the AP_START/STA_GOT_IP storm, which can OOM the LVGL
    // DMA flush on a constrained system.
    uint32_t httpNow = NetDiag::httpEnter.load();
    uint32_t lastGotIp = NetDiag::lastGotIpMs.load();
    if (lastGotIp > 0 &&
        (now - lastGotIp) > 900000 &&      // 15 min (was 5)
        httpNow == _lastHttpEnter &&
        _gwPingFailConsecutive > 0)        // at least 1 recent ping fail
    {
        ESP_LOGW("NETDIAG", "SelfHeal: no HTTP + IP stale %us + GW ping failing -- reconnecting WiFi",
                 (unsigned)((now - lastGotIp) / 1000));
        reconnectWifi();
        _lastHttpEnter = httpNow;
        return;
    }
    _lastHttpEnter = httpNow;

    // -- Trigger 4: mDNS reannounce every 10 min -----------------------------
    if (_lastMdnsAnnounceMs == 0) _lastMdnsAnnounceMs = now;
    if ((now - _lastMdnsAnnounceMs) > 600000) {
        ESP_LOGI("NETDIAG", "SelfHeal: periodic mDNS reannounce");
        Helper::restartMDNS();
        _lastMdnsAnnounceMs = now;
    }

    // -- Trigger 5: Heap < 4KB stable 60s -> WARN only (no restart) ----------
    uint32_t heap = ESP.getFreeHeap();
    if (heap < 4000) {
        if (_heapLowSinceMs == 0) {
            _heapLowSinceMs = now;
        } else if ((now - _heapLowSinceMs) > 60000) {
            ESP_LOGE("NETDIAG", "SelfHeal: heap critically low %u for >60s (no restart, human decision)", heap);
            _heapLowSinceMs = now;
        }
    } else {
        _heapLowSinceMs = 0;
    }
}

} // namespace WiFiModule
