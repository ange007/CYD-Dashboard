<template>
  <div class="page">
    <div class="page-header">
      <h2>Wi-Fi</h2>
      <div class="toolbar">
        <button class="btn btn-ghost" @click="loadStatus">↻ Status</button>
      </div>
    </div>

    <!-- Current status -->
    <div class="card" style="margin-bottom:16px;display:flex;gap:12px;align-items:center">
      <span class="dot" :class="statusDotClass"></span>
      <span>{{ statusText }}</span>
      <span v-if="status?.ip" class="text-muted" style="font-size:12px">IP: {{ status.ip }}</span>
    </div>

    <!-- mDNS hostname -->
    <div class="form" style="margin-bottom:16px">
      <div class="field">
        <label>mDNS hostname</label>
        <div class="gap-8" style="align-items:center">
          <input v-model="mdnsHost" style="flex:1" placeholder="CYD-Dashboard" maxlength="32" />
          <button class="btn btn-ghost" @click="saveMdns" :disabled="savingMdns">
            {{ savingMdns ? '…' : 'Save' }}
          </button>
        </div>
        <span class="field-hint">
          Device accessible at
          <strong>http://{{ mdnsHost || 'CYD-Dashboard' }}.local</strong>
        </span>
      </div>
      <p v-if="mdnsMessage" :class="mdnsMsgClass" style="margin:4px 0 0">{{ mdnsMessage }}</p>
    </div>

    <!-- Scan + connect -->
    <div class="form">
      <div class="field">
        <label>Network</label>
        <div class="gap-8" style="align-items:center">
          <select v-model="ssid" style="flex:1">
            <option value="" disabled>{{ networks.length ? 'Select network…' : 'Scan first' }}</option>
            <option v-for="n in networks" :key="n.ssid" :value="n.ssid">
              {{ n.ssid }} ({{ n.rssi }} dBm){{ n.open ? ' 🔓' : ' 🔒' }}
            </option>
          </select>
          <button class="btn btn-ghost" @click="scan" :disabled="scanning">
            {{ scanning ? 'Scanning…' : 'Scan' }}
          </button>
        </div>
      </div>

      <div class="field">
        <label>Password</label>
        <input v-model="pass" type="password" placeholder="leave empty for open networks" />
      </div>

      <p v-if="message" :class="msgClass">{{ message }}</p>

      <div class="gap-8">
        <button class="btn" @click="connect" :disabled="connecting || !ssid">
          {{ connecting ? 'Connecting…' : 'Connect' }}
        </button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { Api, type WifiNetwork, type WifiStatus } from '../api/http';
import { awaitInit, initSettings } from '../api/init';
import { Ws } from '../api/wsApi';

const networks   = ref<WifiNetwork[]>([]);
const ssid       = ref('');
const pass       = ref('');
const scanning   = ref(false);
const connecting = ref(false);
const message    = ref('');
const isError    = ref(false);
const status     = ref<WifiStatus | null>(null);

const mdnsHost    = ref('');
const savingMdns  = ref(false);
const mdnsMessage = ref('');
const mdnsMsgClass = computed(() => mdnsError.value ? 'text-danger' : 'text-ok');
const mdnsError   = ref(false);

const STATE_LABELS = ['Disconnected', 'Connecting…', 'Connected', 'Error'];
const statusText = computed(() => status.value ? (STATE_LABELS[status.value.state] ?? 'Unknown') + (status.value.ssid ? ` · ${status.value.ssid}` : '') : '…');
const statusDotClass = computed(() => {
  const s = status.value?.state;
  if (s === 2) return 'dot-green';
  if (s === 1) return 'dot-yellow';
  return 'dot-red';
});
const msgClass = computed(() => isError.value ? 'text-danger' : '');

async function loadStatus() {
  try {
    status.value = await Ws.getWifiStatus();
  } catch { /* ignore */ }
}

async function loadMdns() {
  try {
    await awaitInit();
    mdnsHost.value = initSettings.value?.mdns_host ?? '';
  } catch { /* ignore */ }
}

async function saveMdns() {
  if (!mdnsHost.value.trim()) return;
  savingMdns.value  = true;
  mdnsMessage.value = '';
  try {
    await Ws.saveSettings({ mdns_host: mdnsHost.value.trim() });
    mdnsMessage.value = `Saved — device now at http://${mdnsHost.value.trim()}.local`;
    mdnsError.value   = false;
  } catch (e: any) {
    mdnsMessage.value = 'Save failed: ' + (e.message ?? '');
    mdnsError.value   = true;
  } finally {
    savingMdns.value = false;
  }
}

// Poll an API call until it returns a non-202 result or the attempt limit is reached.
// Returns the final parsed JSON or throws on timeout/error.
async function pollUntilReady<T>(
  apiFn: () => Promise<T>,
  isReady: (v: T) => boolean,
  { attempts = 10, intervalMs = 1500 }: { attempts?: number; intervalMs?: number } = {},
): Promise<T> {
  for (let i = 0; i < attempts; i++) {
    const result = await apiFn();
    if (isReady(result)) return result;
    await new Promise((r) => setTimeout(r, intervalMs));
  }
  throw new Error('Timed out waiting for device response');
}

async function scan() {
  scanning.value = true;
  message.value  = '';
  try {
    // Backend returns 202 {"scanning":true} while the scan runs (async scan).
    // Keep polling until we get a 200 with actual results.
    const results = await pollUntilReady(
      () => Api.wifiScan(),
      (v: any) => Array.isArray(v),   // 200 returns array; 202 returns {scanning:true}
      { attempts: 12, intervalMs: 1500 },
    );
    networks.value = results as WifiNetwork[];
    message.value  = `Found ${networks.value.length} network(s)`;
    isError.value  = false;
  } catch (e: any) {
    message.value = 'Scan failed: ' + (e.message ?? '');
    isError.value = true;
  } finally {
    scanning.value = false;
  }
}

async function connect() {
  connecting.value = true;
  message.value    = '';
  try {
    // Backend now returns 202 immediately and performs the actual connect in
    // the main loop (to avoid blocking the async TCP task).  Poll /wifi/status
    // until state transitions to Connected (2) or Error (3).
    await Ws.connectWifi(ssid.value, pass.value);
    message.value = 'Connecting…';

    const final = await pollUntilReady(
      () => Ws.getWifiStatus(),
      (s) => s.state === 2 || s.state === 3,
      { attempts: 20, intervalMs: 1000 },
    );
    if (final.state === 2) {
      message.value = `Connected! IP: ${final.ip ?? '?'}`;
      isError.value  = false;
      status.value   = final;
    } else {
      message.value = 'Connection failed';
      isError.value  = true;
    }
  } catch (e: any) {
    message.value = 'Error: ' + (e.message ?? '');
    isError.value  = true;
  } finally {
    connecting.value = false;
  }
}

onMounted(() => {
  loadStatus();
  loadMdns();
});
</script>

<style scoped>
.field-hint {
  font-size: 11px;
  color: var(--muted);
  margin-top: 4px;
  display: block;
}
.text-ok { color: #16a34a; }
</style>
