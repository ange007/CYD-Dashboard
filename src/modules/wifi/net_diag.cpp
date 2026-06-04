#include "net_diag.h"

#include <WiFi.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include "ping/ping_sock.h"
#include "lwip/inet.h"
#include "lwip/tcp.h"

// lwIP private list head. Declared extern here (instead of pulling
// lwip/priv/tcp_priv.h) so we can iterate without including private headers.
extern "C" struct tcp_pcb *tcp_tw_pcbs;
extern "C" struct tcp_pcb *tcp_active_pcbs;

namespace WiFiModule
{

// ── Static counter storage ───────────────────────────────────────────────────
std::atomic<uint32_t> NetDiag::wifiDisconnects{0};
std::atomic<uint32_t> NetDiag::wifiReconnects{0};
std::atomic<int32_t>  NetDiag::lastRssi{0};
std::atomic<uint32_t> NetDiag::lastGotIpMs{0};

std::atomic<uint32_t> NetDiag::httpEnter{0};
std::atomic<uint32_t> NetDiag::http503HeapGuard{0};
std::atomic<uint32_t> NetDiag::http503Busy{0};
std::atomic<uint32_t> NetDiag::httpOk{0};

std::atomic<uint32_t> NetDiag::tcpOpens{0};
std::atomic<uint32_t> NetDiag::tcpCloses{0};
std::atomic<uint32_t> NetDiag::tcpRejectsLowHeap{0};

std::atomic<uint32_t> NetDiag::gwPingOk{0};
std::atomic<uint32_t> NetDiag::gwPingFail{0};
std::atomic<uint32_t> NetDiag::lastGwPingMs{0};

std::atomic<uint32_t> NetDiag::mdnsAnnounces{0};

// ── Tick ─────────────────────────────────────────────────────────────────────
void NetDiag::tick()
{
    // Snapshot WiFi state
    lastRssi.store(WiFi.RSSI());  // returns 0 when disconnected (valid RSSI is always negative)

    // Issue one-shot ping to gateway (async — result arrives in on_ping_end)
    IPAddress gw = WiFi.gatewayIP();
    if (gw != IPAddress(0, 0, 0, 0) && WiFi.status() == WL_CONNECTED)
    {
        esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
        config.count = 1;              // one-shot
        config.interval_ms = 0;
        config.timeout_ms = 1000;      // 1s
        ip_addr_t target;
        IP_ADDR4(&target, gw[0], gw[1], gw[2], gw[3]);
        config.target_addr = target;

        esp_ping_callbacks_t cbs{};
        cbs.on_ping_end = [](esp_ping_handle_t h, void* args) {
            uint32_t received = 0;
            esp_ping_get_profile(h, ESP_PING_PROF_REPLY, &received, sizeof(received));
            NetDiag::onGwPingResult(received > 0);
            esp_ping_delete_session(h);
        };
        cbs.cb_args = nullptr;

        esp_ping_handle_t handle = nullptr;
        if (esp_ping_new_session(&config, &cbs, &handle) == ESP_OK && handle)
        {
            if (esp_ping_start(handle) != ESP_OK)
            {
                esp_ping_delete_session(handle);
                gwPingFail++;
            }
        }
    }

    logSerial();
}

// ── Serial dump (4 lines) ────────────────────────────────────────────────────
void NetDiag::logSerial()
{
    const wl_status_t st = WiFi.status();
    const char* wifiState =
        st == WL_CONNECTED       ? "CONN"    :
        st == WL_DISCONNECTED    ? "DISC"    :
        st == WL_IDLE_STATUS     ? "IDLE"    :
        st == WL_CONNECTION_LOST ? "LOST"    :
        st == WL_NO_SSID_AVAIL   ? "NO_SSID" :
        "OTHER";

    ESP_LOGI("NETDIAG", "wifi=%s rssi=%d ip=%s uptime=%us  disc=%u rec=%u",
             wifiState, (int)lastRssi.load(),
             WiFi.localIP().toString().c_str(),
             (unsigned)(millis() / 1000),
             (unsigned)wifiDisconnects.load(),
             (unsigned)wifiReconnects.load());

    ESP_LOGI("NETDIAG", "http enter=%u ok=%u 503heap=%u 503busy=%u",
             (unsigned)httpEnter.load(), (unsigned)httpOk.load(),
             (unsigned)http503HeapGuard.load(), (unsigned)http503Busy.load());

    // Count lwIP TCP pcbs in TIME_WAIT and ACTIVE lists. TIME_WAIT entries
    // linger ~60s after close per RFC 793 and each holds ~250B in DRAM — a
    // suspected source of heap fragmentation across service-mode cycles.
    unsigned tw = 0, act = 0;
    for (struct tcp_pcb *p = tcp_tw_pcbs; p; p = p->next) tw++;
    for (struct tcp_pcb *p = tcp_active_pcbs; p; p = p->next) act++;
    ESP_LOGI("NETDIAG", "tcp open=%u close=%u reject_lowheap=%u  tw=%u active=%u  heap_lf=%u",
             (unsigned)tcpOpens.load(), (unsigned)tcpCloses.load(),
             (unsigned)tcpRejectsLowHeap.load(),
             tw, act,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    ESP_LOGI("NETDIAG", "gw ping ok=%u fail=%u | mdns ann=%u",
             (unsigned)gwPingOk.load(), (unsigned)gwPingFail.load(),
             (unsigned)mdnsAnnounces.load());
}

// ── JSON builder ─────────────────────────────────────────────────────────────
String NetDiag::toJson()
{
    String s;
    s.reserve(640);
    s += "{\"uptime_ms\":";  s += (uint32_t)millis();
    s += ",\"wifi\":{\"status\":";       s += (int)WiFi.status();
    s += ",\"rssi\":";                    s += (int)lastRssi.load();
    s += ",\"ip\":\"";                    s += WiFi.localIP().toString(); // IPAddress::toString() returns "A.B.C.D" — no JSON-unsafe chars
    s += "\",\"disconnects\":";           s += wifiDisconnects.load();
    s += ",\"reconnects\":";              s += wifiReconnects.load();
    s += ",\"last_got_ip_ms\":";          s += lastGotIpMs.load();
    s += "},\"http\":{\"enter\":";        s += httpEnter.load();
    s += ",\"ok\":";                      s += httpOk.load();
    s += ",\"503_heap\":";                s += http503HeapGuard.load();
    s += ",\"503_busy\":";                s += http503Busy.load();
    s += "},\"tcp\":{\"opens\":";         s += tcpOpens.load();
    s += ",\"closes\":";                  s += tcpCloses.load();
    s += ",\"reject_lowheap\":";          s += tcpRejectsLowHeap.load();
    s += "},\"gw\":{\"ping_ok\":";        s += gwPingOk.load();
    s += ",\"ping_fail\":";               s += gwPingFail.load();
    s += ",\"last_ping_ms\":";            s += lastGwPingMs.load();
    s += "},\"mdns\":{\"announces\":";    s += mdnsAnnounces.load();
    s += "},\"heap\":{\"free\":";         s += ESP.getFreeHeap();
    s += ",\"min\":";                     s += ESP.getMinFreeHeap();
    s += ",\"psram\":";                   s += ESP.getFreePsram();
    s += "}}";
    return s;
}

// ── Ping callback stub (Task 3 fills real body) ──────────────────────────────
void NetDiag::onGwPingResult(bool ok)
{
    if (ok) gwPingOk++; else gwPingFail++;
    lastGwPingMs.store(millis());
}

} // namespace WiFiModule
