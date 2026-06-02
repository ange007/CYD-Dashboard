import { createApp } from 'vue';
import { createPinia } from 'pinia';
import router from './router';
import MainLayout from './layouts/MainLayout.vue';
import './style.css';

const app = createApp(MainLayout);
app.use(createPinia());
app.use(router);
app.mount('#app');
// Remove shell.html loader overlay (no-op when absent in dev / index.html path)
document.getElementById('_l')?.remove();

// Defer non-critical resource loading to avoid exhausting ESP32's TCP pool
// during initial page load.  favicon is inlined as data-URI in index.html.
// Manifest and service worker are loaded after a delay so the browser fetches
// JS chunks and API data first.
window.addEventListener('load', () => {
  setTimeout(() => {
    // Inject manifest link (deferred to avoid concurrent HTTP request at page load)
    const link = document.createElement('link');
    link.rel = 'manifest';
    link.href = '/manifest.webmanifest';
    document.head.appendChild(link);

    // Register service worker
    if ('serviceWorker' in navigator) {
      navigator.serviceWorker.register('/sw.js').catch(console.error);
    }
  }, 3000);
});
