<template>
  <div class="page">
    <div class="page-header"><h2>Settings</h2></div>

    <!-- ── Tab bar ── -->
    <div class="tab-bar">
      <button
        v-for="t in TABS" :key="t.id" type="button"
        :class="['tab-btn', { active: activeTab === t.id }]"
        @click="activeTab = t.id"
      >{{ t.label }}</button>
    </div>

    <div class="settings-grid">

      <!-- ════════════════════════════════════════════════════════════════════ -->
      <!-- TAB: Device                                                        -->
      <!-- ════════════════════════════════════════════════════════════════════ -->
      <template v-if="activeTab === 'device'">

        <!-- ── Display ── -->
        <section class="settings-section">
          <div class="section-title">Display</div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Screen rotation</span>
              <span class="text-muted">Physical orientation of the panel</span>
            </div>
            <select v-model.number="form.screen_rotate" class="setting-select">
              <option :value="0">0° (default)</option>
              <option :value="1">90°</option>
              <option :value="2">180°</option>
              <option :value="3">270°</option>
            </select>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Device theme</span>
              <span class="text-muted">LVGL UI colour scheme</span>
            </div>
            <select v-model.number="form.theme" class="setting-select">
              <option :value="0">Light</option>
              <option :value="1">Dark</option>
              <option :value="2">Ocean</option>
              <option :value="3">Warm</option>
              <option :value="4">Real Black (OLED)</option>
            </select>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Backlight brightness</span>
              <span class="text-muted">{{ form.brightness }} %</span>
            </div>
            <div class="slider-wrap">
              <span class="text-muted" style="font-size:11px">0</span>
              <input type="range" min="5" max="100" step="5" v-model.number="form.brightness" class="slider" />
              <span class="text-muted" style="font-size:11px">100</span>
            </div>
          </div>
        </section>

        <!-- ── Behaviour ── -->
        <section class="settings-section">
          <div class="section-title">Behaviour</div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Startup tab</span>
              <span class="text-muted">Which tab is shown on device boot</span>
            </div>
            <select v-model.number="form.startup_tab" class="setting-select">
              <option :value="0">Macros</option>
              <option :value="1">Widgets</option>
            </select>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Tab bar position</span>
              <span class="text-muted">Where the tab navigation bar is placed</span>
            </div>
            <div class="tab-pos-picker">
              <button
                v-for="p in TAB_POSITIONS" :key="p.value" type="button"
                :class="['tab-pos-opt', { active: form.tab_pos === p.value }]"
                @click="form.tab_pos = p.value"
              >
                <span class="tab-pos-icon">{{ p.icon }}</span>
                <span>{{ p.label }}</span>
              </button>
            </div>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Status bar position</span>
              <span class="text-muted">Where the status bar sits on screen</span>
            </div>
            <div class="tab-pos-picker">
              <button
                v-for="p in STATUS_BAR_POSITIONS" :key="p.value" type="button"
                :class="['tab-pos-opt', { active: form.status_bar_pos === p.value }]"
                @click="form.status_bar_pos = p.value"
              >
                <span class="tab-pos-icon">{{ p.icon }}</span>
                <span>{{ p.label }}</span>
              </button>
            </div>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Auto-hide status bar</span>
              <span class="text-muted">Fade status bar when screen idle, restore on touch</span>
            </div>
            <label class="toggle">
              <input type="checkbox" v-model="form.status_bar_autohide" />
              <span class="toggle-track"></span>
            </label>
          </div>

          <div class="setting-row" v-if="form.status_bar_autohide">
            <div class="setting-label">
              <span>Auto-hide delay</span>
              <span class="text-muted">Seconds of inactivity before status bar fades</span>
            </div>
            <select v-model.number="form.status_bar_idle_s" class="setting-select">
              <option :value="3">3 s</option>
              <option :value="5">5 s</option>
              <option :value="10">10 s</option>
              <option :value="15">15 s</option>
              <option :value="30">30 s</option>
              <option :value="60">60 s</option>
            </select>
          </div>

          <div class="setting-row" v-if="form.status_bar_autohide">
            <div class="setting-label">
              <span>Faded opacity</span>
              <span class="text-muted">{{ Math.round((form.status_bar_faded_opa / 255) * 100) }} %</span>
            </div>
            <div class="slider-wrap">
              <span class="text-muted" style="font-size:11px">0</span>
              <input type="range" min="0" max="255" step="5" v-model.number="form.status_bar_faded_opa" class="slider" />
              <span class="text-muted" style="font-size:11px">255</span>
            </div>
          </div>
        </section>

        <!-- ── Power ── -->
        <section class="settings-section">
          <div class="section-title">Power</div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Dim screen after</span>
              <span class="text-muted">Reduce backlight when idle</span>
            </div>
            <select v-model.number="form.sleep_dim_timeout" class="setting-select">
              <option :value="0">Off</option>
              <option :value="1">1 min</option>
              <option :value="2">2 min</option>
              <option :value="5">5 min</option>
              <option :value="10">10 min</option>
              <option :value="15">15 min</option>
              <option :value="30">30 min</option>
            </select>
          </div>

          <div class="setting-row" v-if="form.sleep_dim_timeout > 0">
            <div class="setting-label">
              <span>Dim brightness</span>
              <span class="text-muted">{{ form.sleep_dim_level }} %</span>
            </div>
            <div class="slider-wrap">
              <span class="text-muted" style="font-size:11px">5</span>
              <input type="range" min="5" max="50" step="5" v-model.number="form.sleep_dim_level" class="slider" />
              <span class="text-muted" style="font-size:11px">50</span>
            </div>
          </div>

          <div class="setting-row" v-if="form.sleep_dim_timeout > 0">
            <div class="setting-label">
              <span>Turn off after</span>
              <span class="text-muted">Minutes after dimming until backlight off</span>
            </div>
            <select v-model.number="form.sleep_off_timeout" class="setting-select">
              <option :value="0">Off</option>
              <option :value="5">5 min</option>
              <option :value="15">15 min</option>
              <option :value="30">30 min</option>
              <option :value="60">60 min</option>
            </select>
          </div>
        </section>
      </template>

      <!-- ════════════════════════════════════════════════════════════════════ -->
      <!-- TAB: Layout & Styling                                              -->
      <!-- ════════════════════════════════════════════════════════════════════ -->
      <template v-if="activeTab === 'layout'">

        <!-- ── Layout ── -->
        <section class="settings-section">
          <div class="section-title">Layout</div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Macro columns</span>
              <span class="text-muted">Number of macro buttons per row</span>
            </div>
            <select v-model.number="form.macro_cols" class="setting-select">
              <option :value="2">2 columns</option>
              <option :value="3">3 columns</option>
              <option :value="4">4 columns</option>
              <option :value="5">5 columns</option>
              <option :value="6">6 columns</option>
            </select>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Widget grid layout</span>
              <span class="text-muted">How widgets are arranged on the Widgets tab</span>
            </div>
            <select v-model.number="form.widget_masonry" class="setting-select">
              <option :value="0">Standard (flex wrap)</option>
              <option :value="1">Column masonry</option>
              <option :value="2">Masonry layout</option>
            </select>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Widget columns</span>
              <span class="text-muted">Number of columns in widget grid (auto = based on screen width)</span>
            </div>
            <select v-model.number="form.widget_columns" class="setting-select">
              <option :value="0">Auto</option>
              <option :value="2">2 columns</option>
              <option :value="3">3 columns</option>
              <option :value="4">4 columns</option>
            </select>
          </div>
        </section>

        <!-- ── Default styling ── -->
        <section class="settings-section">
          <div class="section-title">Default styling</div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Macro background</span>
              <span class="text-muted">Default bg colour for new macros</span>
            </div>
            <div class="color-pick-wrap">
              <input type="color" v-model="defMacroBgHex" class="color-input" />
              <span class="color-hex">{{ defMacroBgHex }}</span>
            </div>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Macro icon colour</span>
              <span class="text-muted">Default icon colour (empty = auto from bg luminance)</span>
            </div>
            <div class="color-pick-wrap">
              <input type="color" v-model="defMacroIconClrHex" class="color-input" />
              <button type="button" class="btn btn-ghost btn-sm" @click="defMacroIconClrHex = '#000000'; form.def_macro_icon_clr = 0">Auto</button>
              <span class="color-hex">{{ form.def_macro_icon_clr ? defMacroIconClrHex : 'auto' }}</span>
            </div>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Macro icon size</span>
              <span class="text-muted">Default icon size for new macros</span>
            </div>
            <select v-model.number="form.def_macro_icon_sz" class="setting-select">
              <option :value="0">Small</option>
              <option :value="1">Medium</option>
              <option :value="2">Large</option>
            </select>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Macro image icon size</span>
              <span class="text-muted">Default size for custom image icons</span>
            </div>
            <select v-model.number="form.def_macro_image_sz" class="setting-select">
              <option :value="0">Small (50%)</option>
              <option :value="1">Medium (75%)</option>
              <option :value="2">Large (100%)</option>
            </select>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Widget background</span>
              <span class="text-muted">Default bg colour for new widgets</span>
            </div>
            <div class="color-pick-wrap">
              <input type="color" v-model="defWidgetBgHex" class="color-input" />
              <span class="color-hex">{{ defWidgetBgHex }}</span>
            </div>
          </div>
        </section>

        <!-- ── Visual appearance ── -->
        <section class="settings-section">
          <div class="section-title">Visual appearance</div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Button corner radius</span>
              <span class="text-muted">0 = square, 50 = pill</span>
            </div>
            <div class="slider-wrap">
              <input type="range" v-model.number="form.macro_radius" min="0" max="50" step="1" class="slider" />
              <span class="range-value">{{ form.macro_radius }}</span>
            </div>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Button transparency</span>
              <span class="text-muted">0 = fully transparent, 100 = opaque</span>
            </div>
            <div class="slider-wrap">
              <input type="range" v-model.number="macroBgOpaPercent" min="0" max="100" step="1" class="slider" />
              <span class="range-value">{{ macroBgOpaPercent }}%</span>
            </div>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Widget transparency</span>
              <span class="text-muted">0 = fully transparent, 100 = opaque</span>
            </div>
            <div class="slider-wrap">
              <input type="range" v-model.number="widgetBgOpaPercent" min="0" max="100" step="1" class="slider" />
              <span class="range-value">{{ widgetBgOpaPercent }}%</span>
            </div>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Button title</span>
              <span class="text-muted">Where to show the macro button label</span>
            </div>
            <select v-model.number="form.macro_title_pos" class="setting-select">
              <option :value="0">Inside button</option>
              <option :value="1">Below button</option>
              <option :value="2">Hidden</option>
            </select>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Button shadow</span>
              <span class="text-muted">
                Drop shadow under macro buttons
                <span v-if="form.macro_radius !== 0" style="color:var(--warning,#f59e0b)">
                  — requires corner radius = 0
                </span>
              </span>
            </div>
            <label class="toggle" :style="form.macro_radius !== 0 ? 'opacity:0.4;pointer-events:none' : ''">
              <input type="checkbox" v-model="form.macro_shadow" :disabled="form.macro_radius !== 0" />
              <span class="toggle-track"></span>
            </label>
          </div>

          <div class="setting-row">
            <div class="setting-label">
              <span>Button border color</span>
              <span class="text-muted">Leave black (#000000) to disable border</span>
            </div>
            <div style="display:flex;align-items:center;gap:8px">
              <input type="color" v-model="macroBorderClrHex" class="color-input" />
              <button type="button" class="btn btn-ghost btn-sm"
                      @click="form.macro_brd_clr = 0">None</button>
            </div>
          </div>
        </section>
      </template>

      <!-- ════════════════════════════════════════════════════════════════════ -->
      <!-- TAB: Backgrounds                                                   -->
      <!-- ════════════════════════════════════════════════════════════════════ -->
      <template v-if="activeTab === 'backgrounds'">
        <section class="settings-section">
          <div class="section-title">
            Background image library
          </div>

          <!-- File gallery with delete -->
          <div class="setting-row">
            <div>
              <p class="text-muted" style="font-size:12px;margin-bottom:10px">
                Manage background images stored on the SD card.
                Auto-converted to ≤ 320×240 JPEG (~96 KB max) on upload.
              </p>
            </div>

            <div v-if="bgFiles.length" class="icon-gallery" style="margin-bottom:8px">
              <div v-for="bg in bgFiles" :key="bg.name"
                  class="icon-thumb bg-thumb">
                <span class="bg-thumb-icon">🖼</span>
                <span class="icon-thumb-name" :title="bg.name">{{ bg.name }}</span>
                <span class="bg-thumb-size">{{ fmtSize(bg.size) }}</span>
                <button type="button" class="icon-thumb-del" @click.stop="onDeleteBg(bg.name)" title="Delete">✕</button>
              </div>
            </div>

            <!-- Upload -->
            <div class="upload-row" style="gap:8px;align-items:center">
              <label class="btn btn-ghost btn-sm" style="cursor:pointer">
                {{ bgUploading ? 'Uploading…' : '+ Upload image' }}
                <input type="file" accept="image/png, image/jpeg, image/webp" style="display:none"
                      :disabled="bgUploading" @change="onBgUpload" />
              </label>
              <select v-model="bgFitMode" class="select-sm"
                      :title="`Resize fit mode — output ${initDisplay.w}×${initDisplay.h}`">
                <option value="cover">Crop to fit</option>
                <option value="contain">Letterbox</option>
                <option value="stretch">Stretch</option>
              </select>
              <span v-if="bgUploadError" class="text-danger" style="font-size:12px">{{ bgUploadError }}</span>
            </div>
          </div>
        </section>

        <section class="settings-section">
          <div class="section-title">
            Icon library
          </div>

          <div class="setting-row">
            <div>
              <p class="text-muted" style="font-size:12px;margin-bottom:10px">
                Manage macro/widget icons stored on the SD card.
                Auto-converted to LVGL .bin (ARGB8888), up to 96×96.
              </p>
            </div>
            <div v-if="iconFiles.length" class="icon-gallery" style="margin-bottom:8px">
              <div v-for="ic in iconFiles" :key="ic.name" class="icon-thumb">
                <!-- Default: point directly at /icons/src/<name>; server
                     serves PNG/JPG/etc for the <base>.bin compat key.
                     Fall back to the custom preview (data: URL) only if
                     one was populated by refreshIconPreviews. -->
                <img :src="iconPreviews[ic.name] || `/icons/src/${ic.name}`" :alt="ic.name" />
                <span class="icon-thumb-name" :title="ic.name">{{ ic.name }}</span>
                <button type="button" class="icon-thumb-del"
                        @click.stop="onDeleteIcon(ic.name)" title="Delete">✕</button>
              </div>
            </div>

            <div class="upload-row">
              <label class="btn btn-ghost btn-sm" style="cursor:pointer">
                {{ iconUploading ? 'Uploading…' : '+ Upload image' }}
                <input type="file" accept="image/png, image/jpeg, image/webp"
                       style="display:none" :disabled="iconUploading"
                       @change="onIconUpload" />
              </label>
              <span v-if="iconUploadError" class="text-danger" style="font-size:12px">
                {{ iconUploadError }}
              </span>
            </div>
          </div>
        </section>
      </template>

      <!-- ════════════════════════════════════════════════════════════════════ -->
      <!-- TAB: System                                                        -->
      <!-- ════════════════════════════════════════════════════════════════════ -->
      <template v-if="activeTab === 'system'">

        <!-- ── Web interface ── -->
        <section class="settings-section">
          <div class="section-title">Web interface</div>
          <div class="setting-row">
            <div class="setting-label">
              <span>Dark mode</span>
              <span class="text-muted">Dashboard appearance (browser only)</span>
            </div>
            <label class="toggle">
              <input type="checkbox" v-model="webDark" @change="applyWebTheme" />
              <span class="toggle-track"></span>
            </label>
          </div>
        </section>

        <!-- ── Backup ── -->
        <section class="settings-section">
          <div class="section-title">Backup</div>
          <div class="setting-row">
            <div class="setting-label">
              <span>Export config</span>
              <span class="text-muted">Download macros, widgets and settings as JSON</span>
            </div>
            <button class="btn btn-ghost" :disabled="exporting" @click="onExport">
              {{ exporting ? 'Exporting...' : 'Export' }}
            </button>
          </div>
          <div class="setting-row">
            <div class="setting-label">
              <span>Import config</span>
              <span class="text-muted">Restore from a previously exported JSON file</span>
            </div>
            <label class="btn btn-ghost" style="cursor:pointer">
              Import
              <input type="file" accept=".json" style="display:none" @change="onImport" />
            </label>
          </div>
        </section>

        <!-- ── OTA ── -->
        <section v-if="form.ota_enabled" class="settings-section">
          <div class="section-title">Firmware update</div>
          <div class="setting-row">
            <div class="setting-label">
              <span>Upload firmware</span>
              <span class="text-muted">Device will restart after a successful update</span>
            </div>
            <label class="btn btn-ghost" style="cursor:pointer" :class="{ disabled: otaUploading }">
              {{ otaUploading ? `Uploading... ${otaProgress}%` : 'Upload .bin' }}
              <input type="file" accept=".bin" style="display:none" @change="onOtaUpload" :disabled="otaUploading" />
            </label>
          </div>
          <div v-if="otaUploading" class="ota-progress-wrap">
            <div class="ota-progress-bar" :style="{ width: otaProgress + '%' }"></div>
          </div>
        </section>
      </template>

      <!-- ════════════════════════════════════════════════════════════════════ -->
      <!-- Bottom bar                                                         -->
      <!-- ════════════════════════════════════════════════════════════════════ -->
      <p v-if="message" :class="isError ? 'text-danger' : 'text-ok'" style="margin-top:12px">{{ message }}</p>

      <div class="gap-8 mt-8">
        <button class="btn" :disabled="saving" @click="onSave">{{ saving ? 'Saving...' : 'Save' }}</button>
        <button class="btn btn-ghost" @click="load">Reset</button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, inject, watch, type Ref } from 'vue';
