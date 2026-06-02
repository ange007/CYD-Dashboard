import { ref } from 'vue';
import { Api, loadBaseUrl, type Settings } from './http';
import { connectWs, waitWsOpen, wsGetInit, registerStateRefreshHook } from './ws';
import { useMacrosStore } from '../stores/macros';
import { useWidgetsStore } from '../stores/widgets';

export const initSettings       = ref<Settings | null>(null);
export const initProfiles       = ref<any[]>([]);
export const initActiveProfile  = ref('');
export const initDisplay        = ref<{ w: number; h: number }>({ w: 320, h: 240 });
export const initSdAvailable    = ref<boolean | null>(null);
export const initDone           = ref(false);

interface InitPayload {
  macros?: any[]; widgets?: any[]; settings?: any; profiles?: any[];
  active_profile?: string; display?: { w: number; h: number }; sd_available?: boolean;
}

let _promise: Promise<void> | null = null;
let _initRunning = false;  // true while the async body is executing

function _applyInit(d: InitPayload) {
  const macrosStore  = useMacrosStore();
  const widgetsStore = useWidgetsStore();

  // Don't overwrite a non-empty store with an empty array.
  // A background refresh can arrive with macros=[] when the device's cache is
  // momentarily stale or a second concurrent init races and times-out via HTTP.
  // Only replace when: device sends real data, OR store is currently empty.
  if ((d.macros?.length ?? 0) > 0 || macrosStore.items.length === 0) {
    macrosStore.setItems(d.macros ?? []);
  }
  if ((d.widgets?.length ?? 0) > 0 || widgetsStore.items.length === 0) {
    widgetsStore.setItems(d.widgets ?? []);
  }

  initSettings.value      = d.settings ?? null;
  initProfiles.value      = d.profiles ?? [];
  initActiveProfile.value = d.active_profile ?? '';
  if (d.display && d.display.w > 0 && d.display.h > 0) {
    initDisplay.value = { w: d.display.w, h: d.display.h };
  }
  if (typeof d.sd_available === 'boolean') {
    initSdAvailable.value = d.sd_available;
  }
}

// Run the init flow. Subsequent calls return the same in-flight promise,
// so multiple awaitInit() calls from pages/stores coalesce into one fetch.
export function runInit(): Promise<void> {
  if (_promise) return _promise;
  _initRunning = true;
  _promise = (async () => {
    const base = loadBaseUrl();
    if (!base) return;

    // Kick off WS early — runs in parallel with the init attempt below.
    connectWs(base);

    let ok = false;
    if (await waitWsOpen(2000)) {
      try {
        const d = await wsGetInit(8000);
        _applyInit(d);
        ok = true;
      } catch { /* fall back to HTTP */ }
    }

    if (!ok) {
      try {
        const d = await Api.init();
        _applyInit(d as any);
      } catch { /* both paths failed — pages render with empty state */ }
    }

    initDone.value = true;
  })();
  _promise.finally(() => { _initRunning = false; });
  return _promise;
}

// Wait for the current init run. Starts one if none in-flight.
export function awaitInit(): Promise<void> {
  return _promise ?? runInit();
}

// Reset after disconnect so runInit fetches fresh data on reconnect.
export function resetInit() {
  _promise = null;
  initDone.value = false;
}

// ws.ts calls this hook whenever the device broadcasts state_changed.
// Skip if init is already running — it will complete with fresh data anyway.
registerStateRefreshHook(() => {
  if (_initRunning) return;
  resetInit();
  runInit().catch(() => { /* best effort — ignore network errors here */ });
});
