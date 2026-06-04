#pragma once
#ifndef __WIFI_SERVER_ASYNC_H__
#define __WIFI_SERVER_ASYNC_H__

#include "server_base.h"

#ifdef ESP32
  #include <WiFi.h>
  #include <AsyncTCP.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

namespace WiFiModule
{

// ESPAsyncWebServer backend — implements all library-specific operations.
class AsyncWebServerImpl : public WebServerBase
{
public:
    void start() override;
    bool hasClients() override { return _ws.count() > 0; }

    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                   AwsEventType type, void *arg, uint8_t *data, size_t len);

protected:
    void   wsText(uint32_t clientId, const char *msg) override { _ws.text(clientId, msg); }
    void   wsTextAll(const char *msg) override                 { _ws.textAll(msg); }
    size_t wsClientCount() override                            { return _ws.count(); }
    bool   wsClientQueueFull(uint32_t clientId) override;
    void   wsCloseAll() override                               { _ws.closeAll(); }
    void   wsCloseClient(uint32_t id) override                 { auto *c = _ws.client(id); if (c) c->close(); }
    void   wsCleanup() override                                { _ws.cleanupClients(); }
    bool   wsBinary(uint32_t clientId, const uint8_t* buf, size_t len) override {
        auto *c = _ws.client(clientId);
        if (!c) return false;
        c->binary(const_cast<uint8_t*>(buf), len);
        return true;
    }

private:
    AsyncWebServer _server{80};
    AsyncWebSocket _ws{"/ws"};

    // Static-file sender
    void sendStatic(AsyncWebServerRequest *req, const String &fsPath);

    // Multi-frame WS message reassembly
    void handleWsData(AsyncWebSocketClient *client, void *arg,
                      uint8_t *data, size_t len);

    // Diagnostic HTTP logger (always falls through — canHandle = false)
    class HttpLoggingHandler : public AsyncWebHandler
    {
    public:
        explicit HttpLoggingHandler(AsyncWebServerImpl *owner) : _owner(owner) {}
        bool canHandle(AsyncWebServerRequest *r) const override
        {
            ESP_LOGI("HTTP", "[%s] %s  heap=%u  ws=%u",
                     r->methodToString(), r->url().c_str(),
                     ESP.getFreeHeap(), (unsigned)_owner->wsClientCount());
            return false;
        }
        void handleRequest(AsyncWebServerRequest *) override {}
    private:
        AsyncWebServerImpl *_owner;
    };
};

} // namespace WiFiModule

#endif // __WIFI_SERVER_ASYNC_H__