import { Api, loadBaseUrl, type Settings } from '../api/http';
import { awaitInit, resetInit, initSettings as initSettingsRef } from '../api/init';
import { Ws } from '../api/wsApi';
import { resizeBackgroundJpeg, type BgFitMode } from '../utils/image';
import { dedupeIconList, buildIconPreviewMap, uploadIconWithVariants } from '../utils/icons';

// Display dimensions injected from MainLayout (loaded via /api/init).
const initDisplay = inject<Ref<{ w: number; h: number }>>('initDisplay', ref({ w: 320, h: 240 }));

// Fit mode for background uploads — persisted in localStorage so user picks once.
const bgFitMode = ref<BgFitMode>(
  (localStorage.getItem('bgFitMode') as BgFitMode) || 'cover'
);
watch(bgFitMode, (v) => localStorage.setItem('bgFitMode', v));

// ── helpers ──────────────────────────────────────────────────────────────
function numberToHex(n: number): string {
  if (!n) return '#000000';
  return '#' + (n & 0xffffff).toString(16).padStart(6, '0');
}
function hexToNumber(hex: string): number {
  return parseInt(hex.replace('#', ''), 16) || 0;
}

// ── tabs ─────────────────────────────────────────────────────────────────
const TABS = [
  { id: 'device',      label: 'Device' },
  { id: 'layout',      label: 'Layout & Styling' },
  { id: 'backgrounds', label: 'Backgrounds' },
  { id: 'system',      label: 'System' },
];
const activeTab = ref('device');

