// Browser WebSocket client — acts as a fetch proxy for the ESP32.
// When the device sends a "get_url" message, the browser fetches the URL
// (avoiding ESP32 HTTPS/CORS limitations) and sends the result back.

export type WsListener = (msg: any) => void;

let socket: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let _heartbeatTimer: ReturnType<typeof setInterval> | null = null;
let _baseUrl = '';
let _reconnectAttempt = 0;
const MAX_PROXY_SIZE = 64 * 1024;

// Service mode session token — survives WS reconnects inside the device's
// 60 s grace window. Stored in sessionStorage so it clears when the tab closes.
const SVC_TOKEN_KEY = 'cyd_svc_token';
function _getSvcToken(): string {
  try { return sessionStorage.getItem(SVC_TOKEN_KEY) ?? ''; } catch { return ''; }
}
function _setSvcToken(t: string) {
  try { if (t) sessionStorage.setItem(SVC_TOKEN_KEY, t); else sessionStorage.removeItem(SVC_TOKEN_KEY); } catch {}
}

// Debounced refresh trigger for state_changed broadcasts. A single save may
// fan out to several domains (settings+macros via import) so we coalesce
// refresh calls for 150 ms before invalidating the init cache.
// init.ts imports ws.ts so we cannot import statically here — both modules
// would end up in a cycle during initial evaluation. The refresh handler is
// looked up lazily via window in the debounced tick, which runs strictly
// after init.ts finished loading.
let _stateRefreshTimer: ReturnType<typeof setTimeout> | null = null;
let _refreshHook: (() => void) | null = null;
export function registerStateRefreshHook(fn: () => void) { _refreshHook = fn; }
function _scheduleStateRefresh() {
  if (_stateRefreshTimer) return;
  _stateRefreshTimer = setTimeout(() => {
    _stateRefreshTimer = null;
    if (_refreshHook) {
      try { _refreshHook(); } catch { /* swallow — don't break WS loop */ }
    }
  }, 150);
}

// Redirect to proxy page with a reason param, so proxy can show context.
// Only redirect once per session to avoid loops if proxy page itself fails.
let _redirected = false;
function _redirectToProxy(reason: string) {
  if (_redirected) return;
  _redirected = true;
  try { sessionStorage.setItem('cyd_svc_reason', reason); } catch {}
  console.warn('[SVC] redirect to proxy:', reason);
  window.location.href = '/';
}

function _jsonGetPath(obj: any, path: string): any {
  return path.split('.').reduce((cur, key) => {
    if (cur == null) return undefined;
    const idx = Number(key);
    return Array.isArray(cur) && !isNaN(idx) ? cur[idx] : cur[key];
  }, obj);
}

// ── Message listener registry ────────────────────────────────────────────────
// Components subscribe via onMessage() to receive all inbound WS messages.
// This is the mechanism used by MainLayout to detect service_mode_changed.
const _listeners = new Set<WsListener>();

// Active wsRequest reject callbacks — drained on socket close so callers
// fail immediately instead of waiting for the full timeout.
const _pendingRejects = new Set<(e: Error) => void>();

export function onMessage(cb: WsListener): () => void {
  _listeners.add(cb);
  return () => _listeners.delete(cb);
}

// ── WS open/close state subscription ──────────────────────────────────────────
// StatusBar uses this to show a yellow dot while HTTP is reachable but the WS
// (the real control channel) has not opened yet.
const _wsStateListeners = new Set<(open: boolean) => void>();
export function onWsState(cb: (open: boolean) => void): () => void {
  _wsStateListeners.add(cb);
  return () => _wsStateListeners.delete(cb);
}
function _notifyWsState(open: boolean) {
  _wsStateListeners.forEach(fn => { try { fn(open); } catch { /* ignore */ } });
}

