<template>
  <div class="status-bar">
    <template v-if="connState === 'reconnecting'">
      <span class="dot dot-yellow"></span>
      <span class="reconnecting">Reconnecting…</span>
    </template>
    <template v-else-if="connState === 'disconnected'">
      <span class="dot dot-red"></span>
      <RouterLink to="/connect" class="not-connected">Disconnected</RouterLink>
    </template>
    <template v-else-if="baseUrl && !wsOpen">
      <span class="dot dot-yellow"></span>
      <span class="reconnecting">{{ hostShort }} · WS…</span>
    </template>
    <template v-else-if="baseUrl">
      <span class="dot dot-green"></span>
      <span>{{ hostShort }}</span>
    </template>
    <template v-else>
      <span class="dot dot-red"></span>
      <RouterLink to="/connect" class="not-connected">Not connected</RouterLink>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { loadBaseUrl, getConnectionState, onConnectionStateChange, type ConnectionState } from '../api/http';
import { isWsOpen, onWsState } from '../api/ws';

const baseUrl = ref(loadBaseUrl());
const connState = ref<ConnectionState>(getConnectionState());
const wsOpen = ref(isWsOpen());

const hostShort = computed(() => (baseUrl.value ?? '').replace(/^https?:\/\//, ''));

let unsubConn: (() => void) | null = null;
let unsubWs: (() => void) | null = null;
onMounted(() => {
  unsubConn = onConnectionStateChange(s => { connState.value = s; });
  unsubWs = onWsState(open => { wsOpen.value = open; });
});
onUnmounted(() => { unsubConn?.(); unsubWs?.(); });
</script>

<style scoped>
.status-bar {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: #555;
}
.dot {
  width: 8px; height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}
.dot-green  { background: #22c55e; }
.dot-yellow { background: #f59e0b; }
.dot-red    { background: var(--danger, #ef4444); }
.not-connected { color: var(--danger); }
.reconnecting  { color: #f59e0b; }
</style>
