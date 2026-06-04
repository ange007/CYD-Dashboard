#include "./constants.h"

#include "helpers.h"
#include "../../utils/settings.h"
#include "ui.h"

#include "./ui/ui.h"

#include "./utils/memory.h"
#include <esp_wifi.h>
#include "net_diag.h"

namespace WiFiModule {
  static volatile bool _reconnect_pending  = false;
  static volatile bool _disconnect_pending = false;

  State Helper::getState()
  {
    return wifi_state;
  }

  /**
   * 
  */
  wl_status_t Helper::getRealState()
  {
    return WiFiClient.status();
  }

  /**
   * @brief This function returns the WiFi SSID Names appended together which is 
   *        suitable for LVGL drop down widget
   * @param  none
   * @return pointer to wifi ssid drop down list
   */
  char *Helper::getSSIDList()
  {
    return wifi_dd_list;
  }

  /**
   * @brief Starts the Access Point (always on, dual AP+STA mode).
   *        SSID and password are defined by WIFI_AP_SSID / WIFI_AP_PASS.
   */
  void Helper::startAP()
  {
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    WiFiClient.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    ESP_LOGI("WiFi", "AP started: SSID=%s IP=%s", WIFI_AP_SSID,
             WiFiClient.softAPIP().toString().c_str());
  }

  /**
   * @brief Stops the Access Point.
   */
  void Helper::stopAP()
  {
    WiFiClient.softAPdisconnect(false);
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI("WiFi", "AP stopped");
  }

  /**
   * @brief Returns the AP IP address as a String.
   */
  String Helper::getAPIP()
  {
    return WiFiClient.softAPIP().toString();
  }

  /**
   * @brief Initialize the WiFi mode to AP+STA and attempt STA connection
   *        if credentials are saved. AP is always started so the device
   *        is always reachable for initial configuration.
   * @param  none
   */
  void Helper::init()
  {
    wifi_state = State::Disconnected;

    memset(wifi_dd_list, 0, sizeof(wifi_dd_list));
    memset(wifi_ssid, 0, sizeof(wifi_ssid));

    WiFiClient.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFiClient.setHostname(SERVER_NAME);
    WiFiClient.mode(WIFI_AP_STA);

    // WiFi modem sleep must remain enabled (WIFI_PS_MIN_MODEM) when BLE is active.
    // ESP32-S3 hardware coex requires modem sleep — setSleep(false)/WIFI_PS_NONE
    // is explicitly forbidden and causes an abort() in pm_set_sleep_type.
    WiFiClient.setSleep(true);

    // Always start the AP so the device is reachable before STA connects
    startAP();

    // Auto-reconnect: ESP32 will reconnect automatically; we just need to
    // restart mDNS and update UI state when it succeeds.
    WiFiClient.setAutoReconnect(true);
    WiFiClient.persistent(true);
    WiFiClient.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        _reconnect_pending = true;
        NetDiag::wifiReconnects++;
        NetDiag::lastGotIpMs.store(millis());
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

    WiFiClient.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        _disconnect_pending = true;
        NetDiag::wifiDisconnects++;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    uMemory::read("wifi", [](Preferences preferences) {
      String ssid = preferences.getString("ssid");
      String password = preferences.getString("pass");

      if (ssid == "" || password == "") {
        ESP_LOGI("WiFi", "No STA credentials saved — AP-only mode");
      } else {
        connect(ssid.c_str(), password.c_str());
        UI::setSSIDList(ssid.c_str());
      }
    });
  }