export function connectWs(baseUrl: string) {
  if (socket) return;
  _baseUrl = baseUrl;
  const wsUrl = baseUrl.replace(/^http/, 'ws') + '/ws';
  console.log('[WS] Connecting to', wsUrl);
  const ws = new WebSocket(wsUrl);

  ws.onopen = () => {
    console.log('[WS] Connected');
    const wasReconnect = _reconnectAttempt > 0;
    socket = ws;
    _reconnectAttempt = 0;
    _notifyWsState(true);
    // Send a ping every 30 s to keep the service-mode activity timer alive.
    // The device auto-exits service mode after 120 s of no WS messages from
    // the owner; 30 s gives 3× safety margin.
    if (_heartbeatTimer) clearInterval(_heartbeatTimer);
    _heartbeatTimer = setInterval(() => {
      if (socket?.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ action: 'ping' }));
      }
    }, 30000);
    // Re-claim service mode ownership with the stored token. The device keeps
    // service mode alive for 60 s after the previous WS drops, giving the SPA
    // time to reload / reconnect without forcing the user through the proxy
    // page again.
    // Always send enter_service_mode on WS open. Device handles all cases:
    //   - token matches current session → re-claim ownership (new clientId)
    //   - no owner (grace window) → claim ownership
    //   - another owner holds and token mismatch → deny → we redirect to proxy
    // Without this call a fresh SPA (served by device because _serviceMode=true
    // but the previous owner's WS already dropped) would sit with clientId=0 as
    // device's _serviceModeClientId and every save would fail with
    // not_in_service_mode until the 60 s grace expires.
    const savedToken = _getSvcToken();
    ws.send(JSON.stringify({
      action: 'enter_service_mode',
      ...(savedToken ? { token: savedToken } : {}),
    }));
    // If this is a reconnect (WS dropped then came back), the device may have
    // rebooted and exited service mode without sending service_mode_changed.
    // Notify listeners so the SPA can redirect to the proxy page.
    if (wasReconnect) {
      _listeners.forEach(cb => cb({ action: 'ws_reconnected' }));
    }
  };

  ws.onmessage = async (ev) => {
    let msg: any;
    try { msg = JSON.parse(ev.data); } catch { return; }

    // Persist/clear the service-mode session token so the SPA can re-claim
    // ownership after a WS drop (page reload, flaky WiFi, device reboot).
    if (msg.action === 'service_mode_entered' && msg.token) {
      _setSvcToken(msg.token);
    } else if (msg.action === 'service_mode_changed') {
      if (msg.active === true && msg.token) _setSvcToken(msg.token);
      else if (msg.active === false)         _setSvcToken('');
    } else if (msg.action === 'service_mode_denied') {
      // Another client owns service mode and our token doesn't match.
      // Clear stale token, send user to proxy page to re-enter cleanly.
      _setSvcToken('');
      _redirectToProxy('session_taken_by_another_device');
    } else if (msg.action === 'save_ack' && msg.ok === false &&
               msg.error === 'not_in_service_mode') {
      // Save was rejected — our session expired or another client took over.
      _setSvcToken('');
      _redirectToProxy('session_expired');
    } else if (msg.action === 'state_changed') {
      // Device mutated some domain (macros/widgets/profiles/settings) —
      // drop the cached init promise and trigger a background refresh so
      // stores pick up the new authoritative state.
      _scheduleStateRefresh();
    }

    if (msg.action === 'get_url') {
      try {
        const headers: Record<string, string> = {};
        if (Array.isArray(msg.headers)) {
          for (const h of msg.headers) {
            if (h.key) headers[h.key] = h.value;
          }
        }
        const resp = await fetch(msg.url, { headers });
        const cl = resp.headers.get('content-length');
        if (cl && parseInt(cl) > MAX_PROXY_SIZE) throw new Error('Response too large');
        const body = await resp.text();
        if (body.length > MAX_PROXY_SIZE) throw new Error('Response too large');

        let response = body;
        let proxyApplied = false;
        const parse = msg.parse;
        if (parse && resp.ok) {
          try {
            if (parse.type === 'regex' && parse.regex) {
              const m = body.match(new RegExp(parse.regex));
              if (m && parse.template) {
                let result: string = parse.template;
                for (let i = 1; i < m.length; i++)
                  result = result.replaceAll(`{${i}}`, m[i] ?? '');
                response = result;
                proxyApplied = true;
              }
            } else if (parse.type === 'json' && Array.isArray(parse.json_keys) && parse.template) {
              const obj = JSON.parse(body);
              let result: string = parse.template;
              for (const key of parse.json_keys as string[])
                result = result.replaceAll(`{${key}}`, String(_jsonGetPath(obj, key) ?? ''));
              response = result;
              proxyApplied = true;
            }
          } catch { /* invalid regex/JSON — fall back to raw body */ }
        }

        send({
          action: 'return_url_response',
          target: msg.target,
          id: msg.id,
          code: resp.status,
          response,
          ...(proxyApplied && { proxy_applied: true }),
        });
      } catch (e: any) {
        send({
          action: 'return_url_response',
          target: msg.target,
          id: msg.id,
          url: msg.url,
          code: 0,
          response: '',
          fetch_error: e?.message ?? 'fetch failed',
        });
      }
    }

    // Broadcast every message to all subscribers (e.g. MainLayout listening
    // for service_mode_changed so it can reload to the proxy page).
    _listeners.forEach(cb => cb(msg));
  };

  ws.onclose = (ev) => {
    console.warn('[WS] Closed', ev.code, ev.reason);
    socket = null;
    _notifyWsState(false);
    if (_heartbeatTimer) { clearInterval(_heartbeatTimer); _heartbeatTimer = null; }
    if (_pendingRejects.size) {
      const err = new Error('ws_closed');
      _pendingRejects.forEach(fn => fn(err));
      _pendingRejects.clear();
    }
    scheduleReconnect();
  };

  ws.onerror = (ev) => {
    console.error('[WS] Error', ev);
    ws.close();
  };
}

function scheduleReconnect() {
  if (reconnectTimer || !_baseUrl) return;
  const delay = Math.min(1000 * Math.pow(2, _reconnectAttempt), 15000);
  _reconnectAttempt++;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connectWs(_baseUrl);
  }, delay);
}

