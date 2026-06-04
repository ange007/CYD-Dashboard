const envBase = (import.meta.env.VITE_DEFAULT_CYD_HOST as string | undefined) || '';
let baseUrl = '';

function initBaseUrl() {
  if (baseUrl) return;

  // 1) If the app is served from the device itself (not localhost) — always use current origin.
  //    This takes priority over localStorage to avoid stale AP/STA IP confusion: when the
  //    device switches from AP (192.168.4.1) to STA (192.168.x.x), the old AP address
  //    persisted in localStorage would cause WebSocket connections to the wrong host.
  if (typeof window !== 'undefined') {
    const { hostname, origin } = window.location;
    if (hostname && hostname !== 'localhost' && hostname !== '127.0.0.1') {
      baseUrl = origin.replace(/\/+$/, '');
      window.localStorage.setItem('cyd_base_url', baseUrl);
      return;
    }
  }

  // 2) Prefer value from localStorage (dev build pointing at device)
  const saved = typeof window !== 'undefined'
    ? window.localStorage.getItem('cyd_base_url')
    : null;
  if (saved) {
    baseUrl = saved;
    return;
  }

  // 3) Fallback to build-time env (for production builds hosted elsewhere)
  if (envBase) {
    baseUrl = envBase.replace(/\/+$/, '');
  }
}

export function setBaseUrl(url: string) {
  baseUrl = url.replace(/\/+$/, '');
  if (typeof window !== 'undefined') {
    window.localStorage.setItem('cyd_base_url', baseUrl);
  }
}

export function loadBaseUrl(): string {
  if (!baseUrl) initBaseUrl();
  return baseUrl;
}

export function clearBaseUrl() {
  baseUrl = '';
  if (typeof window !== 'undefined') {
    window.localStorage.removeItem('cyd_base_url');
  }
}

// ── Connection state ──────────────────────────────────────────────────────────
export type ConnectionState = 'connected' | 'reconnecting' | 'disconnected';
let _connState: ConnectionState = 'connected';
const _connListeners = new Set<(s: ConnectionState) => void>();

export function getConnectionState(): ConnectionState { return _connState; }

export function onConnectionStateChange(fn: (s: ConnectionState) => void): () => void {
  _connListeners.add(fn);
  return () => _connListeners.delete(fn);
}

function _setConnState(s: ConnectionState) {
  if (_connState === s) return;
  _connState = s;
  _connListeners.forEach(fn => fn(s));
}

let _reconnecting = false;

function _startReconnectLoop(onRestored: () => void) {
  if (_reconnecting) return;
  _reconnecting = true;
  _setConnState('reconnecting');

  let attempts = 0;
  const MAX = 60;

  const tryPing = async () => {
    if (attempts++ >= MAX) {
      _reconnecting = false;
      _setConnState('disconnected');
      return;
    }
    try {
      const base = loadBaseUrl();
      if (!base) { setTimeout(tryPing, 2000); return; }
      const res = await fetch(`${base}/api/health?act=ping`, { signal: AbortSignal.timeout(3000) });
      if (res.ok) {
        _reconnecting = false;
        _setConnState('connected');
        onRestored();
        return;
      }
    } catch { /* still down */ }
    setTimeout(tryPing, 2000);
  };

  setTimeout(tryPing, 2000);
}

// ── Request serializer ────────────────────────────────────────────────────────
// ESP32 AsyncWebServer can handle ~3-5 concurrent connections.  Static files
// (JS/CSS) are managed by the browser and arrive first.  By limiting API
// requests to 1-at-a-time we keep the total connection count safely below 5.
let _inFlight = 0;
const _MAX_CONCURRENT = 1;
const _pending: Array<() => void> = [];

function _acquireSlot(): Promise<void> {
  if (_inFlight < _MAX_CONCURRENT) { _inFlight++; return Promise.resolve(); }
  return new Promise(resolve => _pending.push(resolve));
}

function _releaseSlot(): void {
  _inFlight--;
  const next = _pending.shift();
  if (next) { _inFlight++; next(); }
}

