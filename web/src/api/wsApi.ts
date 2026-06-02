import { wsRequest } from './ws';
import type { WifiStatus } from './http';

export interface WsAck { ok: boolean; error?: string; req_id?: string; }

/** Throw if ack.ok is false — converts save_ack failure into a catchable error. */
function _check(ack: WsAck): WsAck {
  if (!ack.ok) throw new Error(ack.error || 'save failed');
  return ack;
}

export const Ws = {
  saveMacros:   (items: any[]) =>
    wsRequest<WsAck>({ action: 'save', what: 'macros', items }, 10000).then(_check),
  saveWidgets:  (items: any[]) =>
    wsRequest<WsAck>({ action: 'save', what: 'widgets', items }, 10000).then(_check),
  saveSettings: (items: Record<string, any>) =>
    wsRequest<WsAck>({ action: 'save', what: 'settings', items }, 5000).then(_check),
  saveProfiles: (items: any[]) =>
    wsRequest<WsAck>({ action: 'save', what: 'profiles', items }, 10000).then(_check),
  setActiveProfile: (id: string) =>
    wsRequest<WsAck>({ action: 'save', what: 'active_profile', id }, 5000).then(_check),
  setSceneBg: (scene_id: string, image: string) =>
    wsRequest<WsAck>({ action: 'save', what: 'scene_bg', scene_id, image }, 5000).then(_check),
  importConfig: (payload: any) =>
    wsRequest<WsAck>({ action: 'save', what: 'import', payload }, 15000).then(_check),
  connectWifi: (ssid: string, pass: string) =>
    wsRequest<WsAck>({ action: 'save', what: 'wifi', ssid, pass }, 5000).then(_check),
  reboot: () =>
    wsRequest<WsAck>({ action: 'reboot' }, 5000).then(_check),
  deleteIcon: (name: string) =>
    wsRequest<WsAck>({ action: 'delete', what: 'icon', name }, 5000).then(_check),
  deleteBackground: (name: string) =>
    wsRequest<WsAck>({ action: 'delete', what: 'background', name }, 5000).then(_check),
  getWifiStatus: () =>
    wsRequest<WifiStatus>({ action: 'get', what: 'wifi_status' }, 5000),
};
