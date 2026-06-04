#ifndef __WIFI_HELPERS_H__
#define __WIFI_HELPERS_H__

#include <WiFiType.h>
#include <ESPmDNS.h>

#include "core.h"

namespace WiFiModule {
    class Helper {
    private:
    public:
        static void init();
        static bool connect(const char* ssid, const char *passwd);
        static bool disconnect();
        static bool scanSSID();
        static void changeScanState(ScanState state);
        static void changeState(State state);
        static State getState();
        static wl_status_t getRealState();
        static char *getSSIDList();
        static String getAPIP();
        static void startAP();
        static void stopAP();
        static void restartMDNS();
        static bool checkAndHandleReconnect();  // call from main loop; handles mDNS + state update
    };
}

#endif // __WIFI_HELPERS_H__