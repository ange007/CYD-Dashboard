<template>
  <div class="page">
    <div class="page-header">
      <h2>{{ isNew
        ? (form.type === 'scene' ? 'New Scene' : 'New Widget')
        : (form.type === 'scene' ? 'Edit Scene' : 'Edit Widget') }}</h2>
    </div>

    <form class="form" @submit.prevent="onSave">

      <!-- ── Scene mode banner ── -->
      <template v-if="form.type === 'scene'">
        <div class="scene-banner">
          <span class="scene-banner-icon">{{ form.target_id ? '🔗' : '📁' }}</span>
          <div v-if="!form.target_id">
            <strong>Scene (folder)</strong><br>
            <span class="text-muted" style="font-size:12px">
              Creates a folder that groups widgets. On the device this card opens the folder.
              Assign widgets to it via the <strong>Scene</strong> field in each widget's editor.
            </span>
          </div>
          <div v-else>
            <strong>Scene navigator</strong><br>
            <span class="text-muted" style="font-size:12px">
              A card that opens an existing scene. Can be placed inside any scene,
              allowing the same target scene to be reached from multiple places.
            </span>
          </div>
        </div>
      </template>

      <!-- ── Basic ─────────────────────────────────────────────────────────── -->
      <div class="field">
        <label>{{ form.type === 'scene' ? 'Scene name' : 'Title' }}
          <span class="text-muted">(shown as widget header)</span></label>
        <input v-model="form.title" required
          :placeholder="form.type === 'scene' ? 'e.g. Home Assistant, Media, Work' : 'e.g. CPU Usage'" />
      </div>

      <div class="field-row" style="gap:12px;flex-wrap:wrap">
        <div class="field" style="flex:2;min-width:140px">
          <label>Icon</label>
          <select v-model="form.icon">
            <option value="">(none — show title text)</option>
            <option v-for="ic in ICONS" :key="ic" :value="ic">{{ ic }}</option>
          </select>
        </div>
        <div v-if="form.icon && form.type !== 'scene'" class="field" style="flex:1;min-width:120px">
          <label>Icon position</label>
          <select v-model="form.icon_pos">
            <option value="top">Top</option>
            <option value="bottom">Bottom</option>
            <option value="left">Left</option>
            <option value="right">Right</option>
          </select>
        </div>
      </div>

      <!-- Widget type — hidden when editing a scene (type is locked) -->
      <div v-if="form.type !== 'scene'" class="field">
        <label>Widget type</label>
        <select v-model="form.type">
          <option value="text">Text</option>
          <option value="progress">Progress bar / Arc</option>
          <option value="chart">Chart</option>
          <option value="counter">Counter (tap to increment)</option>
          <option value="timer">Timer (tap to start/stop)</option>
          <option value="image">Image (static PNG from SD card)</option>
          <option value="scene">Scene navigator (folder)</option>
        </select>
      </div>

      <!-- Scene assignment — hidden for bare scene definitions (folders live at root);
           shown for regular widgets AND for scene navigators that have a target -->
      <div v-if="form.type !== 'scene' || form.target_id" class="field">
        <label>Scene <span class="text-muted">(assign to a scene / folder)</span></label>
        <select v-model="form.scene_id">
          <option value="">Root (no scene)</option>
          <option v-for="s in sceneItems" :key="s.id" :value="s.id">{{ s.title }}</option>
        </select>
      </div>

      <div v-if="form.type === 'progress'" class="field">
        <label>Progress style</label>
        <select v-model.number="form.style">
          <option :value="0">Horizontal bar</option>
          <option :value="1">Vertical bar</option>
          <option :value="2">Arc</option>
        </select>
      </div>

      <div v-if="form.type !== 'scene'" class="field">
        <label>Auto-refresh interval <span class="text-muted">(seconds, 0 = on-tap only)</span></label>
        <input v-model.number="form.update" type="number" min="0" placeholder="0" />
      </div>

      <!-- ── Appearance ──────────────────────────────────────────────────────── -->
      <div class="section-divider">Appearance</div>

      <div class="field-row">
        <div class="field" style="flex:1">
          <label>Background color <span class="text-muted">(leave default for theme color)</span></label>
          <div style="display:flex;gap:8px;align-items:center;margin-top:4px">
            <input type="color" v-model="form.bg_color_hex" class="color-swatch" />
            <button type="button" class="btn btn-ghost btn-sm" @click="form.bg_color_hex = ''">Reset</button>
            <span class="text-muted" style="font-size:11px">{{ form.bg_color_hex || 'default' }}</span>
          </div>
        </div>
        <div v-if="form.bg_color_hex" class="field" style="width:160px">
          <label>Fill style</label>
          <div class="radio-group" style="margin-top:4px">
            <label class="radio-opt" :class="{ active: form.bg_style === 'solid' }">
              <input type="radio" v-model="form.bg_style" value="solid" />Solid
            </label>
            <label class="radio-opt" :class="{ active: form.bg_style === 'gradient' }">
              <input type="radio" v-model="form.bg_style" value="gradient" />Gradient
            </label>
          </div>
        </div>
      </div>

      <!-- preview swatch -->
      <div v-if="form.bg_color_hex" class="color-preview" :style="previewStyle"></div>

      <!-- Per-widget transparency override -->
      <div v-if="form.type !== 'scene'" class="field">
        <label class="checkbox-label">
          <input type="checkbox" v-model="useCustomBgOpa" />
          Custom transparency <span class="text-muted">(overrides global setting for this widget)</span>
        </label>
        <div v-if="useCustomBgOpa" class="slider-row">
          <input type="range" min="0" max="100" v-model.number="bgOpaPercent" class="slider" />
          <span class="range-value">{{ bgOpaPercent }}%</span>
        </div>
      </div>

      <!-- ── Size ────────────────────────────────────────────────────────────── -->
      <div class="section-divider">Display size</div>

      <div class="field">
        <label>Width <span class="text-muted">(span ≥ column count → full width)</span></label>
        <div class="radio-group">
          <label class="radio-opt" :class="{ active: form.span === 1 }">
            <input type="radio" v-model.number="form.span" :value="1" />
            1 column
          </label>
          <label class="radio-opt" :class="{ active: form.span === 2 }">
            <input type="radio" v-model.number="form.span" :value="2" />
            2 columns
          </label>
          <label class="radio-opt" :class="{ active: form.span === 3 }">
            <input type="radio" v-model.number="form.span" :value="3" />
            3 columns
          </label>
          <label class="radio-opt" :class="{ active: form.span === 0 }">
            <input type="radio" v-model.number="form.span" :value="0" />
            Full width
          </label>
        </div>
      </div>

      <div class="field">
        <label>Height <span class="text-muted">(px, 0 = default)</span></label>
        <input v-model.number="form.height" type="number" min="0" placeholder="0" />
      </div>

      <!-- ── Counter config ─────────────────────────────────────────────────── -->
      <template v-if="form.type === 'counter'">
        <div class="section-divider">Counter settings</div>
        <div class="field-row">
          <div class="field" style="flex:1">
            <label>Step <span class="text-muted">(increment per tap)</span></label>
            <input v-model.number="form.step" type="number" min="1" placeholder="1" />
          </div>
          <div class="field" style="flex:1">
            <label>Min <span class="text-muted">(long-press to reset)</span></label>
            <input v-model.number="form.min" type="number" placeholder="0" />
          </div>
          <div class="field" style="flex:1">
            <label>Max</label>
            <input v-model.number="form.max" type="number" placeholder="99" />
          </div>
        </div>
        <div class="hint-box">Long press on device to reset counter to Min value.</div>
      </template>

      <!-- ── Timer config ────────────────────────────────────────────────────── -->
      <template v-if="form.type === 'timer'">
        <div class="section-divider">Timer settings</div>
        <div class="field-row">
          <div class="field" style="flex:1">
            <label>Duration (seconds)</label>
            <input v-model.number="form.duration" type="number" min="1" placeholder="300" />
          </div>
          <div class="field" style="flex:2">
            <label>Done URL <span class="text-muted">(optional, called when timer reaches zero)</span></label>
            <input v-model="form.done_url" placeholder="http://192.168.1.x/notify" />
          </div>
        </div>
        <div class="hint-box">Tap to start/stop. Timer resets on device reboot.</div>
      </template>

      <!-- ── Image widget config ───────────────────────────────────────────────── -->
      <template v-if="form.type === 'image'">
        <div class="section-divider">Image source</div>
        <div class="field">
          <label>Image filename <span class="text-muted">(from uploaded icons on SD card)</span></label>
          <input v-model="form.image_src" placeholder="e.g. my_image.bin" />
          <div class="hint-box" style="margin-top:6px">
            Upload images via Settings → Icon library or the Macro Editor, then enter the
            filename here (LVGL .bin format, auto-converted on upload). Requires SD card.
          </div>
        </div>
      </template>

      <template v-if="!['scene','counter','timer','image'].includes(form.type)">
        <!-- ── Data source (URL-fetching types only) ─────────────────────────── -->
        <div class="section-divider">Data source</div>

        <div class="field">
          <label>Fetch method</label>
          <select v-model="form.data_target">
            <option value="url">Direct URL (ESP32 fetches)</option>
            <option value="url_system">Proxied URL (browser fetches → sends to device)</option>
            <option value="system">System metric (companion)</option>
          </select>
        </div>

        <!-- Companion metric picker -->
        <template v-if="form.data_target === 'system'">
          <div class="field">
            <label>Metric ID
              <button type="button" class="btn btn-ghost btn-sm" style="margin-left:8px"
                :disabled="companionLoading" @click="loadCompanionMetrics">
                {{ companionLoading ? 'Loading…' : 'Load from companion' }}
              </button>
            </label>
            <select v-if="companionMetrics.length" v-model="form.metric_id" style="margin-bottom:6px">
              <option value="">— select metric —</option>
              <optgroup v-for="cat in [...new Set(companionMetrics.map(m => m.category))]" :key="cat" :label="cat">
                <option v-for="m in companionMetrics.filter(x => x.category === cat)" :key="m.id" :value="m.id">
                  {{ m.label }} ({{ m.value }}{{ m.unit }})
                </option>
              </optgroup>
            </select>
            <input v-model="form.metric_id" placeholder="e.g. cpu.usage" />
            <span v-if="companionError" class="text-muted" style="color:#f87171;font-size:12px">{{ companionError }}</span>
          </div>
        </template>

        <div v-else class="field">
          <label>URL</label>
          <div style="display:flex;gap:6px;align-items:center">
            <input v-model="form.url" placeholder="http://192.168.1.x/api/data" style="flex:1" />
            <button type="button" class="btn btn-ghost btn-sm" :disabled="urlTest.loading || !form.url.trim()" @click="testUrl">
              {{ urlTest.loading ? '…' : 'Test' }}
            </button>
          </div>
          <!-- Test result panel -->
          <div v-if="urlTest.done" class="hint-box" style="margin-top:6px">
            <div v-if="urlTest.error" style="color:#f87171">
              ✕ Fetch error: {{ urlTest.error }}
              <span v-if="urlTest.corsHint" style="display:block;margin-top:4px;font-size:11px;color:#fbbf24">
                ⚠ CORS: this URL may not be reachable from service-mode browser. Use companion proxy or configure CORS on the server.
              </span>
            </div>
            <template v-else>
              <div style="display:flex;gap:12px;flex-wrap:wrap;font-size:12px;color:#a0aec0;margin-bottom:4px">
                <span>HTTP {{ urlTest.status }}</span>
                <span>{{ urlTest.rawSize }} B raw</span>
                <span v-if="urlTest.parsed !== null" style="color:#4ade80">✓ parse OK → {{ urlTest.parsed.length }} B</span>
                <span v-else-if="form.parse_type && (form.regex || form.json_keys.length)" style="color:#f87171">✕ parse failed</span>
              </div>
              <div v-if="urlTest.warnings.length" style="color:#fbbf24;font-size:12px;margin-bottom:4px">
                <div v-for="w in urlTest.warnings" :key="w">⚠ {{ w }}</div>
              </div>
              <div v-if="urlTest.parsed !== null" style="font-size:12px">
                <strong>Result:</strong> <code style="word-break:break-all">{{ urlTest.parsed }}</code>
              </div>
              <div v-else-if="urlTest.rawPreview" style="font-size:12px">
                <strong>Raw preview:</strong>
                <code style="display:block;white-space:pre-wrap;max-height:80px;overflow:auto;font-size:11px;word-break:break-all">{{ urlTest.rawPreview }}</code>
              </div>
            </template>
          </div>
        </div>

        <!-- ── Request headers ─────────────────────────────────────────────── -->
        <div v-if="form.data_target !== 'system'" class="field">
          <label>
            Request headers 
            <span class="text-muted">(optional, e.g. Authorization: Bearer …)</span>
          </label>
          <div class="keys-list">
            <div v-for="(h, i) in form.headers" :key="h.id" class="key-row">
              <input
                v-model="h.key"
                placeholder="Header name"
                style="flex:1;min-width:0"
              />
              <input
                v-model="h.value"
                placeholder="Value"
                style="flex:2;min-width:0"
              />
              <button type="button" class="btn btn-ghost btn-sm" @click="removeHeader(i)">✕</button>
            </div>
            <button type="button" class="btn btn-ghost btn-sm" @click="addHeader">+ Add header</button>
          </div>
        </div>

        <!-- ── Parse mode toggle ─────────────────────────────────────────────── -->
        <!-- ── Data parsing (hidden for system metrics) ─────────────────────── -->
        <template v-if="form.data_target !== 'system'">
          <div class="section-divider">Data parsing</div>

          <div class="field">
            <label>Parse type</label>
            <div class="radio-group">
              <label class="radio-opt" :class="{ active: form.parse_type === 'regex' }">
                <input type="radio" v-model="form.parse_type" value="regex" />
                Regex
              </label>
              <label class="radio-opt" :class="{ active: form.parse_type === 'json' }">
                <input type="radio" v-model="form.parse_type" value="json" />
                JSON keys
              </label>
            </div>
          </div>

          <!-- ── Regex mode ─────────────────────────────────────────────────── -->
          <template v-if="form.parse_type === 'regex'">
            <div class="field">
              <label>Regex <span class="text-muted">(extract value from response)</span></label>
              <input v-model="form.regex" placeholder='"cpu":\s*([\d.]+)' style="font-family:monospace" />
              <span class="text-muted hint">Capture group 1 → <code>{1}</code>, group 2 → <code>{2}</code>, etc.</span>
              <span class="text-muted hint" style="color:#c98a00">
                ⚠ On-device regex runs only on boards with PSRAM. On low-memory boards
                (plain ESP32) regex is skipped and <code>{1}</code> receives the raw response.
                For full regex support on such boards, fetch via companion/web proxy
                and deliver the already-parsed value.
              </span>
            </div>
            <div class="field">
              <label>Template</label>
              <input v-model="form.template" placeholder="e.g. {1}% or CPU: {1}%, RAM: {2}%" />
            </div>
          </template>

          <!-- ── JSON mode ──────────────────────────────────────────────────── -->
          <template v-if="form.parse_type === 'json'">
            <div class="field">
              <label>
                JSON keys
                <span class="text-muted">(dot-notation supported: <code>memory.used</code>)</span>
              </label>
              <div class="keys-list">
                <div v-for="(key, i) in form.json_keys" :key="key.id" class="key-row">
                  <input
                    v-model="key.value"
                    :placeholder="`key ${i + 1}, e.g. cpu or memory.used`"
                    style="font-family:monospace; flex:1"
                  />
                  <button type="button" class="btn btn-ghost btn-sm" @click="removeKey(i)">✕</button>
                </div>
                <button type="button" class="btn btn-ghost btn-sm" @click="addKey">+ Add key</button>
              </div>
              <div v-if="form.json_keys.length" class="hint-box">
                Available placeholders:
                <code v-for="k in form.json_keys.filter(k => k.value)" :key="k.id">{{'{ ' + k.value + ' }'}}</code>
              </div>
            </div>
            <div class="field">
              <label>
                Template
                <span class="text-muted">(use <code>\n</code> for new line)</span>
              </label>
              <textarea
                v-model="form.template"
                rows="3"
                :placeholder="jsonTemplatePlaceholder"
                style="font-family:monospace; resize:vertical"
              />
            </div>
          </template>
        </template>
      </template>

      <!-- ── Preview ──────────────────────────────────────────────────────── -->
      <div v-if="form.template" class="hint-box">
        <strong>Template preview:</strong><br />
        <code style="white-space:pre-wrap">{{ form.template }}</code>
      </div>

      <p v-if="error" class="text-danger">{{ error }}</p>

      <div class="gap-8 mt-8">
        <button type="submit" class="btn" :disabled="saving">{{ saving ? 'Saving…' : 'Save' }}</button>
        <button type="button" class="btn btn-ghost" @click="router.back()">Cancel</button>
      </div>
    </form>
  </div>