// Callback invoked by MainLayout when the connection is restored, so it can
// re-run /api/init without creating a circular dependency.
let _onRestored: (() => void) | null = null;
export function setOnRestored(fn: () => void) { _onRestored = fn; }

// Handle a 403 from any POST/DELETE — if the body says the device left
// service mode (rebooted, session expired), hop back to the proxy page so
// the user can re-enter instead of getting silent edit failures.
async function _handleServiceModeExpired(res: Response): Promise<boolean> {
  if (res.status !== 403) return false;
  try {
    const body = await res.clone().json();
    if (body?.error === 'not_in_service_mode') {
      console.warn('[HTTP] 403 not_in_service_mode — returning to proxy page');
      window.location.href = '/';
      return true;
    }
  } catch { /* not JSON — caller decides */ }
  return false;
}

async function request<T>(path: string, init?: RequestInit, timeoutMs = 20000, _attempt = 0): Promise<T> {
  await _acquireSlot();
  // Track whether this call still owns the slot so the outer finally never
  // double-releases on the 503-retry path (manual release + finally release).
  let _slotOwned = true;
  try {
    const base = loadBaseUrl();
    if (!base) throw new Error('CYD base URL is not set');

    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);

    try {
      const res = await fetch(`${base}${path}`, {
        ...init,
        signal: controller.signal,
        headers: { 'Content-Type': 'application/json', ...(init?.headers ?? {}) },
      });

      // Device overloaded — release slot now, back off, then re-enter queue.
      // _slotOwned=false prevents the outer finally from releasing again.
      if (res.status === 503 && _attempt === 0) {
        _releaseSlot();
        _slotOwned = false;
        await new Promise(r => setTimeout(r, 1000));
        return request<T>(path, init, timeoutMs, 1);
      }

      if (await _handleServiceModeExpired(res)) {
        throw new Error('not_in_service_mode');
      }

      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      _setConnState('connected');
      return res.json() as Promise<T>;
    } catch (e: any) {
      if (e?.name === 'AbortError') throw new Error('Request timed out');
      // Network error (fetch failed) — start reconnect loop
      if (e instanceof TypeError) {
        _startReconnectLoop(() => { if (_onRestored) _onRestored(); });
      }
      throw e;
    } finally {
      clearTimeout(timer);
    }
  } finally {
    if (_slotOwned) _releaseSlot();
  }
}

export interface WifiNetwork { ssid: string; rssi: number; open: boolean; }
export interface WifiStatus  { state: number; ip?: string; ssid?: string; ap_ip?: string; }
export interface Settings {
  screen_rotate: number;   // 0-3
  theme:         number;   // 0=light 1=dark
  brightness:    number;   // 0-100 %
  btn_size:      number;   // 50 | 70 | 90 | 110 px
  mdns_host:     string;   // mDNS hostname (.local)
  startup_tab:   number;   // 0 = macros, 1 = widgets
  def_macro_bg?:       number;  // 24-bit RGB
  def_macro_icon_clr?: number;  // 24-bit RGB (0 = auto)
  def_macro_icon_sz?:  number;  // 0=s, 1=m, 2=l
  def_macro_image_sz?: number;  // 0=s(50%), 1=m(75%), 2=l(100%)
  def_widget_bg?:      number;  // 24-bit RGB
  macro_radius?:     number;   // 0-50 corner radius (255=circle)
  macro_bg_opa?:     number;   // 0-255 button background opacity
  widget_bg_opa?:    number;   // 0-255 widget card background opacity
  macro_title_pos?:  number;   // 0=inside, 1=below, 2=hidden
  macro_shadow?:     boolean;  // true = show shadow
  macro_brd_clr?: number;   // 0 = no border, otherwise 24-bit RGB
}

