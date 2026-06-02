import { defineConfig, loadEnv, type Plugin } from 'vite';
import vue from '@vitejs/plugin-vue';
import type { OutputAsset, OutputChunk } from 'rollup';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { gzipSync } from 'node:zlib';

/**
 * Injects CSS into the JS bundle (so both HTTP and WS delivery paths apply styles),
 * and removes <link rel="modulepreload"> hints to force sequential chunk loading.
 *
 * Why CSS-in-JS instead of CSS-in-HTML:
 *   The no-PSRAM service-mode path delivers app.js via WebSocket binary frames and
 *   mounts Vue via `import(blobUrl)`. There is no index.html in this flow, so CSS
 *   inlined into HTML is never applied. Injecting CSS as a self-executing block at
 *   the top of app.js ensures styles are applied regardless of delivery mechanism.
 *
 * Why modulepreload removal:
 *   Vite emits <link rel="modulepreload"> hints that cause the browser to fetch all
 *   JS chunks in parallel. On ESP32 this exhausts the TCP connection pool, causing
 *   heap pressure and stalled transfers. Removing the hints forces sequential loading.
 */
function inlineCssPlugin(): Plugin {
  let capturedCss = '';
  let cssWasFound = false;

  return {
    name: 'css-inject-into-js',
    apply: 'build',

    // Phase 1: capture all CSS assets, remove them from file output, inject into JS.
    generateBundle(_opts, bundle) {
      const cssKeys = Object.keys(bundle).filter(k => k.endsWith('.css'));
      for (const key of cssKeys) {
        const asset = bundle[key] as OutputAsset;
        const src = asset.source;
        capturedCss += typeof src === 'string'
          ? src
          : new TextDecoder().decode(src as Uint8Array);
        cssWasFound = true;
        delete bundle[key];  // remove CSS file; delivery handled by JS injection
      }

      if (!capturedCss.trim()) return;

      // Prepend a self-executing CSS injector to the entry JS chunk.
      const mainEntry = Object.values(bundle).find(
        (b): b is OutputChunk => b.type === 'chunk' && b.isEntry
      );
      if (mainEntry) {
        const escaped = JSON.stringify(capturedCss);
        mainEntry.code =
          `(function(){if(typeof document!=="undefined"){` +
          `var _s=document.createElement("style");_s.textContent=${escaped};` +
          `document.head.appendChild(_s);}})();\n` +
          mainEntry.code;
      }
    },

    // Phase 2: remove dangling <link rel=stylesheet> and modulepreload hints.
    transformIndexHtml: {
      order: 'post',
      handler(html) {
        if (cssWasFound) {
          // Remove any <link rel=stylesheet> that pointed to the now-deleted CSS file.
          html = html.replace(/<link\b[^>]*rel=["']?stylesheet["']?[^>]*assets\/[^>]*\.css[^>]*>\s*/g, '');
          html = html.replace(/<link\b[^>]*assets\/[^>]*\.css[^>]*rel=["']?stylesheet["']?[^>]*>\s*/g, '');
        }
        html = html.replace(/<link rel="modulepreload"[^>]*>\n?/g, '');
        return html;
      },
    },
  };
}

/**
 * Generates `src/modules/wifi/service_shell.h` from `web/shell.html` after the
 * Vite build completes. Substitutes `__SPA_VERSION__` with the value from the
 * SPA_VERSION env var (or falls back to "v0.0.1"). Gzip-compresses the result
 * so the device can serve it in a single TCP send on no-PSRAM hardware.
 */
function generateShellHeaderPlugin(): Plugin {
  return {
    name: 'generate-shell-header',
    apply: 'build',
    writeBundle() {
      const shellPath = resolve(__dirname, 'shell.html');
      const version   = process.env.SPA_VERSION ?? 'v0.0.1';
      const html      = readFileSync(shellPath, 'utf8')
                          .replace(/__SPA_VERSION__/g, version);

      const compressed = gzipSync(Buffer.from(html, 'utf8'), { level: 9 });
      const rawLen = html.length;
      const gzLen  = compressed.length;

      if (gzLen > 5760) {
        this.warn(`service_shell.h gzip size ${gzLen} B exceeds TCP_SND_BUF (~5760 B).`);
      }

      const hexLines: string[] = [];
      for (let i = 0; i < gzLen; i += 16) {
        const chunk = Array.from(compressed.subarray(i, i + 16))
          .map(b => `0x${b.toString(16).padStart(2, '0')}`)
          .join(', ');
        hexLines.push(`    ${chunk}`);
      }
      const header =
        `#pragma once\n` +
        `// AUTO-GENERATED from web/shell.html by web/vite.config.ts.\n` +
        `// DO NOT EDIT — run \`npm run build\` (or \`pio run -t buildfs\`) to regenerate.\n` +
        `// Shell HTML served at GET / in service mode on all boards.\n` +
        `// Version: ${version}  Raw: ${rawLen} B → gzip: ${gzLen} B\n` +
        `\n` +
        `static const uint8_t SERVICE_SHELL_HTML_GZ[] = {\n` +
        hexLines.join(',\n') + '\n};\n' +
        `static const size_t SERVICE_SHELL_HTML_GZ_LEN = ${gzLen};\n`;

      const outPath = resolve(__dirname, '../src/modules/wifi/service_shell.h');
      writeFileSync(outPath, header);
      // eslint-disable-next-line no-console
      console.log(`[shell-h] wrote ${outPath} (raw=${rawLen} B  gz=${gzLen} B)`);
    },
  };
}

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');
  const cydHost   = (env.VITE_DEFAULT_CYD_HOST ?? '').replace(/^https?:\/\//, '').replace(/\/+$/, '');
  const proxyBase = cydHost ? `http://${cydHost}` : 'http://192.168.1.100';
  const wsBase    = cydHost ? `ws://${cydHost}`   : 'ws://192.168.1.100';

  return {
    plugins: [vue(), inlineCssPlugin(), generateShellHeaderPlugin()],
    build: {
      // ESP32 has limited concurrent HTTP connections and mDNS can be unreliable.
      // All route pages are statically imported (router.ts) so Rollup produces
      // only vendor.js + index.js — no lazy chunks that need separate fetches.
      // CSS is inlined into index.html by inlineCssPlugin above.
      rollupOptions: {
        // Mark framework runtime as external — resolved at runtime via the
        // <script type="importmap"> in index.html which points at esm.sh.
        // Saves ~39 KB gzipped on the device's LittleFS. First browser load
        // fetches from esm.sh (cached by the browser HTTP cache AND our
        // service worker); offline reloads hit the SW cache and never touch
        // the network.
        external: ['vue', 'vue-router', 'pinia'],
        // No manualChunks: externals are not in the bundle, only our code
        // stays. Rollup emits a single chunk.
        // Fixed filename (no hash) so the WS bootstrap can hardcode the path.
        // paths: emit absolute esm.sh URLs instead of bare specifiers so that
        // app.js works when dynamically imported from CDN — importmaps are
        // not reliably applied to cross-origin dynamic imports in all browsers.
        output: {
          paths: {
            vue: 'https://esm.sh/vue@3',
            'vue-router': 'https://esm.sh/vue-router@4',
            pinia: 'https://esm.sh/pinia@2',
          },
          entryFileNames: 'assets/app.js',
          chunkFileNames: 'assets/[name].js',
          assetFileNames: 'assets/app.[ext]',
        },
      },
    },
    server: {
      port: 5173,
      proxy: {
        '/api': { target: proxyBase, changeOrigin: true },
        '/ws':  { target: wsBase,   ws: true, changeOrigin: true },
      },
    },
  };
});