</template>

<script setup lang="ts">
import { reactive, computed, onMounted, ref, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useWidgetsStore } from '../stores/widgets';
import { Api } from '../api/http';
import { generateId } from '../utils/id';

const route  = useRoute();
const router = useRouter();
const store  = useWidgetsStore();

const paramId = route.params.id as string;
const isNew   = computed(() => paramId === 'new');
const saving  = ref(false);
const error   = ref('');
// ── Companion metric picker ──────────────────────────────────────────────────
interface CompanionMetric { id: string; label: string; category: string; unit: string; value: string }
const companionMetrics   = ref<CompanionMetric[]>([]);
const companionLoading   = ref(false);
const companionError     = ref('');

async function loadCompanionMetrics() {
  companionLoading.value = true;
  companionError.value   = '';
  try {
    const res = await fetch('http://localhost:9800/api/metrics');
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    companionMetrics.value = await res.json();
  } catch (e) {
    companionError.value = 'Companion unavailable — enter metric ID manually';
  } finally {
    companionLoading.value = false;
  }
}

// ── LVGL icon list (same as MacroEditorPage) ─────────────────────────────────
const ICONS = [
  'BULLET','AUDIO','VIDEO','LIST','OK','CLOSE','POWER','SETTINGS','HOME',
  'DOWNLOAD','DRIVE','REFRESH','MUTE','VOLUME_MID','VOLUME_MAX','IMAGE','TINT',
  'PREV','PLAY','PAUSE','STOP','NEXT','EJECT','LEFT','RIGHT','PLUS','MINUS',
  'EYE_OPEN','EYE_CLOSE','WARNING','SHUFFLE','UP','DOWN','LOOP','DIRECTORY',
  'UPLOAD','CALL','CUT','COPY','SAVE','BARS','ENVELOPE','CHARGE','PASTE','BELL',
  'KEYBOARD','GPS','FILE','WIFI','BATTERY_FULL','BATTERY_3','BATTERY_2',
  'BATTERY_1','BATTERY_EMPTY','USB','BLUETOOTH','TRASH','EDIT','BACKSPACE',
  'SD_CARD','NEW_LINE','DUMMY',
];

