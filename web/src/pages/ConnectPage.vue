<template>
  <div class="connect-wrap">
    <div class="connect-card card">
      <h2>Connect to CYD Dashboard</h2>
      <p class="text-muted">Enter the IP address or hostname of your ESP32 device.</p>

      <div class="form">
        <div class="field">
          <label>Device IP / Host</label>
          <input v-model="host" placeholder="192.168.1.x  or  cyd.local" @keyup.enter="onConnect" />
        </div>

        <button class="btn" @click="onConnect" :disabled="loading">
          {{ loading ? 'Connecting…' : 'Connect' }}
        </button>

        <p v-if="error" class="text-danger">{{ error }}</p>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { useRouter } from 'vue-router';
import { Api, setBaseUrl, loadBaseUrl } from '../api/http';

const router = useRouter();
const loading = ref(false);
const error = ref('');

const saved = loadBaseUrl();
const host = ref(saved ? saved.replace(/^https?:\/\//, '') : '');

onMounted(async () => {
  if (!saved) return;
  try {
    loading.value = true;
    await Api.ping();
    router.push('/dashboard');
  } catch {
    // silent — stay on form so user can edit host
  } finally {
    loading.value = false;
  }
});

async function onConnect() {
  if (!host.value.trim()) { error.value = 'Enter device address'; return; }
  try {
    loading.value = true;
    error.value = '';
    const url = host.value.startsWith('http') ? host.value : `http://${host.value.trim()}`;
    setBaseUrl(url);
    await Api.ping();
    router.push('/dashboard');
  } catch (e: any) {
    error.value = e.message ?? 'Connection failed. Check IP and that the device is on the same network.';
  } finally {
    loading.value = false;
  }
}
</script>

<style scoped>
.connect-wrap {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: calc(100vh - 48px);
  padding: 24px;
}
.connect-card {
  width: 100%;
  max-width: 420px;
}
.connect-card h2 { margin: 0 0 4px; }
</style>