// ── Background management ──────────────────────────────────────────────────
type BgFile = { name: string; size: number }
const bgFiles         = ref<BgFile[]>([]);
const bgUploading     = ref(false);
const bgUploadError   = ref('');

function fmtSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

async function loadBackgrounds() {
  try {
    bgFiles.value = await Api.getBackgrounds();
  } catch { bgFiles.value = []; }
}

async function onDeleteBg(name: string) {
  if (!confirm(`Delete background "${name}"?`)) return;
  await Ws.deleteBackground(name);
  await loadBackgrounds();
}

async function onBgUpload(e: Event) {
  const raw = (e.target as HTMLInputElement).files?.[0];
  if (!raw) return;
  bgUploading.value  = true;
  bgUploadError.value = '';
  try {
    const file = await resizeBackgroundJpeg(raw, {
      targetW: initDisplay.value.w,
      targetH: initDisplay.value.h,
      fit: bgFitMode.value,
    });
    await Api.uploadBackground(file);
    await loadBackgrounds();
  } catch (err: any) {
    bgUploadError.value = err.message ?? 'Upload failed';
  } finally {
    bgUploading.value = false;
    (e.target as HTMLInputElement).value = '';
  }
}

// ── Icon library (global management, mirrors background library above) ────
const iconFiles       = ref<{ name: string; size: number }[]>([]);
const iconPreviews    = ref<Record<string, string>>({});
const iconUploading   = ref(false);
const iconUploadError = ref('');
// Maps display name ("play.bin") → actual preview filename ("play_m.bin" or "play.bin")
const _iconPreviewMap = ref<Record<string, string>>({});