// Scenes available to assign to (root-level scene folders — no target_id)
const sceneItems = computed(() =>
  store.items.filter(w => w.type === 'scene' && !w.scene_id && !w.target_id)
);

let _rowIdSeq = 0;
function genRowId() { return `r${Date.now().toString(36)}_${_rowIdSeq++}`; }

const form = reactive({
  title:       '',
  icon:        '',
  type:        'text' as 'text' | 'chart' | 'progress' | 'scene' | 'counter' | 'timer',
  scene_id:    '',
  target_id:   '',   // for type=scene: which scene this card navigates to (empty = this IS the folder)
  style:       0 as 0|1|2,
  update:      0,
  url:         '',
  data_target: 'url' as 'url' | 'url_system' | 'system',
  metric_id:    '',
  parse_type:  'regex' as 'regex' | 'json',
  // regex mode
  regex:       '',
  template:    '',
  // json mode
  // Local row objects carry stable `id` for v-for keying; the `id` is stripped
  // when serializing to the device API payload.
  json_keys:   [] as Array<{ id: string; value: string }>,
  // display
  span:        1 as 0|1|2|3,
  height:      0,
  bg_color_hex: '',
  bg_style:    'gradient' as 'solid' | 'gradient',
  // headers
  headers:     [] as Array<{ id: string; key: string; value: string }>,
  // counter
  step:        1,
  min:         0,
  max:         99,
  // timer
  duration:    300,
  done_url:    '',
  // profile visibility
  profile_ids: [] as string[],
  // per-item opacity override (only saved when useCustomBgOpa is enabled)
  bg_opa: 255,
  // icon positioning
  icon_pos: 'top' as 'top' | 'bottom' | 'left' | 'right',
  // image widget
  image_src: '',
});

