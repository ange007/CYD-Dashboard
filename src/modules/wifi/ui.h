#ifndef __WIFI_UI_H__
#define __WIFI_UI_H__

#include "helpers.h"

namespace WiFiModule {
    class UI {
    public:
        static void changeScanState(ScanState state);
        static void changeState(State state);
        static void updateIP(const char* ip);
        static void setSSIDList(const char* options);
        static void updateRSSI(int rssi);
    };
}

#endif // __WIFI_UI_H__