async function loadIcons() {
  try {
    const names = await Api.getIcons();
    const { items, previewMap } = dedupeIconList(names);
    iconFiles.value = items.map(n => ({ name: n, size: 0 }));
    _iconPreviewMap.value = previewMap;
    try { iconPreviews.value = await buildIconPreviewMap(items, previewMap); }
    catch { /* previews degrade, list survives */ }
  } catch {
    iconFiles.value = [];
    iconPreviews.value = {};
    _iconPreviewMap.value = {};
  }
}

async function onIconUpload(e: Event) {
  const raw = (e.target as HTMLInputElement).files?.[0];
  if (!raw) return;
  iconUploading.value = true;
  iconUploadError.value = '';
  try {
    const btnSize = initSettingsRef.value?.btn_size ?? 70;
    await uploadIconWithVariants(raw, btnSize);
    await loadIcons();
  } catch (err: any) {
    iconUploadError.value = err.message ?? 'Upload failed';
  } finally {
    iconUploading.value = false;
    (e.target as HTMLInputElement).value = '';
  }
}

async function onDeleteIcon(name: string) {
  if (!confirm(`Delete icon "${name}"?`)) return;
  // Pass the base name — backend deletes all variants (_s/_m/_l) + source.
  await Ws.deleteIcon(name);
  await loadIcons();
}

