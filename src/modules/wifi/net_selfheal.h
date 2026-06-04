#ifndef NET_SELFHEAL_H
#define NET_SELFHEAL_H

#include <Arduino.h>
#include <cstdint>

namespace WiFiModule
{

// Conservative network self-heal: reads NetDiag counters on its own 30s tick,
// triggers WiFi reconnect or mDNS reannounce on clear failure signals.
// **No auto-reboots.** `ESP.restart()` is a human decision.
class SelfHeal
{
public:
    // Evaluate triggers and dispatch actions. Called every 30s from
    // WebServerBase::loop().
    static void check();

private:
    static uint32_t _gwPingFailConsecutive;    // reset on any ok
    static uint32_t _lastGwPingOkSnapshot;     // last-seen NetDiag::gwPingOk counter
    static uint32_t _lastGwPingFailSnapshot;   // last-seen NetDiag::gwPingFail counter
    static uint32_t _lastHttpEnter;            // snapshot for "no traffic" test
    static uint32_t _lastMdnsAnnounceMs;       // set to millis() after each reannounce
    static uint32_t _heapLowSinceMs;           // set when heap<4000, cleared otherwise
};

} // namespace WiFiModule

#endif // NET_SELFHEAL_H
