<template>
  <div class="page">
    <div class="page-header">
      <h2>Dashboard</h2>
      <button class="btn btn-ghost btn-sm" @click="refresh" :disabled="loading">
        {{ loading ? 'Refreshing…' : '↻ Refresh' }}
      </button>
    </div>

    <p v-if="error" class="text-danger">{{ error }}</p>

    <div class="dash-grid">

      <!-- ── Connection ── -->
      <div class="dash-card">
        <div class="dash-card-title">Connection</div>
        <div class="dash-row">
          <span class="dash-label">Status</span>
          <span :class="connDotClass" class="conn-dot"></span>
          <span>{{ connStateLabel }}</span>
        </div>
        <div class="dash-row">
          <span class="dash-label">Host</span>
          <span class="dash-val mono">{{ host }}</span>
        </div>
      </div>

      <!-- ── WiFi ── -->
      <div class="dash-card">
        <div class="dash-card-title">Wi-Fi</div>
        <div class="dash-row">
          <span class="dash-label">IP</span>
          <span class="dash-val mono">{{ diag?.wifi?.ip || '—' }}</span>
        </div>
        <div class="dash-row">
          <span class="dash-label">RSSI</span>
          <span class="dash-val">{{ diag?.wifi?.rssi != null ? diag.wifi.rssi + ' dBm' : '—' }}</span>
        </div>
        <div class="dash-row">
          <span class="dash-label">Reconnects</span>
          <span class="dash-val">{{ diag?.wifi?.reconnects ?? '—' }}</span>
        </div>
      </div>

      <!-- ── Memory ── -->
      <div class="dash-card">
        <div class="dash-card-title">Memory</div>
        <div class="dash-row">
          <span class="dash-label">Free heap</span>
          <span class="dash-val">{{ fmtBytes(diag?.heap?.free) }}</span>
        </div>
        <div class="dash-row">
          <span class="dash-label">Heap low</span>
          <span class="dash-val" :class="heapMinClass">{{ fmtBytes(diag?.heap?.min) }}</span>
        </div>
        <div class="dash-row" v-if="diag?.heap?.psram != null">
          <span class="dash-label">Free PSRAM</span>
          <span class="dash-val">{{ fmtBytes(diag?.heap?.psram) }}</span>
        </div>
      </div>

      <!-- ── Uptime ── -->
      <div class="dash-card">
        <div class="dash-card-title">Uptime</div>
        <div class="dash-row">
          <span class="dash-label">Up</span>
          <span class="dash-val">{{ fmtUptime(diag?.uptime_ms) }}</span>
        </div>
        <div class="dash-row">
          <span class="dash-label">HTTP ok</span>
          <span class="dash-val">{{ diag?.http?.ok ?? '—' }}</span>
        </div>
        <div class="dash-row">
          <span class="dash-label">503 heap</span>
          <span class="dash-val" :class="diag?.http?.['503_heap'] ? 'text-danger' : ''">
            {{ diag?.http?.['503_heap'] ?? '—' }}
          </span>
        </div>
      </div>

    </div>

    <p class="dash-updated text-muted" v-if="updatedAt">Last updated: {{ updatedAt }}</p>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { loadBaseUrl, getConnectionState, onConnectionStateChange } from '../api/http';
import { wsGetDiag, isWsOpen } from '../api/ws';

const loading  = ref(false);
const error    = ref('');
const diag     = ref<any>(null);
const updatedAt = ref('');
const host     = computed(() => (loadBaseUrl() ?? '').replace(/^https?:\/\//, ''));

const connState = ref(getConnectionState());
let unsubConn: (() => void) | null = null;

const connStateLabel = computed(() => {
  if (connState.value === 'connected') return 'Connected';
  if (connState.value === 'reconnecting') return 'Reconnecting…';
  return 'Disconnected';
});
const connDotClass = computed(() => ({
  'dot-green':  connState.value === 'connected',
  'dot-yellow': connState.value === 'reconnecting',
  'dot-red':    connState.value === 'disconnected',
}));

const heapMinClass = computed(() => {
  const v = diag.value?.heap?.min;
  if (v == null) return '';
  return v < 20000 ? 'text-danger' : v < 50000 ? 'text-warn' : '';
});

function fmtBytes(n?: number): string {
  if (n == null) return '—';
  if (n >= 1024 * 1024) return (n / 1024 / 1024).toFixed(1) + ' MB';
  if (n >= 1024) return (n / 1024).toFixed(1) + ' KB';
  return n + ' B';
}

function fmtUptime(ms?: number): string {
  if (ms == null) return '—';
  const s = Math.floor(ms / 1000);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  if (h > 0) return `${h}h ${m}m`;
  if (m > 0) return `${m}m ${sec}s`;
  return `${sec}s`;
}

async function refresh() {
  if (!isWsOpen()) { error.value = 'WS not connected'; return; }
  loading.value = true;
  error.value = '';
  try {
    diag.value = await wsGetDiag();
    const now = new Date();
    updatedAt.value = `${now.getHours().toString().padStart(2,'0')}:${now.getMinutes().toString().padStart(2,'0')}:${now.getSeconds().toString().padStart(2,'0')}`;
  } catch (e: any) {
    error.value = 'Fetch failed: ' + (e.message ?? 'unknown');
  } finally {
    loading.value = false;
  }
}

let _pollTimer: ReturnType<typeof setInterval> | null = null;

onMounted(() => {
  unsubConn = onConnectionStateChange(s => { connState.value = s; });
  refresh();
  _pollTimer = setInterval(refresh, 5000);
});

onUnmounted(() => {
  unsubConn?.();
  if (_pollTimer) { clearInterval(_pollTimer); _pollTimer = null; }
});
</script>

<style scoped>
.dash-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
  gap: 12px;
  margin-top: 4px;
}

.dash-card {
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: var(--radius, 6px);
  padding: 12px 14px;
}

.dash-card-title {
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: .06em;
  color: var(--muted);
  margin-bottom: 10px;
  padding-bottom: 6px;
  border-bottom: 1px solid var(--border);
}

.dash-row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 0;
  font-size: 13px;
}

.dash-label {
  color: var(--muted);
  min-width: 80px;
  font-size: 12px;
}

.dash-val { color: var(--text); }
.mono { font-family: monospace; font-size: 12px; }

.conn-dot {
  width: 8px; height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}
.dot-green  { background: #22c55e; }
.dot-yellow { background: #f59e0b; }
.dot-red    { background: #ef4444; }

.text-warn { color: #f59e0b; }

.dash-updated {
  margin-top: 12px;
  font-size: 11px;
  text-align: right;
}
</style>