// ── Per-widget opacity override ───────────────────────────────────────────
const useCustomBgOpa = ref(false);
const bgOpaPercent = computed({
  get: () => Math.round((form.bg_opa / 255) * 100),
  set: (v: number) => { form.bg_opa = Math.round((v / 100) * 255); },
});

function hexToNumber(hex: string): number {
  return parseInt(hex.replace('#', ''), 16);
}
function numberToHex(n: number): string {
  return '#' + n.toString(16).padStart(6, '0').slice(-6);
}

// Darken a hex color by mixing with black (factor 0–1, 0=black, 1=original)
function darkenHex(hex: string, factor = 0.55): string {
  const n = parseInt(hex.replace('#',''), 16);
  const r = Math.round(((n >> 16) & 0xff) * factor);
  const g = Math.round(((n >> 8)  & 0xff) * factor);
  const b = Math.round(( n        & 0xff) * factor);
  return `#${((r<<16)|(g<<8)|b).toString(16).padStart(6,'0')}`;
}

const previewStyle = computed(() => {
  if (!form.bg_color_hex) return {};
  if (form.bg_style === 'gradient') {
    return { background: `linear-gradient(to bottom, ${form.bg_color_hex}, ${darkenHex(form.bg_color_hex)})` };
  }
  return { background: form.bg_color_hex };
});