// ── web dark mode ─────────────────────────────────────────────────────────
const webDark = ref(localStorage.getItem('cyd_theme') === 'dark');

function applyWebTheme() {
  const val = webDark.value ? 'dark' : 'light';
  localStorage.setItem('cyd_theme', val);
  document.documentElement.setAttribute('data-theme', webDark.value ? 'dark' : '');
}

// ── tab position options ──────────────────────────────────────────────────
const TAB_POSITIONS = [
  { value: 0, label: 'Top',    icon: '\u2580' },
  { value: 1, label: 'Bottom', icon: '\u2584' },
  { value: 2, label: 'Left',   icon: '\u258C' },
  { value: 3, label: 'Right',  icon: '\u2590' },
];

// Status bar supports all 4 edges (tabs only 3 because lv_tabview has no
// LV_DIR_BOTTOM). Icons are Unicode block-quadrants matching the edge.
const STATUS_BAR_POSITIONS = [
  { value: 0, label: 'Top',    icon: '\u2580' },
  { value: 1, label: 'Bottom', icon: '\u2584' },
  { value: 2, label: 'Left',   icon: '\u258C' },
  { value: 3, label: 'Right',  icon: '\u2590' },
];

// ── device form ───────────────────────────────────────────────────────────
const form = ref({
  screen_rotate:      0,
  theme:              0,
  brightness:         100,
  btn_size:           70,
  macro_cols:         4,
  startup_tab:        0,
  tab_pos:            0,
  status_bar_pos:     1,
  status_bar_autohide:false,
  status_bar_idle_s:  5,
  status_bar_faded_opa: 40,
  sleep_dim_timeout:  5,
  sleep_dim_level:    20,
  sleep_off_timeout:  0,
  widget_masonry:     0,
  widget_columns:     0,
  ota_enabled:        false,
  def_macro_bg:       0x1e293b,
  def_macro_icon_clr: 0,
  def_macro_icon_sz:  0,
  def_macro_image_sz: 1,
  def_widget_bg:      0x1e293b,
  macro_radius:       5,
  macro_bg_opa:       255,
  widget_bg_opa:      255,
  macro_title_pos:    0,
  macro_shadow:       true,
  macro_brd_clr:   0,
});

