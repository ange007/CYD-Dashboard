<template>
  <div class="app">
    <header class="top-bar">
      <RouterLink to="/dashboard" class="logo">CYD Dashboard</RouterLink>
      <nav class="tabs">
        <RouterLink to="/dashboard">Dashboard</RouterLink>
        <RouterLink to="/macros">Macros</RouterLink>
        <RouterLink to="/widgets">Widgets</RouterLink>
        <RouterLink to="/wifi">Wi-Fi</RouterLink>
        <RouterLink to="/settings">Settings</RouterLink>
        <RouterLink to="/profiles">Profiles</RouterLink>
        <RouterLink to="/log">Log</RouterLink>
      </nav>
      <StatusBar />
      <button class="exit-svc-btn" @click="exitServiceMode" title="Exit Settings — returns to proxy page">&#x23FB; Exit</button>
    </header>
    <main class="main">
      <RouterView />
    </main>
  </div>
</template>

<script setup lang="ts">
import { onMounted, onUnmounted, provide, watch } from 'vue';
import { useRoute } from 'vue-router';
import StatusBar from '../components/StatusBar.vue';
import { loadBaseUrl, setOnRestored } from '../api/http';
import { disconnectWs, send, onMessage } from '../api/ws';
import {
  initSettings, initProfiles, initActiveProfile, initDisplay,
  initDone, initSdAvailable, runInit, resetInit,
} from '../api/init';

// Track WS unsubscribe and pending exit-navigation timer for cleanup.
let _unsubWs: (() => void) | null = null;
let _exitNavTimer: ReturnType<typeof setTimeout> | null = null;

provide('initSettings',       initSettings);
provide('initProfiles',       initProfiles);
provide('initActiveProfile',  initActiveProfile);
provide('initDisplay',        initDisplay);
provide('initDone',           initDone);
provide('initSdAvailable',    initSdAvailable);

const route = useRoute();

// Re-run init after the reconnect loop restores the connection.
function onRestored() {
  resetInit();
  runInit();
}

onMounted(() => {
  const theme = localStorage.getItem('cyd_theme');
  if (theme === 'dark') document.documentElement.setAttribute('data-theme', 'dark');

  // Register reconnect callback so http.ts can trigger re-init without circular deps
  setOnRestored(onRestored);

  // If we landed directly on a data page (e.g. /macros), init immediately
  if (route.meta.requiresConnection && loadBaseUrl()) runInit();

  // Detect when the device exits service mode (timeout, crash, manual exit).
  // The device broadcasts service_mode_changed:{active:false} and closes the WS.
  // Without this handler the SPA stays alive in "zombie" state: the browser
  // still shows the full SPA, but the device is in normal mode — API calls fail
  // with 503 "busy" (HEAP_GUARD fires), widget timers appear active on device.
  _unsubWs = onMessage((msg) => {
    if (msg.action === 'service_mode_changed' && msg.active === false) {
      console.log('[SVC] Service mode exited — reloading to proxy page');
      // Cancel the fallback timer from exitServiceMode() — server confirmed exit,
      // so the 150 ms timer is redundant and could re-navigate the proxy page.
      if (_exitNavTimer !== null) { clearTimeout(_exitNavTimer); _exitNavTimer = null; }
      disconnectWs();
      window.location.href = '/';
    }
    // WS dropped and reconnected — device likely rebooted and is no longer in
    // service mode. Redirect to proxy page so user re-enters cleanly instead of
    // remaining in the SPA zombie state (device in normal mode, browser in SPA).
    if (msg.action === 'ws_reconnected') {
      console.log('[SVC] WS reconnected after disconnect — returning to proxy page');
      disconnectWs();
      window.location.href = '/';
    }
  });
});

onUnmounted(() => {
  _unsubWs?.();
  if (_exitNavTimer !== null) { clearTimeout(_exitNavTimer); _exitNavTimer = null; }
  disconnectWs();
});

function exitServiceMode() {
  send({ action: 'exit_service_mode' });
  // Navigate back to proxy page after a short delay so the WS message is sent.
  // This timer is cancelled by the service_mode_changed handler if the server
  // confirms exit first (normal case). It fires only as a fallback (WS dead).
  _exitNavTimer = setTimeout(() => {
    _exitNavTimer = null;
    disconnectWs();
    window.location.href = '/';
  }, 150);
}

// Also trigger init when navigating away from /connect to a data page
watch(() => route.path, () => {
  if (route.meta.requiresConnection && !initDone.value && loadBaseUrl()) runInit();
});
</script>

<style scoped>
.app { display: flex; flex-direction: column; height: 100vh; }

.top-bar {
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 0 20px;
  height: 48px;
  background: #1e293b;
  color: #f1f5f9;
  flex-shrink: 0;
}

.logo {
  font-weight: 700;
  font-size: 15px;
  color: #f1f5f9;
  letter-spacing: .01em;
  white-space: nowrap;
}

.tabs {
  display: flex;
  gap: 4px;
}
.tabs a {
  color: #94a3b8;
  padding: 6px 12px;
  border-radius: 4px;
  font-size: 14px;
  transition: color 0.15s, background 0.15s;
  text-decoration: none;
}
.tabs a:hover { color: #f1f5f9; background: rgba(255,255,255,.08); }
.tabs a.router-link-active { color: #f1f5f9; font-weight: 600; background: rgba(255,255,255,.12); }

.main { flex: 1; overflow: auto; }

.exit-svc-btn {
  padding: 5px 12px;
  border: 1px solid #475569;
  border-radius: 4px;
  background: transparent;
  color: #94a3b8;
  font-size: 13px;
  cursor: pointer;
  white-space: nowrap;
  transition: color 0.15s, border-color 0.15s;
}
.exit-svc-btn:hover { color: #f1f5f9; border-color: #94a3b8; }
</style>
