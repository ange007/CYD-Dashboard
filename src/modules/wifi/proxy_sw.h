#pragma once

// Service worker for proxy mode PWA.
// Caches the proxy page, manifest, and icon for offline launch.
// Never caches /ws (WebSocket upgrade must always go to device).
static const char PROXY_SW_JS[] =
"const C='cyd-proxy-v1';"
"const A=['/','/manifest.json','/icons/icon-192.png'];"
"self.addEventListener('install',function(e){"
"  e.waitUntil(caches.open(C).then(function(c){return c.addAll(A);}));"
"  self.skipWaiting();"
"});"
"self.addEventListener('activate',function(e){"
"  e.waitUntil(clients.claim());"
"});"
"self.addEventListener('fetch',function(e){"
"  if(e.request.url.indexOf('/ws')!==-1)return;"
"  if(e.request.mode==='navigate'){"
"    e.respondWith(fetch(e.request).catch(function(){return caches.match('/');}));"
"    return;"
"  }"
"  e.respondWith(caches.match(e.request).then(function(r){return r||fetch(e.request);}));"
"});";