export const Api = {
  ping:         () => request<{ ok: boolean }>('/api/health?act=ping'),
  init: (() => {
    let _etag: string | null = null;
    let _cached: any = null;
    return async () => {
      const base = loadBaseUrl();
      if (!base) throw new Error('CYD base URL is not set');
      await _acquireSlot();
      let _slotOwned = true;
      try {
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), 20000);
        try {
          const headers: Record<string, string> = { 'Content-Type': 'application/json' };
          if (_etag) headers['If-None-Match'] = _etag;
          const res = await fetch(`${base}/api/init`, { signal: controller.signal, headers });
          if (res.status === 304 && _cached) {
            _setConnState('connected');
            return _cached as {
              macros: any[]; widgets: any[]; settings: Settings;
              profiles: any[]; active_profile: string;
              display?: { w: number; h: number };
            };
          }
          if (res.status === 503 && _slotOwned) {
            _releaseSlot();
            _slotOwned = false;
            await new Promise(r => setTimeout(r, 1000));
            return (Api.init as () => Promise<any>)();
          }
          if (!res.ok) throw new Error(`HTTP ${res.status}`);
          const body = await res.json();
          const et = res.headers.get('ETag');
          if (et) _etag = et;
          _cached = body;
          _setConnState('connected');
          return body;
        } catch (e: any) {
          if (e?.name === 'AbortError') throw new Error('Request timed out');
          if (e instanceof TypeError) {
            _startReconnectLoop(() => { if (_onRestored) _onRestored(); });
          }
          throw e;
        } finally {
          clearTimeout(timer);
        }
      } finally {
        if (_slotOwned) _releaseSlot();
      }
    };
  })(),
  wifiScan:     () => request<WifiNetwork[]>('/api/wifi?act=scan'),
  exportConfig: () => request<any>('/api/settings?act=export'),
  getIcons:     () => request<string[]>('/api/icons'),
  uploadIcon: async (file: File): Promise<{ ok: boolean }> => {
    await _acquireSlot();
    try {
      const base = loadBaseUrl();
      if (!base) throw new Error('CYD base URL is not set');
      const fd = new FormData();
      fd.append('file', file, file.name);
      const res = await fetch(`${base}/api/icons`, { method: 'POST', body: fd });
      if (await _handleServiceModeExpired(res)) throw new Error('not_in_service_mode');
      if (!res.ok) throw new Error(`Upload failed: HTTP ${res.status}`);
      return res.json();
    } finally {
      _releaseSlot();
    }
  },
  uploadIconSrc: async (file: File): Promise<{ ok: boolean }> => {
    await _acquireSlot();
    try {
      const base = loadBaseUrl();
      if (!base) throw new Error('CYD base URL is not set');
      const fd = new FormData();
      fd.append('file', file, file.name);
      const res = await fetch(`${base}/api/icons?act=src`, { method: 'POST', body: fd });
      if (await _handleServiceModeExpired(res)) throw new Error('not_in_service_mode');
      if (!res.ok) throw new Error(`Upload failed: HTTP ${res.status}`);
      return res.json();
    } finally {
      _releaseSlot();
    }
  },
  /** Get full URL for a source icon (for browser-side re-optimization) */
  getIconSrcUrl: (name: string): string => {
    const base = loadBaseUrl();
    return `${base}/icons/src/${encodeURIComponent(name)}`;
  },
  getBackgrounds:   () => request<{ name: string; size: number }[]>('/api/backgrounds'),
  uploadBackground: async (file: File): Promise<{ ok: boolean }> => {
    await _acquireSlot();
    try {
      const base = loadBaseUrl();
      if (!base) throw new Error('CYD base URL is not set');
      const fd = new FormData();
      fd.append('file', file, file.name);
      const res = await fetch(`${base}/api/backgrounds`, { method: 'POST', body: fd });
      if (await _handleServiceModeExpired(res)) throw new Error('not_in_service_mode');
      if (!res.ok) throw new Error(`Upload failed: HTTP ${res.status}`);
      return res.json();
    } finally {
      _releaseSlot();
    }
  },
  getSceneBgs:    () => request<Record<string, string>>('/api/backgrounds?act=scene-bg'),
};
