#pragma once
#ifndef __WIFI_SERVER_PSYCHIC_H__
#define __WIFI_SERVER_PSYCHIC_H__

#include "server_base.h"

#include <PsychicHttp.h>
#include <LittleFS.h>
#include <array>

namespace WiFiModule
{

// PsychicHttp backend — implements all library-specific operations.
class PsychicHttpImpl : public WebServerBase
{
public:
    void start() override;
    void startHttpServer() override;
    bool hasClients() override;

protected:
    void   wsText(uint32_t clientId, const char *msg) override;
    void   wsTextAll(const char *msg) override { _wsHandler.sendAll(msg); }
    size_t wsClientCount() override;
    bool   wsClientQueueFull(uint32_t /*clientId*/) override { return false; }
    void   wsCloseAll() override;
    void   wsCloseClient(uint32_t id) override;
    void   wsCleanup() override {} // PsychicHttp manages cleanup automatically
    bool   wsBinary(uint32_t clientId, const uint8_t* buf, size_t len) override;

private:
    PsychicHttpServer     _server;   // default port 80
    PsychicWebSocketHandler _wsHandler;

    // Fixed-size client registry. Replaces a std::map<uint32_t, ...> whose
    // per-insert/per-erase node allocations (~40-60 B at varying heap
    // positions) were a cross-cycle fragmentation source. The httpd config
    // caps concurrent sockets at WS_MAX_CLIENTS (declared in server_base.h),
    // so a static slot table is sufficient. id == 0 marks an empty slot.
    struct WsSlot {
        uint32_t id = 0;
        PsychicWebSocketClient* client = nullptr;
    };
    std::array<WsSlot, WS_MAX_CLIENTS> _wsSlots {};

    // Helpers — array lookup is O(WS_MAX_CLIENTS) = O(4), no heap.
    WsSlot* _findSlot(uint32_t id);
    WsSlot* _claimSlot(uint32_t id, PsychicWebSocketClient* c);
    void    _releaseSlot(uint32_t id);

};

} // namespace WiFiModule

#endif // __WIFI_SERVER_PSYCHIC_H__
