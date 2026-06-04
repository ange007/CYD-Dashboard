#ifndef NET_DIAG_H
#define NET_DIAG_H

#include <Arduino.h>
#include <atomic>
#include <cstdint>

namespace WiFiModule
{

// Passive network-stack observability.
// All counters are std::atomic<uint32_t> — lock-free reads/writes from any task
// (WiFi event callback, HTTP handler task, LVGL/UI task). Reads from toJson()
// are coherent per-field; the struct may represent a non-atomic snapshot across
// fields but never tears within a single field.
class NetDiag
{
public:
    // WiFi state
    static std::atomic<uint32_t> wifiDisconnects;
    static std::atomic<uint32_t> wifiReconnects;
    static std::atomic<int32_t>  lastRssi;
    static std::atomic<uint32_t> lastGotIpMs;

    // HTTP middleware / handler counters
    static std::atomic<uint32_t> httpEnter;
    static std::atomic<uint32_t> http503HeapGuard;
    static std::atomic<uint32_t> http503Busy;
    static std::atomic<uint32_t> httpOk;

    // TCP socket accept / close tracking
    static std::atomic<uint32_t> tcpOpens;
    static std::atomic<uint32_t> tcpCloses;
    static std::atomic<uint32_t> tcpRejectsLowHeap;

    // Gateway liveness probe
    static std::atomic<uint32_t> gwPingOk;
    static std::atomic<uint32_t> gwPingFail;
    static std::atomic<uint32_t> lastGwPingMs;

    // mDNS announcements
    static std::atomic<uint32_t> mdnsAnnounces;

    // Periodic tick (called every 30s from WebServerBase::loop()).
    // Snapshots RSSI, issues async GW ping, emits logSerial().
    static void tick();

    // Build JSON snapshot of all counters for /api/_diag.
    static String toJson();

    // 4-line formatted dump to Serial via ESP_LOGI("NETDIAG", ...).
    static void logSerial();

    // Callback from async ping end; updates gwPingOk/gwPingFail + lastGwPingMs.
    static void onGwPingResult(bool ok);
};

} // namespace WiFiModule

#endif // NET_DIAG_H