// Color hex proxies for v-model on color inputs
const defMacroBgHex = computed({
  get: () => numberToHex(form.value.def_macro_bg),
  set: (v: string) => { form.value.def_macro_bg = hexToNumber(v); },
});
const defMacroIconClrHex = computed({
  get: () => numberToHex(form.value.def_macro_icon_clr),
  set: (v: string) => { form.value.def_macro_icon_clr = hexToNumber(v); },
});
const defWidgetBgHex = computed({
  get: () => numberToHex(form.value.def_widget_bg),
  set: (v: string) => { form.value.def_widget_bg = hexToNumber(v); },
});
const macroBorderClrHex = computed({
  get: () => form.value.macro_brd_clr === 0 ? '#000000' : numberToHex(form.value.macro_brd_clr),
  set: (v: string) => { form.value.macro_brd_clr = hexToNumber(v); },
});

// Opacity sliders use 0-100% but device stores 0-255
const macroBgOpaPercent = computed({
  get: () => Math.round((form.value.macro_bg_opa / 255) * 100),
  set: (v: number) => { form.value.macro_bg_opa = Math.round((v / 100) * 255); },
});
const widgetBgOpaPercent = computed({
  get: () => Math.round((form.value.widget_bg_opa / 255) * 100),
  set: (v: number) => { form.value.widget_bg_opa = Math.round((v / 100) * 255); },
});

const saving       = ref(false);
const exporting    = ref(false);
const otaUploading = ref(false);
const otaProgress  = ref(0);
const message   = ref('');
const isError   = ref(false);

function applySettings(s: Settings) {
  form.value.screen_rotate     = s.screen_rotate ?? 0;
  form.value.theme             = s.theme         ?? 0;
  form.value.brightness        = s.brightness    ?? 100;
  form.value.btn_size          = s.btn_size       ?? 70;
  form.value.macro_cols        = (s as any).macro_cols       ?? 4;
  form.value.startup_tab       = s.startup_tab        ?? 0;
  form.value.tab_pos           = s.tab_pos            ?? 0;
  form.value.status_bar_pos      = (s as any).status_bar_pos      ?? 1;
  form.value.status_bar_autohide = (s as any).status_bar_autohide ?? false;
  form.value.status_bar_idle_s   = (s as any).status_bar_idle_s   ?? 5;
  form.value.status_bar_faded_opa= (s as any).status_bar_faded_opa?? 40;
  form.value.sleep_dim_timeout = (s as any).sleep_dim_timeout ?? 5;
  form.value.sleep_dim_level   = (s as any).sleep_dim_level   ?? 20;
  form.value.sleep_off_timeout = (s as any).sleep_off_timeout ?? 0;
  form.value.widget_masonry    = (s as any).widget_masonry    ?? 0;
  form.value.widget_columns    = (s as any).widget_columns    ?? 0;
  form.value.ota_enabled       = (s as any).ota_enabled       ?? false;
  form.value.def_macro_bg      = s.def_macro_bg       ?? 0x1e293b;
  form.value.def_macro_icon_clr = s.def_macro_icon_clr ?? 0;
  form.value.def_macro_icon_sz = s.def_macro_icon_sz  ?? 0;
  form.value.def_macro_image_sz = s.def_macro_image_sz ?? 1;
  form.value.def_widget_bg     = s.def_widget_bg      ?? 0x1e293b;
  form.value.macro_radius      = s.macro_radius       ?? 5;
  form.value.macro_bg_opa      = s.macro_bg_opa       ?? 255;
  form.value.widget_bg_opa     = s.widget_bg_opa      ?? 255;
  form.value.macro_title_pos   = s.macro_title_pos    ?? 0;
  form.value.macro_shadow      = s.macro_shadow       ?? true;
  form.value.macro_brd_clr  = s.macro_brd_clr   ?? 0;
  message.value = '';
}

