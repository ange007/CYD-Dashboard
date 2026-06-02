#ifndef __WIFI_CORE_H__
#define __WIFI_CORE_H__

#include <WiFi.h>

// #define LVGL_REFRESH_TIME                (5u)      // 5 milliseconds
#define WIFI_MAX_SSID                       (7u)
#define WIFI_SSID_BUFFER_SIZE               (33u)    // 32-char SSID + null terminator

namespace WiFiModule {        
    enum ScanState {
        None = 0,
        Scanned = 1,
        ScanError = 2
    }; 

    enum State {
        Disconnected = 0,
        Connection = 1,
        Connected = 2,
        Error = 3
    };

    static WiFiClass WiFiClient;
    static State wifi_state;

    static char wifi_dd_list[WIFI_MAX_SSID * 34];  // 32-char SSID + newline + margin per entry
    static char wifi_ssid[WIFI_SSID_BUFFER_SIZE];
}
#endif // __WIFI_CORE_H__