const jsonTemplatePlaceholder = computed(() => {
  const keys = form.json_keys.map(k => k.value).filter(Boolean);
  if (!keys.length) return 'e.g. CPU: {cpu}%, RAM: {memory.used} MB';
  return keys.map(k => `${k}: {${k}}`).join('\n');
});

function addKey()          { form.json_keys.push({ id: genRowId(), value: '' }); }
function removeKey(i: number) { form.json_keys.splice(i, 1); }

function addHeader()            { form.headers.push({ id: genRowId(), key: '', value: '' }); }
function removeHeader(i: number) { form.headers.splice(i, 1); }

// ── URL test ──────────────────────────────────────────────────────────────────
const MAX_RESPONSE = 3500;

const urlTest = reactive({
  loading:    false,
  done:       false,
  error:      '',
  corsHint:   false,
  status:     0,
  rawSize:    0,
  rawPreview: '',
  parsed:     null as string | null,
  warnings:   [] as string[],
});

function _jsonGet(o: any, path: string): any {
  for (const p of path.split('.')) {
    if (o == null) return undefined;
    o = o[p];
  }
  return o;
}

function _applyParse(text: string): string | null {
  if (form.parse_type === 'json') {
    const keys = form.json_keys.map(k => k.value).filter(Boolean);
    if (!keys.length) return null;
    try {
      const obj = JSON.parse(text);
      let r = form.template || '';
      for (const k of keys) {
        const v = _jsonGet(obj, k);
        r = r.split(`{${k}}`).join(v != null ? String(v) : '');
      }
      return r;
    } catch { return null; }
  }
  if (form.parse_type === 'regex') {
    if (!form.regex) return null;
    try {
      const mt = text.match(new RegExp(form.regex));
      if (!mt) return null;
      let r = form.template || '';
      for (let i = 1; i < mt.length; i++) r = r.split(`{${i}}`).join(mt[i] ?? '');
      return r;
    } catch { return null; }
  }
  return null;
}

