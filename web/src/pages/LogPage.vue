<template>
  <div class="page" style="display:flex;flex-direction:column;height:calc(100vh - 48px)">
    <div class="page-header">
      <h2>Device Log</h2>
      <div class="toolbar">
        <button class="btn btn-ghost btn-sm" @click="clear">Clear</button>
      </div>
    </div>

    <pre class="log" ref="logEl">
      <span v-if="!entries.length" style="color:#94a3b8">(waiting for events — press a macro button or trigger a widget update…)</span>
      <template v-for="(e, i) in entries" :key="i"><span :class="'log-line log-' + e.type">{{ e.ts }} {{ e.text }}</span>{{ '\n' }}</template>
    </pre>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, nextTick, onMounted, onUnmounted } from 'vue';
import { loadBaseUrl } from '../api/http';

interface LogEntry { ts: string; text: string; type: string; }

const logEl   = ref<HTMLElement | null>(null);
const entries = ref<LogEntry[]>([]);
let lastSeq   = 0;
let timer: ReturnType<typeof setInterval> | null = null;
let startHandle: ReturnType<typeof setTimeout> | null = null;
let cancelled = false;

function ts() {
  return new Date().toTimeString().slice(0, 8);
}

function classify(msg: any): string {
  if (msg?.action === 'macro_pressed')  return 'action';
  if (msg?.action === 'widget_update')  return 'widget';
  if (msg?.action === 'widget_result')  return msg.error ? 'error' : 'widget-ok';
  return 'info';
}

function format(msg: any): string {
  if (msg?.action === 'macro_pressed')
    return `▶ macro  id="${msg.id}"  type=${msg.type}`;

  if (msg?.action === 'widget_update') {
    const via = msg.data_target === 'url_system' ? 'companion-proxy' : 'direct';
    return `⟳ widget update  id="${msg.id}"  via=${via}  url=${msg.url}`;
  }

  if (msg?.action === 'widget_result') {
    if (msg.error)
      return `✕ widget result  id="${msg.id}"  code=${msg.code}  error: ${msg.error}`;
    return `✓ widget result  id="${msg.id}"  code=${msg.code}  value="${msg.value}"`;
  }

  return JSON.stringify(msg);
}

function push(msg: any) {
  entries.value.push({ ts: ts(), text: format(msg), type: classify(msg) });
  if (entries.value.length > 300) entries.value.shift();
}

function clear() {
  entries.value = [];
  lastSeq = 0;
}

async function poll() {
  const base = loadBaseUrl();
  if (!base) return;
  try {
    const r = await fetch(`${base}/api/log?since=${lastSeq}`);
    if (!r.ok) return;
    const { entries: evs } = await r.json() as { entries: { seq: number; ts: number; data: any }[] };
    for (const e of evs) {
      push(e.data);
      if (e.seq > lastSeq) lastSeq = e.seq;
    }
  } catch { /* device unreachable — silently skip */ }
}

onMounted(() => {
  // Delay first poll to let static assets and /api/init finish first.
  // Capture the setTimeout handle so a fast unmount (user leaves page
  // within 2s of open) cancels before setInterval ever starts, avoiding
  // ghost polling that continues to hammer /api/log after component tear-down.
  startHandle = setTimeout(() => {
    startHandle = null;
    if (cancelled) return;
    poll();
    timer = setInterval(poll, 2000);
  }, 2000);
});

onUnmounted(() => {
  cancelled = true;
  if (startHandle !== null) { clearTimeout(startHandle); startHandle = null; }
  if (timer !== null) { clearInterval(timer); timer = null; }
});

watch(entries, async () => {
  await nextTick();
  if (logEl.value) logEl.value.scrollTop = logEl.value.scrollHeight;
}, { deep: true });
</script>

<style scoped>
.log {
  flex: 1;
  background: #0d1117;
  color: #c9d1d9;
  padding: 12px 14px;
  margin: 0;
  font-size: 12px;
  line-height: 1.6;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-all;
}
.log-line { display: block; }
.log-action    { color: #7ee787; }
.log-fetch     { color: #79c0ff; }
.log-widget    { color: #e3b341; }
.log-widget-ok { color: #a5d6a7; }
.log-error     { color: #f97583; }
.log-info      { color: #c9d1d9; }
</style>