async function load() {
  try {
    await awaitInit();
    if (initSettingsRef.value) applySettings(initSettingsRef.value);
  } catch (e: any) {
    message.value = 'Load failed: ' + (e.message ?? '');
    isError.value = true;
  }
}

async function onSave() {
  saving.value = true;
  message.value = '';
  try {
    await Ws.saveSettings({ ...form.value });

    message.value = 'Saved';
    isError.value = false;
  } catch (e: any) {
    message.value = 'Save failed: ' + (e.message ?? '');
    isError.value = true;
  } finally {
    saving.value = false;
  }
}

async function onExport() {
  exporting.value = true;
  try {
    const data = await Api.exportConfig();
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const a = Object.assign(document.createElement('a'), {
      href: URL.createObjectURL(blob),
      download: 'cyd-dashboard-backup.json',
    });
    a.click();
    URL.revokeObjectURL(a.href);
  } catch (e: any) {
    message.value = 'Export failed: ' + (e.message ?? '');
    isError.value = true;
  } finally {
    exporting.value = false;
  }
}

async function onImport(e: Event) {
  const file = (e.target as HTMLInputElement).files?.[0];
  if (!file) return;
  message.value = '';
  try {
    const text = await file.text();
    await Ws.importConfig(JSON.parse(text));
    // Invalidate cached /api/init so load() refetches fresh device state.
    resetInit();
    await load();
    message.value = 'Config imported successfully';
    isError.value = false;
  } catch (e: any) {
    message.value = 'Import failed: ' + (e.message ?? '');
    isError.value = true;
  }
  (e.target as HTMLInputElement).value = '';
}

async function onOtaUpload(e: Event) {
  const file = (e.target as HTMLInputElement).files?.[0];
  if (!file) return;
  if (!confirm(`Upload firmware "${file.name}" (${(file.size / 1024).toFixed(0)} KB)? The device will restart after update.`)) {
    (e.target as HTMLInputElement).value = '';
    return;
  }
  otaUploading.value = true;
  otaProgress.value  = 0;
  message.value = '';
  try {
    await new Promise<void>((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', loadBaseUrl() + '/api/ota');
      xhr.upload.onprogress = (ev) => {
        if (ev.lengthComputable) otaProgress.value = Math.round(ev.loaded / ev.total * 100);
      };
      xhr.onload = () => {
        if (xhr.status === 200) resolve();
        else reject(new Error(`HTTP ${xhr.status}`));
      };
      xhr.onerror = () => reject(new Error('Network error'));
      xhr.send(file);
    });
    message.value = 'Firmware uploaded successfully. Device is restarting...';
    isError.value = false;
  } catch (err: any) {
    message.value = 'OTA failed: ' + (err.message ?? '');
    isError.value = true;
  } finally {
    otaUploading.value = false;
    (e.target as HTMLInputElement).value = '';
  }
}

onMounted(async () => {
  await load();
  loadBackgrounds();
  loadIcons();
});

// initSettingsRef updates whenever state_changed triggers a background refresh
// (ws.ts listener → resetInit → runInit). Re-apply into form so the UI
// reflects fresh device state without a manual reload.
watch(initSettingsRef, (s) => { if (s) applySettings(s); });
</script>

<style scoped>
.tab-bar {
  display: flex;
  gap: 0;
  margin: 0 auto 16px;
  border: 1px solid var(--border);
  border-radius: var(--radius, 6px);
  overflow: hidden;
  max-width: 600px;
}
.tab-btn {
  flex: 1;
  padding: 8px 16px;
  border: none;
  background: var(--surface2);
  color: var(--muted);
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
}
.tab-btn + .tab-btn { border-left: 1px solid var(--border); }
.tab-btn.active {
  background: var(--accent);
  color: #fff;
}

.settings-grid {
  display: flex;
  flex-direction: column;
  gap: 20px;
  max-width: 600px;
  margin: 0 auto;
}