async function testUrl() {
  urlTest.loading  = true;
  urlTest.done     = false;
  urlTest.error    = '';
  urlTest.corsHint = false;
  urlTest.warnings = [];
  urlTest.parsed   = null;
  urlTest.rawPreview = '';

  try {
    const headers: Record<string, string> = {};
    for (const h of form.headers) { if (h.key) headers[h.key] = h.value; }

    const res  = await fetch(form.url.trim(), { headers });
    const text = await res.text();

    urlTest.status  = res.status;
    urlTest.rawSize = text.length;
    urlTest.rawPreview = text.slice(0, 300);

    const warns: string[] = [];
    if (text.length > MAX_RESPONSE)
      warns.push(`Response ${text.length} B exceeds ${MAX_RESPONSE} B limit — will be blocked by proxy page.`);
    else if (text.length > 1024 && !form.regex && !form.json_keys.length)
      warns.push(`Response ${text.length} B with no parse rules — device receives full text, risking heap fragmentation on no-PSRAM boards.`);

    const parsed = _applyParse(text);
    if (parsed !== null) {
      urlTest.parsed = parsed;
    } else if (form.regex || form.json_keys.filter(k => k.value).length) {
      warns.push('Parse rules configured but result is empty — check regex/keys and template.');
    }

    urlTest.warnings = warns;
  } catch (e: any) {
    urlTest.error    = e?.message ?? 'fetch failed';
    urlTest.corsHint = urlTest.error.toLowerCase().includes('cors') ||
                       urlTest.error.toLowerCase().includes('failed to fetch') ||
                       urlTest.error.toLowerCase().includes('networkerror');
  } finally {
    urlTest.loading = false;
    urlTest.done    = true;
  }
}