export function disconnectWs() {
  if (_heartbeatTimer) { clearInterval(_heartbeatTimer); _heartbeatTimer = null; }
  if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
  _baseUrl = '';
  if (socket) { socket.close(); socket = null; }
}

export function send(msg: any) {
  if (socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(msg));
  }
}

export function isWsOpen(): boolean {
  return socket?.readyState === WebSocket.OPEN;
}

// Wait up to `timeoutMs` for the WS socket to reach OPEN state.
// Used by runInit() to decide between WS-based and HTTP-based init.
export function waitWsOpen(timeoutMs = 2000): Promise<boolean> {
  if (isWsOpen()) return Promise.resolve(true);
  return new Promise(resolve => {
    const t0 = Date.now();
    const iv = setInterval(() => {
      if (isWsOpen()) { clearInterval(iv); resolve(true); }
      else if (Date.now() - t0 >= timeoutMs) { clearInterval(iv); resolve(false); }
    }, 50);
  });
}

// Shape of init payload reassembled from WS frames (set_macros, set_widgets,
// set_settings, set_profiles, set_info) — matches the /api/init HTTP response.
export interface WsInitPayload {
  macros: any[];
  widgets: any[];
  settings: any;
  profiles: any[];
  active_profile: string;
  sd_available: boolean;
  display?: { w: number; h: number };
}

// Generate a short random request id. Uses crypto.randomUUID if available,
// falls back to Math.random for very old browsers (never hit on dev devices).
function _newReqId(): string {
  try { return crypto.randomUUID(); } catch {
    return 'r' + Math.random().toString(36).slice(2) + Date.now().toString(36);
  }
}

// Send a WS message that expects exactly one direct reply (matching req_id).
// Rejects on timeout, on socket close before the reply arrives, or when the
// socket is not open at call time. The generic T is the expected reply shape.
export function wsRequest<T = any>(msg: Record<string, any>, timeoutMs = 5000): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    if (!isWsOpen()) return reject(new Error('ws not open'));

    const reqId = _newReqId();
    let settled = false;

    let unsub: () => void;
    let timer: ReturnType<typeof setTimeout>;

    const finish = (fn: () => void) => {
      if (!settled) {
        settled = true;
        unsub?.();
        clearTimeout(timer);
        _pendingRejects.delete(rejectFn);
        fn();
      }
    };

    const rejectFn = (e: Error) => finish(() => reject(e));
    _pendingRejects.add(rejectFn);

    unsub = onMessage((m) => {
      if (m && m.req_id === reqId) {
        finish(() => resolve(m as T));
      }
    });

    timer = setTimeout(() => finish(() => reject(new Error('timeout'))), timeoutMs);

    try {
      send({ ...msg, req_id: reqId });
    } catch (e) {
      finish(() => reject(e instanceof Error ? e : new Error(String(e))));
    }
  });
}

// Request full init payload over WS in a single round-trip.
//
// The device bundles macros + widgets + settings + profiles + display info
// into one init_ack reply. Earlier the device emitted five separate set_*
// frames before init_ack, but esp-httpd's `httpd_ws_send_data_async` queues
// each send through a UDP control socket whose mailbox holds 6 packets —
// combined with the 3 service_mode_* broadcasts that follow enter_service_mode
// the burst silently drops the last 2-3 frames (including init_ack) and the
// SPA times out and falls back to /api/init, which 503s on no-PSRAM boards.
// One bundled message = one mailbox slot = reliable delivery.
export function wsGetInit(timeoutMs = 8000): Promise<WsInitPayload> {
  return new Promise((resolve, reject) => {
    if (!isWsOpen()) return reject(new Error('ws not open'));
    wsRequest<any>({ action: 'get', what: 'init' }, timeoutMs)
      .then((ack) => {
        if (!ack.ok) return reject(new Error(ack.error || 'init_ack not ok'));
        resolve({
          macros:         Array.isArray(ack.macros)   ? ack.macros   : [],
          widgets:        Array.isArray(ack.widgets)  ? ack.widgets  : [],
          settings:       ack.settings ?? {},
          profiles:       Array.isArray(ack.profiles) ? ack.profiles : [],
          active_profile: ack.active_profile ?? '',
          sd_available:   !!ack.sd_available,
          display:        ack.display,
        } as WsInitPayload);
      })
      .catch((e) => reject(e));
  });
}

// Request device diagnostics over WS.
export function wsGetDiag(timeoutMs = 5000): Promise<any> {
  return wsRequest<any>({ action: 'get', what: 'diag' }, timeoutMs)
    .then(ack => {
      const { action: _a, req_id: _r, ...fields } = ack;
      return fields;
    });
}

// Best-effort exit signal when the tab closes (main safety net is WS onClose on ESP32)
window.addEventListener('beforeunload', () => {
  send({ action: 'exit_service_mode' });
});
