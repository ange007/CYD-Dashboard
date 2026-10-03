// Increment CACHE_VERSION whenever the web UI is updated so that browsers
// discard the old cache and fetch fresh assets after a firmware/filesystem
// flash.  Without versioning, browsers keep serving the stale cached UI
// indefinitely even after OTA updates.
const CACHE_VERSION = 'cyd-dashboard-v8';
// Cross-origin CDNs we opportunistically cache so the SPA works offline
// after the first online load. opaque responses are safely stored — the
// browser handles them transparently when we return them to <script>/<link>.
const CDN_ORIGINS = ['https://esm.sh', 'https://cdnjs.cloudflare.com'];

self.addEventListener('install', (event) => {
  // Skip waiting immediately without pre-caching.  Pre-caching '/' and
  // '/index.html' opens two extra TCP connections to the ESP32 on every cold
  // install, which exhausts the ~36 KB lwIP heap.  The fetch handler below
  // caches everything on first access instead (demand-fill).
  event.waitUntil(self.skipWaiting());
});

self.addEventListener('activate', (event) => {
  // Delete all caches with a different version name so stale entries are
  // purged without requiring a manual browser cache clear.
  event.waitUntil(
    caches.keys().then((names) =>
      Promise.all(
        names
          .filter((name) => name !== CACHE_VERSION)
          .map((name) => caches.delete(name)),
      ),
    ).then(() => self.clients.claim()),
  );
});

self.addEventListener('fetch', (event) => {
  const { request } = event;
  if (request.method !== 'GET') return;

  const url = new URL(request.url);

  // API and WebSocket calls — always network-first so the UI stays in sync
  // with the device.  If the device is unreachable the request simply fails.
  if (url.pathname.startsWith('/api/') || url.pathname === '/ws') return;

  const isCdn = CDN_ORIGINS.some(o => request.url.startsWith(o));
  const isSameOrigin = url.origin === self.location.origin;
  if (!isCdn && !isSameOrigin) return;  // don't intercept other cross-origin

  // Root path serves different content depending on device mode:
  //   normal mode  → PROXY_PAGE_HTML_GZ (lightweight proxy page)
  //   service mode → SERVICE_BOOTSTRAP_HTML_GZ (WS-bootstrap loader)
  // Caching '/' makes the browser stuck on whichever variant was loaded first.
  // Always go to network for navigation requests so mode transitions work.
  if (isSameOrigin && (url.pathname === '/' || url.pathname === '/index.html')) return;

  // Cache-first with network fallback. CDN responses may be `opaque` (no-cors
  // mode); they still cache + serve correctly for <script>/<link> consumers.
  event.respondWith(
    caches.match(request).then((cached) => {
      if (cached) return cached;
      return fetch(request).then((response) => {
        if (response && (response.status === 200 || response.type === 'opaque')) {
          const clone = response.clone();
          caches.open(CACHE_VERSION).then((cache) => cache.put(request, clone));
        }
        return response;
      });
    }),
  );
});