  bool Helper::connect(const char* ssid, const char *passwd)
  {
    ESP_LOGI("WiFi", "Try connect...");
    ESP_LOGI("WiFi", "SSID: %s", ssid);
    // NEVER log the password in clear. Serial output is unprotected on USB
    // and can be captured by anyone with physical / local network access
    // (many users share pio device monitor output when asking for help).
    ESP_LOGI("WiFi", "Password: [%u chars redacted]",
             passwd ? (unsigned)strlen(passwd) : 0u);

    changeState(State::Connection);

    // Force Google DNS so external hostnames resolve regardless of DHCP DNS quality.
    // Passing 0.0.0.0 for local_ip/gateway/subnet keeps DHCP for those fields.
    WiFiClient.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0),
                      IPAddress(8, 8, 8, 8), IPAddress(8, 8, 4, 4));

    // Try to Connect with the Router
    WiFiClient.begin(ssid, passwd);

    // Poll for connection with 500 ms steps, max 15 s
    uint32_t startTime = millis();
    while (WiFiClient.status() != WL_CONNECTED && (millis() - startTime) < 15000) {
      delay(500);
    }

    wl_status_t wifiStatus = WiFiClient.status();
    ESP_LOGI("WiFi", "WiFi status: %d", wifiStatus);

    if (wifiStatus == WL_CONNECTED) {
      changeState(State::Connected);

      ESP_LOGI("WiFi", "Connected! IP=%s DNS=%s",
               WiFiClient.localIP().toString().c_str(),
               WiFiClient.dnsIP().toString().c_str());

      uMemory::write("wifi", [&](Preferences preferences) {
        preferences.putString("ssid", ssid);
        preferences.putString("pass", passwd);
      });

          
      stopAP();
      restartMDNS();

      return true;
    }

    disconnect();
    changeState(State::Error);

    ESP_LOGE("WiFi", "Connected Error!");

    return false;
  }

  bool Helper::disconnect()
  {
    ESP_LOGI("WiFi", "Try to Disconnect...");

    // Try to Disconnect
    bool status = WiFiClient.disconnect(false, true);
    changeState(State::Disconnected);

    MDNS.end();
    startAP();

    if (status) {
      ESP_LOGI("WiFi", "Disconnected!");
    } else {
      ESP_LOGE("WiFi", "Disconnected Error!");
    }

    return status;
  }

  /**
   * @brief Scans for the SSID and store the results in a WiFi drop down list
   * "wifi_dd_list", this list is suitable to be used with the LVGL Drop Down
   * @param  none
   */
  bool Helper::scanSSID()
  {
    String ssid_name;

    ESP_LOGI("WiFi.Helpers", "Start Scanning");
    int n = WiFiClient.scanNetworks();
    ESP_LOGI("WiFi.Helpers", "Scanning Done");

    if (n == 0) {
      ESP_LOGI("WiFi.Helpers", "No Networks Found");

      return false;
    } else {
      // I am restricting n to max WIFI_MAX_SSID value
      n = n <= WIFI_MAX_SSID ? n : WIFI_MAX_SSID;
      for (int i = 0; i < n; i++) {
        if (i == 0) {
          ssid_name = WiFiClient.SSID(i);
        } else {
          ssid_name = ssid_name + WiFiClient.SSID(i);
        }
        ssid_name = ssid_name + '\n';
        delay(10);
      }
      
      // clear the array, it might be possible that we coming after rescanning
      memset(wifi_dd_list, 0x00, sizeof(wifi_dd_list));
      // strlcpy (not strcpy) — enforces the buffer limit so a run of long SSIDs
      // cannot overflow wifi_dd_list (sized WIFI_MAX_SSID * 34 bytes in core.h).
      strlcpy(wifi_dd_list, ssid_name.c_str(), sizeof(wifi_dd_list));
      
      ESP_LOGI("WiFi.Helpers", "Scanning Completed");

      return true;
    }
  }

  void Helper::changeScanState(ScanState state)
  {
    UI::changeScanState(state);
  }

  void Helper::changeState(State state)
  {
    UI::changeState(state);

    wifi_state = state;
  }

  void Helper::restartMDNS()
  {
    NetDiag::mdnsAnnounces++;
    MDNS.end();
    String host = uSettings::getMdnsHost();
    WiFiClient.setHostname(host.c_str());
    if (!MDNS.begin(host.c_str())) {
      ESP_LOGE("WiFi", "Error setting up MDNS responder!");
    } else {
      MDNS.addService("http", "tcp", 80);
      ESP_LOGI("WiFi", "MDNS started as http://%s.local", host.c_str());
    }
  }

  bool Helper::checkAndHandleReconnect()
  {
    // Handle disconnect event — update state so UI no longer shows stale IP
    if (_disconnect_pending) {
      _disconnect_pending = false;
      if (wifi_state == State::Connected) {
        ESP_LOGW("WiFi", "STA disconnected");
        changeState(State::Disconnected);
        startAP();
      }
    }

    if (!_reconnect_pending) return false;
    _reconnect_pending = false;
    ESP_LOGI("WiFi", "STA auto-reconnected, IP=%s",
             WiFiClient.localIP().toString().c_str());
    stopAP();
    restartMDNS();
    changeState(State::Connected);
    return true;
  }
}