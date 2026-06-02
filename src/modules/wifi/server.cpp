// server.cpp — static facade; delegates all work to the selected backend.
// Backend is chosen at compile time via USE_PSYCHIC_HTTP / default = async.
#include "server.h"

#ifdef USE_PSYCHIC_HTTP
  #include "server_psychic.h"
#else
  #include "server_async.h"
#endif

namespace WiFiModule
{

WebServerBase *WSServer::_impl = nullptr;

void WSServer::start()
{
#ifdef USE_PSYCHIC_HTTP
    static PsychicHttpImpl impl;
#else
    static AsyncWebServerImpl impl;
#endif
    _impl = &impl;
    _impl->start();
}

void WSServer::loop()
{
    if (_impl) _impl->loop();
}

bool WSServer::sendCommandToClient(uint32_t clientId, const char *jsonStr)
{
    return _impl ? _impl->sendCommandToClient(clientId, jsonStr) : false;
}

bool WSServer::sendCommandToClient(uint32_t clientId, cJsonPtr&& json)
{
    if (_impl) return _impl->sendCommandToClient(clientId, std::move(json));
    return false;   // json freed on scope exit
}

bool WSServer::sendBinaryToClient(uint32_t clientId, const uint8_t* buf, size_t len)
{
    return _impl ? _impl->sendBinaryToClient(clientId, buf, len) : false;
}

bool WSServer::sendCommand(const char *json)
{
    return _impl ? _impl->sendCommand(json) : false;
}

bool WSServer::sendCommand(cJsonPtr&& json)
{
    if (_impl) return _impl->sendCommand(std::move(json));
    return false;
}

void WSServer::broadcastSettingInt(const char *key, int value)
{
    if (_impl) _impl->broadcastSettingInt(key, value);
}

bool WSServer::sendCommandToProxy(cJsonPtr&& json)
{
    if (_impl) return _impl->sendCommandToProxy(std::move(json));
    return false;
}

uint32_t WSServer::getProxyClientId()
{
    return _impl ? _impl->getProxyClientId() : 0;
}

void WSServer::clearProxyClientId()
{
    if (_impl) _impl->clearProxyClientId();
}

uint32_t WSServer::getCompanionClientId()
{
    return _impl ? _impl->getCompanionClientId() : 0;
}

void WSServer::setCompanionClientId(uint32_t id)
{
    if (_impl) _impl->setCompanionClientId(id);
}

void WSServer::triggerWidgetsRefresh()
{
    if (_impl) _impl->triggerWidgetsRefresh();
}

void WSServer::requestProfileSwitch(const char *profileId)
{
    if (_impl) _impl->requestProfileSwitch(profileId);
}

void WSServer::applySettingsResult(const uSettings::SettingsApplyResult& r)
{
    if (_impl) _impl->applySettingsResult(r);
}

void WSServer::triggerSceneBgReload()
{
    if (_impl) _impl->triggerSceneBgReload();
}

void WSServer::stageWifiConnect(const char* ssid, const char* pass)
{
    if (_impl) _impl->stageWifiConnect(ssid, pass);
}

bool WSServer::hasClients()
{
    return _impl ? _impl->hasClients() : false;
}

bool WSServer::isWebServing()
{
    return _impl ? _impl->isWebServing() : false;
}

bool WSServer::isServiceMode()
{
    return _impl ? _impl->isServiceMode() : false;
}

bool WSServer::enterServiceMode(uint32_t clientId, const char* token)
{
    return _impl ? _impl->enterServiceMode(clientId, token) : false;
}

void WSServer::exitServiceMode()
{
    if (_impl) _impl->exitServiceMode();
}

const char* WSServer::getServiceModeToken()
{
    return _impl ? _impl->getServiceModeToken() : "";
}

uint32_t WSServer::getServiceModeClientId()
{
    return _impl ? _impl->getServiceModeClientId() : 0;
}

void WSServer::markWidgetsDirty()
{
    if (_impl) _impl->markWidgetsDirty();
}

void WSServer::pushLogEvent(cJsonPtr&& json)
{
    if (_impl) _impl->pushLogEvent(std::move(json));
    // else json freed on scope exit
}

void WSServer::pushLogEvent(const char* jsonStr)
{
    if (_impl) _impl->pushLogEvent(jsonStr);
}

} // namespace WiFiModule
