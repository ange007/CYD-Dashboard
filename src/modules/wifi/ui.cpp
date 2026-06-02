#include "helpers.h"
#include "ui.h"

#include <WiFi.h>
#include <stdio.h>

#include "./ui/ui.h"

using namespace WiFiModule;

/**
 * Updates the scan state label on the UI based on the provided scan state.
 *
 * @param state The new scan state to update the label with.
 */
void UI::changeScanState(ScanState state)
{
    lv_obj_t * statusLabel = ui_lblWifiScan;
    const char* status = "...";

    ESP_LOGI("WiFi.UI", "New scan state: %i", state);

    switch (state) {
        case ScanState::None:
            status = "Scan";
            break;
        case ScanState::Scanned:
            status = "Scanned...";
            break;
        case ScanState::ScanError:
            status = "Error...";
            break;
        default:
            ESP_LOGE("WiFi.UI", "Unknown scan state");
            return;
    }

    lv_label_set_text(statusLabel, status); 
}

/**
 * Updates the UI components based on the provided state.
 *
 * @param state The new state to update the UI with.
 *
 * @return None
 */
void UI::changeState(State state)
{
    lv_obj_t * statusLabel = ui_lblWifiAction;
    const char* status = "...";

    ESP_LOGI("WiFi.UI", "New state: %i", state);

    // IP
    lv_label_set_text(ui_lblWifiIP, "IP: ...");
    lv_label_set_text(ui_lblWifiRSSI, "Signal: ...");

    // Default panel button state
    lv_obj_set_style_text_color(ui_lblWifi, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);

    switch (state) {
        case State::Connection:
            status = "Connection...";
            lv_obj_add_state(ui_btnWifiScan, LV_STATE_DISABLED);
            break;
        case State::Connected:
            status =  "Disconnect";
            lv_obj_add_state(ui_btnWifiScan, LV_STATE_DISABLED);

            UI::updateIP(WiFiClient.localIP().toString().c_str());
            UI::updateRSSI((int)WiFiClient.RSSI());

            // Panel button
            lv_obj_set_style_text_color(ui_lblWifi, lv_color_hex(0x0026FF), LV_PART_MAIN | LV_STATE_DEFAULT);
            break;
        case State::Disconnected:
            status = "Connect";
            lv_obj_clear_state(ui_btnWifiScan, LV_STATE_DISABLED);
            break;
        case State::Error:
            status = "Error...";
            lv_obj_clear_state(ui_btnWifiScan, LV_STATE_DISABLED);
            break;
        default:
            ESP_LOGE("WiFi.UI", "Unknown state");
            return;
    }

    lv_label_set_text(statusLabel, status);
}

/**
 * Sets the options for the SSID dropdown in the UI.
 *
 * @param options A null-terminated string containing the options for the dropdown.
 */
void UI::setSSIDList(const char* options)
{
    lv_dropdown_set_options(ui_ddWifiSSID, options);
}

/**
 * @brief 
 * 
 * @param ip 
 */
void UI::updateIP(const char* ip)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "IP: %s", ip);
    lv_label_set_text(ui_lblWifiIP, buf);
}

/**
 * Updates RSSI label with dBm value + human-readable quality tier.
 * Tiers per 802.11: >=-55 Excellent, >=-65 Good, >=-75 Fair, <-75 Weak.
 * rssi==0 means disconnected (WiFi.RSSI() returns 0 when not connected).
 */
void UI::updateRSSI(int rssi)
{
    if (!ui_lblWifiRSSI) return;
    if (rssi == 0 || rssi < -127) {
        lv_label_set_text(ui_lblWifiRSSI, "Signal: --");
        return;
    }
    const char* quality =
        (rssi >= -55) ? "Excellent" :
        (rssi >= -65) ? "Good"      :
        (rssi >= -75) ? "Fair"      : "Weak";
    char buf[48];
    snprintf(buf, sizeof(buf), "Signal: %d dBm (%s)", rssi, quality);
    lv_label_set_text(ui_lblWifiRSSI, buf);
}