function applyWidget(w: Record<string, any>) {
  form.title       = w.title       ?? '';
  form.icon        = w.icon        ?? '';
  form.type        = w.type        ?? 'text';
  form.scene_id    = w.scene_id    ?? '';
  form.target_id   = w.target_id   ?? '';
  form.style       = (w.style      ?? 0) as 0|1|2;
  form.update      = w.update      ?? 0;
  form.url         = w.url         ?? '';
  form.data_target = (w.data_target ?? 'url') as 'url'|'url_system'|'system';
  form.metric_id    = (w as any).metric_id ?? '';
  form.parse_type  = (w.parse_type  ?? 'regex') as 'regex'|'json';
  form.regex       = w.regex       ?? '';
  form.template    = w.template    ?? '';
  form.json_keys   = w.json_keys ? w.json_keys.map((v: string) => ({ id: genRowId(), value: v })) : [];
  form.span        = (w.span   ?? 1) as 0|1|2|3;
  form.height      = w.height  ?? 0;
  form.bg_color_hex = (w as any).bg_color != null ? numberToHex((w as any).bg_color) : '';
  form.bg_style    = ((w as any).bg_style ?? 'gradient') as 'solid'|'gradient';
  form.headers     = w.headers ? w.headers.map((h: any) => ({ id: genRowId(), key: h.key, value: h.value })) : [];
  form.step        = (w as any).step     ?? 1;
  form.min         = (w as any).min      ?? 0;
  form.max         = (w as any).max      ?? 99;
  form.duration    = (w as any).duration ?? 300;
  form.done_url    = (w as any).done_url ?? '';
  form.profile_ids = Array.isArray(w.profile_ids) ? [...w.profile_ids] : [];
  if ((w as any).bg_opa != null) { form.bg_opa = (w as any).bg_opa; useCustomBgOpa.value = true; }
  else { form.bg_opa = 255; useCustomBgOpa.value = false; }
  form.icon_pos   = ((w as any).icon_pos   ?? 'top') as typeof form.icon_pos;
  form.image_src  = (w as any).image_src  ?? '';
}

onMounted(async () => {
  if (isNew.value) {
    const prefill = sessionStorage.getItem('widget_prefill');
    if (prefill) {
      sessionStorage.removeItem('widget_prefill');
      try { applyWidget(JSON.parse(prefill)); } catch { /* ignore */ }
    } else {
      // Pre-fill type from URL query (e.g. from "+ Add scene" button)
      const typeFromQuery = route.query.type as string;
      if (typeFromQuery && ['text','chart','progress','scene','counter','timer'].includes(typeFromQuery)) {
        form.type = typeFromQuery as typeof form.type;
      }
    }
    if (!store.items.length) await store.load();
  } else {
    if (!store.items.length) await store.load();
    const w = store.items.find(x => x.id === paramId);
    if (w?.type === 'scene') {
      router.replace(`/scene/widgets/${paramId}`);
      return;
    }
    if (w) applyWidget(w);
  }
});

async function onSave() {
  error.value  = '';
  saving.value = true;
  try {
    const id = isNew.value ? generateId() : paramId;
    const widget: any = {
      id,
      title: form.title.trim(),
      type:  form.type,
    };

    if (form.icon.trim())  widget.icon     = form.icon.trim();
    // Scene assignment: only for non-scene items OR scene navigators with a target
    if (form.type !== 'scene' || form.target_id) {
      if (form.scene_id) widget.scene_id = form.scene_id;
    }
    if (form.type === 'scene' && form.target_id) widget.target_id = form.target_id;
    // Profile visibility — only for bare scene folders
    if (form.type === 'scene' && !form.target_id && form.profile_ids.length)
      widget.profile_ids = [...form.profile_ids];

    // Counter / Timer specific fields
    if (form.type === 'counter') {
      widget.step = form.step;
      widget.min  = form.min;
      widget.max  = form.max;
    }
    if (form.type === 'timer') {
      widget.duration = form.duration;
      if (form.done_url.trim()) widget.done_url = form.done_url.trim();
    }

    // Data fields only for URL-fetching widgets
    if (!['scene','counter','timer','image'].includes(form.type)) {
      widget.update      = form.update;
      widget.data_target = form.data_target;
      widget.parse_type  = form.parse_type;
    }

    if (form.type === 'progress') widget.style = form.style;
    if (form.span !== 1)       widget.span     = form.span;
    if (form.height > 0)       widget.height   = form.height;
    if (form.bg_color_hex) {
      widget.bg_color = hexToNumber(form.bg_color_hex);
      widget.bg_style = form.bg_style;
    }

    if (!['scene','counter','timer','image'].includes(form.type)) {
      if (form.data_target === 'system') {
        if (form.metric_id.trim()) widget.metric_id = form.metric_id.trim();
      } else {
        if (form.url?.trim())    widget.url      = form.url.trim();
      }
      if (form.template.trim())  widget.template = form.template.trim();

      const validHeaders = form.headers.filter(h => h.key.trim());
      if (validHeaders.length)   widget.headers  = validHeaders.map(h => ({ key: h.key.trim(), value: h.value }));

      if (form.parse_type === 'regex') {
        if (form.regex.trim()) widget.regex = form.regex.trim();
      } else {
        const keys = form.json_keys.map(k => k.value.trim()).filter(Boolean);
        if (keys.length) widget.json_keys = keys;
      }
    }

    if (useCustomBgOpa.value) widget.bg_opa = form.bg_opa;
    if (form.icon.trim() && form.icon_pos !== 'top') widget.icon_pos = form.icon_pos;
    if (form.type === 'image' && form.image_src.trim()) widget.image_src = form.image_src.trim();
    await store.upsert(widget);
    router.push('/widgets');
  } catch (e: any) {
    error.value = e.message ?? 'Save failed';
  } finally {
    saving.value = false;
  }
}
</script>