.settings-section {
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: var(--radius, 6px);
  overflow: hidden;
}

.section-title {
  padding: 10px 16px;
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: .06em;
  color: var(--muted);
  background: var(--surface2);
  border-bottom: 1px solid var(--border);
}

.setting-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 12px 16px;
  border-bottom: 1px solid var(--border);
}
.setting-row:last-child { border-bottom: none; }

.setting-label {
  display: flex;
  flex-direction: column;
  gap: 2px;
  font-size: 14px;
  color: var(--text);
}
.setting-label .text-muted { font-size: 11px; }

.setting-select {
  padding: 5px 8px;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--surface);
  color: var(--text);
  font-size: 13px;
  min-width: 130px;
}

/* ── toggle switch ── */
.toggle { position: relative; display: inline-block; width: 44px; height: 24px; flex-shrink: 0; }
.toggle input { opacity: 0; width: 0; height: 0; }
.toggle-track {
  position: absolute; inset: 0;
  background: var(--border);
  border-radius: 24px;
  cursor: pointer;
  transition: background 0.2s;
}
.toggle-track::after {
  content: '';
  position: absolute;
  left: 3px; top: 3px;
  width: 18px; height: 18px;
  border-radius: 50%;
  background: #fff;
  transition: transform 0.2s;
}
.toggle input:checked + .toggle-track { background: var(--accent); }
.toggle input:checked + .toggle-track::after { transform: translateX(20px); }

/* ── brightness slider ── */
.slider-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 180px;
}
.slider { flex: 1; }
.range-value { min-width: 36px; text-align: right; font-size: 13px; color: var(--text-muted); }

/* ── tab position picker ── */
.tab-pos-picker { display: flex; gap: 6px; }
.tab-pos-opt {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 3px;
  padding: 6px 12px;
  border: 1px solid var(--border);
  border-radius: 4px;
  cursor: pointer;
  font-size: 11px;
  background: var(--surface2);
  color: var(--text);
  transition: all 0.15s;
  min-width: 52px;
}
.tab-pos-opt .tab-pos-icon { font-size: 16px; line-height: 1; }
.tab-pos-opt.active {
  background: var(--accent);
  color: #fff;
  border-color: var(--accent);
}

/* ── colour picker ── */
.color-pick-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
}
.color-input {
  width: 36px;
  height: 28px;
  padding: 0;
  border: 1px solid var(--border);
  border-radius: 4px;
  cursor: pointer;
  background: none;
}
.color-hex {
  font-size: 12px;
  font-family: monospace;
  color: var(--muted);
}

.text-ok { color: #16a34a; }

/* ── OTA progress ── */
.ota-progress-wrap {
  height: 4px;
  background: var(--surface2);
  border-top: 1px solid var(--border);
}
.ota-progress-bar {
  height: 100%;
  background: var(--accent);
  transition: width 0.2s;
}
.disabled { opacity: 0.5; pointer-events: none; }

/* ── Background gallery ── */
.icon-gallery {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 8px;
}
.icon-thumb {
  position: relative;
  width: 64px;
  border: 2px solid var(--border);
  border-radius: 6px;
  padding: 4px;
  cursor: pointer;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 3px;
  transition: border-color .15s;
}
.icon-thumb img {
  width: 48px;
  height: 48px;
  object-fit: contain;
  display: block;
}
.bg-thumb { width: 96px; height: 72px; padding: 4px; justify-content: center; }
.bg-thumb-icon { font-size: 28px; line-height: 1; margin-top: 4px; }
.bg-thumb-size { font-size: 8px; color: var(--text-muted, #888); margin-bottom: 14px; }
.bg-thumb-preview {
  max-width: 100%;
  max-height: 150px;
  border: 1px solid var(--border);
  border-radius: 6px;
}
.icon-thumb:hover { border-color: var(--accent, #3d7ed4); }
.icon-thumb.selected { border-color: var(--accent, #3d7ed4); background: #eef4ff; }
.icon-thumb-name {
  font-size: 9px; color: var(--muted); word-break: break-all;
  text-align: center; line-height: 1.2; max-width: 100%;
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  background: rgba(0,0,0,.45); color: #fff; width: 100%;
  padding: 1px 2px; position: absolute; bottom: 0;
}
.icon-thumb-del {
  position: absolute; top: 2px; right: 2px;
  background: var(--danger, #dc2626); color: #fff;
  border: none; border-radius: 3px;
  width: 14px; height: 14px; font-size: 9px;
  cursor: pointer; display: none;
  align-items: center; justify-content: center;
  padding: 0; line-height: 1;
}
.icon-thumb:hover .icon-thumb-del { display: flex; }
.upload-row { display: flex; align-items: center; gap: 10px; }
</style>