<style scoped>
.checkbox-label { display: flex; align-items: center; gap: 6px; font-size: 13px; cursor: pointer; margin-bottom: 4px; }
.slider-row { display: flex; align-items: center; gap: 8px; }
.slider { flex: 1; }
.range-value { font-size: 12px; color: var(--text-muted); min-width: 36px; text-align: right; }

.section-divider {
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: .06em;
  color: var(--muted);
  padding: 4px 0 2px;
  border-bottom: 1px solid var(--border);
  margin-bottom: 4px;
}

/* ── parse-type radio toggle ── */
.radio-group {
  display: flex;
  gap: 8px;
}
.radio-opt {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 5px 14px;
  border: 1px solid var(--border);
  border-radius: 4px;
  cursor: pointer;
  font-size: 13px;
  background: var(--surface2);
  color: var(--text);
  transition: all .15s;
}
.radio-opt.active {
  background: var(--accent);
  color: #fff;
  border-color: var(--accent);
}
.radio-opt input[type="radio"] { display: none; }

/* ── json keys list ── */
.keys-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.key-row {
  display: flex;
  align-items: center;
  gap: 6px;
}

/* ── hint box ── */
.hint-box {
  background: var(--surface2);
  border: 1px solid var(--border);
  border-radius: 4px;
  padding: 8px 12px;
  font-size: 12px;
  color: var(--muted);
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  align-items: center;
}
.hint-box code {
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 3px;
  padding: 1px 5px;
  font-size: 11px;
  color: var(--text);
}

.hint {
  font-size: 11px;
  color: var(--muted);
}
.hint code {
  background: var(--surface2);
  border-radius: 3px;
  padding: 1px 4px;
  font-size: 11px;
}

.btn-sm { padding: 3px 8px; font-size: 12px; }

.field-row { display: flex; gap: 12px; align-items: flex-start; }
.color-swatch {
  width: 36px; height: 28px;
  padding: 2px; border: 1px solid var(--border);
  border-radius: 4px; cursor: pointer; background: none;
}
.color-preview {
  width: 100%;
  height: 28px;
  border-radius: 4px;
  border: 1px solid var(--border);
  margin-top: 4px;
}

textarea {
  width: 100%;
  padding: 8px 10px;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--surface);
  color: var(--text);
  font-size: 13px;
  box-sizing: border-box;
}
textarea:focus {
  outline: none;
  border-color: var(--accent);
}

/* ── scene banner ── */
.scene-banner {
  display: flex;
  align-items: flex-start;
  gap: 12px;
  background: #eef4ff;
  border: 1px solid #3d7ed4;
  border-left: 4px solid #3d7ed4;
  border-radius: 6px;
  padding: 10px 14px;
  margin-bottom: 4px;
}
.scene-banner-icon { font-size: 22px; flex-shrink: 0; margin-top: 1px; }

/* ── profile visibility checkboxes ── */
.profile-checks { display: flex; flex-wrap: wrap; gap: 6px 14px; }
.profile-check-item { display: flex; align-items: center; gap: 5px; font-size: 13px; cursor: pointer; }
</style>
