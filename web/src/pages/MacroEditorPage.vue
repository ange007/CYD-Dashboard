<template>
  <div class="page">
    <div class="page-header">
      <h2>{{ isNew
        ? (form.type === 'scene' ? 'New Scene' : 'New Macro')
        : (form.type === 'scene' ? 'Edit Scene' : 'Edit Macro') }}</h2>
      <span class="text-muted" style="font-size:11px">ID: {{ paramId === 'new' ? '(new)' : paramId }}</span>
    </div>

    <form class="form" @submit.prevent="onSave">

      <!-- ── Scene mode banner ── -->
      <template v-if="form.type === 'scene'">
        <div class="scene-banner">
          <span class="scene-banner-icon">{{ form.target_id ? '🔗' : '📁' }}</span>
          <div v-if="!form.target_id">
            <strong>Scene (folder)</strong><br>
            <span class="text-muted" style="font-size:12px">
              Creates a folder that groups macros. On the device this button opens the folder.
              Assign macros to it via the <strong>Scene</strong> field in each macro's editor.
            </span>
          </div>
          <div v-else>
            <strong>Scene navigator</strong><br>
            <span class="text-muted" style="font-size:12px">
              A shortcut button that opens an existing scene. Can be placed inside any scene,
              allowing the same target scene to be reached from multiple places.
            </span>
          </div>
        </div>
      </template>

      <!-- Title -->
      <div class="field">
        <label>{{ form.type === 'scene' ? 'Scene name' : 'Title' }}
          <span class="text-muted">(shown on button)</span></label>
        <input v-model="form.title" required
          :placeholder="form.type === 'scene' ? 'e.g. Home Assistant, Media, Work' : 'e.g. Copy, Play/Pause'" />
      </div>

      <!-- Icon source toggle (hidden for type=scene; custom image hidden when no SD) -->
      <div v-if="form.type !== 'scene'" class="field">
        <label>Icon source</label>
        <div class="icon-source-toggle">
          <label class="radio-item">
            <input type="radio" :value="false" v-model="useCustomImage" /> Built-in symbol
          </label>
          <label v-if="sdAvailable !== false" class="radio-item">
            <input type="radio" :value="true"  v-model="useCustomImage" /> Custom image (.bin)
          </label>
        </div>
      </div>

      <!-- Custom image gallery -->
      <div v-if="useCustomImage && form.type !== 'scene'" class="field">
        <label>Select image</label>
        <div v-if="uploadedIcons.length" class="icon-gallery">
          <div v-for="name in uploadedIcons" :key="name"
               class="icon-thumb" :class="{ selected: form.image === name }"
               @click="form.image = name">
            <img :src="iconPreviewSrc(name)" :alt="name" />
            <span class="icon-thumb-name">{{ name }}</span>
            <button type="button" class="icon-thumb-del" @click.stop="onDeleteIcon(name)" title="Delete">✕</button>
          </div>
        </div>
        <div v-else class="text-muted" style="font-size:12px;margin-bottom:6px">No images uploaded yet.</div>
        <div class="upload-row">
          <label class="btn btn-ghost btn-sm" style="cursor:pointer">
            {{ uploadingIcon ? 'Uploading…' : '+ Upload image' }}
            <input type="file" accept="image/png, image/jpeg, image/webp" style="display:none" :disabled="uploadingIcon" @change="onFileUpload" />
          </label>
          <span v-if="uploadError" class="text-danger" style="font-size:12px">{{ uploadError }}</span>
          <span v-if="form.image" class="text-muted" style="font-size:12px">Selected: {{ form.image }}</span>
        </div>
        <div class="hint-box" style="margin-top:6px">
          Auto-converted to LVGL .bin (ARGB8888). Source saved at full resolution; display copy optimized for current icon size.
        </div>
        <div class="field" style="margin-top:8px">
          <label>Image size</label>
          <select v-model="form.image_size">
            <option value="s">Small (50%)</option>
            <option value="m">Medium (75%)</option>
            <option value="l">Large (100%)</option>
          </select>
        </div>
      </div>

      <!-- Icon + Color row — built-in symbols -->
      <div v-if="!useCustomImage || form.type === 'scene'" class="field">
        <label>Icon</label>
        <div class="icon-symbol-grid">
          <div class="icon-thumb" :class="{ selected: form.icon === '' }"
               @click="form.icon = ''" title="(none)">
            <span class="icon-fa-placeholder">—</span>
            <span class="icon-thumb-name">none</span>
          </div>
          <div v-for="ic in ICONS" :key="ic"
               class="icon-thumb" :class="{ selected: form.icon === ic }"
               @click="form.icon = ic" :title="ic">
            <i :class="ICON_FA[ic] ?? 'fas fa-question'" class="icon-fa-glyph"></i>
            <span class="icon-thumb-name">{{ ic }}</span>
          </div>
        </div>
      </div>
      <div class="field-row" v-if="!useCustomImage || form.type === 'scene'">
        <!-- Live button preview -->
        <div class="field" style="flex:1">
          <label>Preview</label>
          <div class="macro-preview-wrap">
            <div class="macro-preview-btn" :style="{ backgroundColor: form.bg_color_hex || '#1e293b' }">
              <i v-if="form.icon" :class="ICON_FA[form.icon] ?? 'fas fa-question'"
                 :style="{ color: previewIconColor, fontSize: previewIconSize }"></i>
              <span v-else :style="{ color: previewIconColor }">{{ form.title || '?' }}</span>
            </div>
          </div>
        </div>
        <!-- Icon size + color — only when an icon is selected -->
        <template v-if="form.icon">
          <div class="field" style="width:100px">
            <label>Icon size</label>
            <select v-model="form.icon_size">
              <option value="s">Small</option>
              <option value="m">Medium</option>
              <option value="l">Large</option>
            </select>
          </div>
          <div class="field" style="width:120px">
            <label>Icon color</label>
            <div class="color-row">
              <input type="color" v-model="form.icon_color_hex" class="color-swatch"
                :class="{ 'swatch-auto': !form.icon_color_hex }" />
              <button v-if="form.icon_color_hex" type="button" class="btn btn-ghost btn-sm"
                style="font-size:10px;padding:2px 6px" @click="form.icon_color_hex = ''">Auto</button>
              <span v-else class="text-muted" style="font-size:11px">Auto</span>
            </div>
          </div>
        </template>
        <div class="field" style="width:120px">
          <label>Button color</label>
          <div class="color-row">
            <input type="color" v-model="form.bg_color_hex" class="color-swatch" />
            <span class="text-muted" style="font-size:11px">{{ form.bg_color_hex }}</span>
          </div>
        </div>
      </div>

      <!-- Preview for custom image mode -->
      <div v-if="useCustomImage && form.type !== 'scene'" class="field">
        <label>Preview</label>
        <div class="macro-preview-wrap">
          <div class="macro-preview-btn" :style="{ backgroundColor: form.bg_color_hex || '#1e293b' }">
            <img v-if="form.image" :src="iconPreviewSrc(form.image)" class="macro-preview-img" />
            <span v-else :style="{ color: previewIconColor }">{{ form.title || '?' }}</span>
          </div>
        </div>
      </div>

      <!-- Per-macro appearance overrides -->
      <div v-if="form.type !== 'scene'" class="field">
        <label>Appearance overrides <span class="text-muted">(override global settings for this button)</span></label>
        <div class="field-row" style="flex-wrap:wrap;gap:12px">
          <div class="field" style="flex:1;min-width:180px">
            <label class="checkbox-label">
              <input type="checkbox" v-model="useCustomRadius" />
              Custom corner radius
            </label>
            <div v-if="useCustomRadius" class="slider-row">
              <input type="range" min="0" max="50" :disabled="form.radius === 255"
                     :value="form.radius === 255 ? 50 : form.radius"
                     @input="form.radius = +($event.target as HTMLInputElement).value"
                     class="slider" />
              <span class="range-value">{{ form.radius + 'px' }}</span>
            </div>
          </div>
          <div class="field" style="flex:1;min-width:180px">
            <label class="checkbox-label">
              <input type="checkbox" v-model="useCustomBgOpa" />
              Custom transparency
            </label>
            <div v-if="useCustomBgOpa" class="slider-row">
              <input type="range" min="0" max="100" v-model.number="bgOpaPercent" class="slider" />
              <span class="range-value">{{ bgOpaPercent }}%</span>
            </div>
          </div>
        </div>
      </div>

      <!-- Type — hidden when editing a scene (type is locked) -->
      <div v-if="form.type !== 'scene'" class="field">
        <label>Type</label>
        <select v-model="form.type">
          <option value="keys">Keys / keyboard shortcut</option>
          <option value="text">Plain text (typed as-is)</option>
          <option value="multi">Multi-action sequence</option>
          <option value="toggle">Toggle (ON/OFF)</option>
          <option value="command">Command (shell/app)</option>
          <option value="action">System action</option>
          <option value="url">HTTP URL request</option>
          <option value="scene">Scene navigator (folder)</option>
        </select>
      </div>

      <!-- Scene assignment — hidden for bare scene definitions (folders live at root);
           shown for regular macros AND for scene navigators that have a target -->
      <div v-if="form.type !== 'scene' || form.target_id" class="field">
        <label>Scene <span class="text-muted">(assign to a scene / folder)</span></label>
        <select v-model="form.scene_id">
          <option value="">Root (no scene)</option>
          <option v-for="s in sceneItems" :key="s.id" :value="s.id">{{ s.title }}</option>
        </select>
      </div>

      <!-- ── Keys editor ── -->
      <template v-if="form.type === 'keys'">
        <div class="field">
          <label>Key sequence <span class="text-muted">(one action per line)</span></label>
          <div class="hint-box">
            <strong>Formats:</strong><br>
            <code>LEFT_CTRL+c</code> — key combo<br>
            <code>KEY_ENTER</code> · <code>KEY_LEFT_SHIFT</code> — special key<br>
            <code>%pause:2%</code> — pause 2 s<br>
            plain text — typed as-is<br>
            <span class="hint-cyrillic">🌐 Cyrillic text is auto-transliterated for Ukrainian/Russian keyboard layouts.</span>
          </div>
          <div v-for="(k, i) in form.keys" :key="k.id" class="key-row">
            <input v-model="k.value" list="key-suggestions" placeholder="e.g. KEY_LEFT_CTRL+c" @focus="lastFocusedKeyIndex = i" />
            <button type="button" class="btn btn-danger btn-sm" @click="removeKey(i)">✕</button>
          </div>
          <datalist id="key-suggestions">
            <option v-for="k in KEY_SUGGESTIONS" :key="k" :value="k" />
          </datalist>
          <button type="button" class="btn btn-ghost btn-sm mt-8" @click="addKey">+ Add step</button>
        </div>

        <details class="key-ref">
          <summary>Key name reference</summary>
          <div class="key-ref-grid">
            <div v-for="group in KEY_GROUPS" :key="group.label" class="key-group">
              <div class="key-group-label">{{ group.label }}</div>
              <div v-for="k in group.keys" :key="k" class="key-chip" @click="appendKey(k)">{{ k }}</div>
            </div>
          </div>
        </details>
      </template>

      <!-- ── Command editor ── -->
      <template v-if="form.type === 'command'">
        <div class="field">
          <label>Command</label>
          <textarea v-model="form.cmd" rows="6" placeholder="e.g. shutdown /s /t 0" style="font-family:monospace;resize:vertical" />
        </div>
      </template>

      <!-- ── Action editor ── -->
      <template v-if="form.type === 'action'">
        <div class="field">
          <label>Action</label>
          <select v-model="form.action_id">
            <option value="" disabled>Select an action…</option>
            <option value="restart_device">Restart device</option>
            <option value="sleep_display">Sleep display (turn off backlight)</option>
            <option value="wake_display">Wake display (restore brightness)</option>
          </select>
        </div>
      </template>

      <!-- ── URL editor ── -->
      <template v-if="form.type === 'url'">
        <div class="field">
          <label>URL</label>
          <input v-model="form.url" placeholder="http://192.168.1.x/endpoint" />
        </div>
      </template>

      <!-- ── Toggle editor ── -->
      <template v-if="form.type === 'toggle'">
        <div class="field">
          <label>ON URL <span class="text-muted">(called when toggled on)</span></label>
          <input v-model="form.on_url" placeholder="http://192.168.1.x/on" />
        </div>
        <div class="field">
          <label>OFF URL <span class="text-muted">(called when toggled off)</span></label>
          <input v-model="form.off_url" placeholder="http://192.168.1.x/off" />
        </div>
        <div class="field-row">
          <div class="field" style="flex:1">
            <label>ON color</label>
            <div class="color-row">
              <input type="color" v-model="form.on_color_hex" class="color-swatch" />
              <span class="text-muted" style="font-size:11px">{{ form.on_color_hex }}</span>
            </div>
          </div>
          <div class="field" style="flex:1">
            <label>OFF color</label>
            <div class="color-row">
              <input type="color" v-model="form.off_color_hex" class="color-swatch" />
              <span class="text-muted" style="font-size:11px">{{ form.off_color_hex }}</span>
            </div>
          </div>
        </div>
        <div class="hint-box">Button color changes to reflect current state. State is saved on device across reboots.</div>
      </template>

      <!-- ── Multi-action editor ── -->
      <template v-if="form.type === 'multi'">
        <div class="field">
          <label>Actions <span class="text-muted">(executed in order)</span></label>
          <div v-for="(act, i) in form.actions" :key="act.id" class="action-row">
            <select v-model="act.type" class="action-type-select">
              <option value="keys">Keys</option>
              <option value="text">Text</option>
              <option value="pause">Pause (ms)</option>
              <option value="url">URL</option>
            </select>
            <input
              v-if="act.type !== 'pause'"
              v-model="act.value"
              class="action-value-input"
              :placeholder="act.type === 'keys' ? 'KEY_LEFT_CTRL+c' : act.type === 'text' ? 'hello world' : 'http://...'"
            />
            <input
              v-if="act.type === 'pause'"
              v-model.number="act.ms"
              type="number" min="0" max="10000" step="100"
              class="action-value-input"
              placeholder="ms"
            />
            <button type="button" class="btn btn-danger btn-sm" @click="removeAction(i)">✕</button>
          </div>
          <button type="button" class="btn btn-ghost btn-sm mt-8" @click="addAction">+ Add step</button>
        </div>
      </template>

      <p v-if="error" class="text-danger">{{ error }}</p>

      <div class="gap-8 mt-8">
        <button type="submit" class="btn" :disabled="saving">{{ saving ? 'Saving…' : 'Save' }}</button>
        <button type="button" class="btn btn-ghost" @click="router.back()">Cancel</button>
      </div>
    </form>
  </div>
</template>

<script setup lang="ts">
import { reactive, computed, onMounted, ref, watch, inject, type Ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useMacrosStore, type MultiAction } from '../stores/macros';
import { Api, loadBaseUrl, type Settings } from '../api/http';
import { Ws } from '../api/wsApi';
import {
  iconVariantName,
  dedupeIconList,
  buildIconPreviewMap,
  uploadIconWithVariants,
} from '../utils/icons';
import { generateId } from '../utils/id';

const route  = useRoute();
const router = useRouter();
const store  = useMacrosStore();

const paramId = route.params.id as string;
const isNew   = computed(() => paramId === 'new');
const saving  = ref(false);
const error   = ref('');
const _initSettings    = inject<Ref<Settings | null>>('initSettings', ref(null));
const _initDisplay     = inject<Ref<{ w: number; h: number }>>('initDisplay', ref({ w: 320, h: 240 }));
const _initSdAvailable = inject<Ref<boolean | null>>('initSdAvailable', ref(null));

// ── LVGL icon list (matches Delphi cbb_icon) ──────────────────────────────
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

// ── FontAwesome 5 Free classes for LVGL built-in symbols ─────────────────
// LVGL embeds FA5 icons in its font — these classes give the same visuals.
const ICON_FA: Record<string, string> = {
  BULLET:        'fas fa-circle',
  AUDIO:         'fas fa-music',
  VIDEO:         'fas fa-film',
  LIST:          'fas fa-list',
  OK:            'fas fa-check',
  CLOSE:         'fas fa-times',
  POWER:         'fas fa-power-off',
  SETTINGS:      'fas fa-cog',
  HOME:          'fas fa-home',
  DOWNLOAD:      'fas fa-download',
  DRIVE:         'fas fa-hdd',
  REFRESH:       'fas fa-sync',
  MUTE:          'fas fa-volume-mute',
  VOLUME_MID:    'fas fa-volume-down',
  VOLUME_MAX:    'fas fa-volume-up',
  IMAGE:         'fas fa-image',
  TINT:          'fas fa-tint',
  PREV:          'fas fa-step-backward',
  PLAY:          'fas fa-play',
  PAUSE:         'fas fa-pause',
  STOP:          'fas fa-stop',
  NEXT:          'fas fa-step-forward',
  EJECT:         'fas fa-eject',
  LEFT:          'fas fa-caret-left',
  RIGHT:         'fas fa-caret-right',
  PLUS:          'fas fa-plus',
  MINUS:         'fas fa-minus',
  EYE_OPEN:      'fas fa-eye',
  EYE_CLOSE:     'fas fa-eye-slash',
  WARNING:       'fas fa-exclamation-triangle',
  SHUFFLE:       'fas fa-random',
  UP:            'fas fa-caret-up',
  DOWN:          'fas fa-caret-down',
  LOOP:          'fas fa-redo',
  DIRECTORY:     'fas fa-folder',
  UPLOAD:        'fas fa-upload',
  CALL:          'fas fa-phone',
  CUT:           'fas fa-cut',
  COPY:          'fas fa-copy',
  SAVE:          'fas fa-save',
  BARS:          'fas fa-bars',
  ENVELOPE:      'fas fa-envelope',
  CHARGE:        'fas fa-bolt',
  PASTE:         'fas fa-paste',
  BELL:          'fas fa-bell',
  KEYBOARD:      'fas fa-keyboard',
  GPS:           'fas fa-map-marker-alt',
  FILE:          'fas fa-file',
  WIFI:          'fas fa-wifi',
  BATTERY_FULL:  'fas fa-battery-full',
  BATTERY_3:     'fas fa-battery-three-quarters',
  BATTERY_2:     'fas fa-battery-half',
  BATTERY_1:     'fas fa-battery-quarter',
  BATTERY_EMPTY: 'fas fa-battery-empty',
  USB:           'fas fa-plug',
  BLUETOOTH:     'fab fa-bluetooth-b',
  TRASH:         'fas fa-trash-alt',
  EDIT:          'fas fa-edit',
  BACKSPACE:     'fas fa-backspace',
  SD_CARD:       'fas fa-sd-card',
  NEW_LINE:      'fas fa-level-down-alt',
  DUMMY:         'fas fa-square',
};

// ── Button preview helpers ────────────────────────────────────────────────
function hexLuminance(hex: string): number {
  const n = parseInt(hex.replace('#', ''), 16) || 0;
  return 0.299 * ((n >> 16) & 0xff) + 0.587 * ((n >> 8) & 0xff) + 0.114 * (n & 0xff);
}
const previewIconColor = computed(() =>
  form.icon_color_hex
    ? form.icon_color_hex
    : hexLuminance(form.bg_color_hex || '#1e293b') > 128 ? '#1e293b' : '#ffffff'
);
const previewIconSize = computed(() =>
  ({ s: '16px', m: '22px', l: '30px' } as Record<string, string>)[form.icon_size] ?? '16px'
);

// ── Key name groups (matches Delphi mmo_keys_help) ─────────────────────────
const KEY_GROUPS = [
  {
    label: 'Media',
    keys: [
      'KEY_MEDIA_NEXT_TRACK','KEY_MEDIA_PREVIOUS_TRACK','KEY_MEDIA_STOP',
      'KEY_MEDIA_PLAY_PAUSE','KEY_MEDIA_MUTE','KEY_MEDIA_VOLUME_UP',
      'KEY_MEDIA_VOLUME_DOWN','KEY_MEDIA_WWW_HOME','KEY_MEDIA_LOCAL_MACHINE_BROWSER',
      'KEY_MEDIA_CALCULATOR','KEY_MEDIA_WWW_BOOKMARKS','KEY_MEDIA_WWW_SEARCH',
      'KEY_MEDIA_WWW_STOP','KEY_MEDIA_WWW_BACK',
      'KEY_MEDIA_CONSUMER_CONTROL_CONFIGURATION','KEY_MEDIA_EMAIL_READER',
    ],
  },
  {
    label: 'Modifiers',
    keys: [
      'KEY_LEFT_CTRL','KEY_LEFT_SHIFT','KEY_LEFT_ALT','KEY_LEFT_GUI',
      'KEY_RIGHT_CTRL','KEY_RIGHT_SHIFT','KEY_RIGHT_ALT','KEY_RIGHT_GUI',
    ],
  },
  {
    label: 'Navigation',
    keys: [
      'KEY_UP_ARROW','KEY_DOWN_ARROW','KEY_LEFT_ARROW','KEY_RIGHT_ARROW',
      'KEY_HOME','KEY_END','KEY_PAGE_UP','KEY_PAGE_DOWN',
      'KEY_INSERT','KEY_DELETE','KEY_BACKSPACE',
    ],
  },
  {
    label: 'Special',
    keys: [
      'KEY_RETURN','KEY_ESC','KEY_TAB','KEY_CAPS_LOCK','KEY_PRTSC',
    ],
  },
  {
    label: 'F-keys',
    keys: Array.from({ length: 24 }, (_, i) => `KEY_F${i + 1}`),
  },
  {
    label: 'Numpad',
    keys: [
      ...Array.from({ length: 10 }, (_, i) => `KEY_NUM_${i}`),
      'KEY_NUM_SLASH','KEY_NUM_ASTERISK','KEY_NUM_MINUS',
      'KEY_NUM_PLUS','KEY_NUM_ENTER','KEY_NUM_PERIOD',
    ],
  },
];

const KEY_SUGGESTIONS = KEY_GROUPS.flatMap(g => g.keys);

// ── helpers ───────────────────────────────────────────────────────────────
function hexToNumber(hex: string): number {
  return parseInt(hex.replace('#', ''), 16);
}
function numberToHex(n: number): string {
  return '#' + (n >>> 0).toString(16).padStart(6, '0').slice(-6);
}

// Scenes available to assign to (root-level scene-type macros without a target — i.e. actual folders)
const sceneItems = computed(() =>
  store.items.filter(m => m.type === 'scene' && !m.scene_id && !m.target_id)
);

// ── form state ────────────────────────────────────────────────────────────
// Local row types carry a stable `id` so Vue's v-for key stays attached to
// the right DOM node when users reorder or delete items mid-edit. The id is
// stripped when serializing to the API payload.
interface KeyItem    { id: string; value: string; }
interface ActionItem extends MultiAction { id: string; }

let _rowIdSeq = 0;
function genRowId() { return `r${Date.now().toString(36)}_${_rowIdSeq++}`; }

const form = reactive({
  title:        '',
  icon:         '',
  bg_color_hex: '#1e293b',
  type:         'keys' as 'keys' | 'command' | 'action' | 'url' | 'scene' | 'toggle' | 'multi',
  scene_id:     '',
  target_id:    '',
  keys:         [{ id: genRowId(), value: '' }] as KeyItem[],
  cmd:          '',
  action_id:    '',
  url:          '',
  // Toggle
  on_url:       '',
  off_url:      '',
  on_color_hex: '#16a34a',
  off_color_hex:'#dc2626',
  // Multi-action
  actions:      [] as ActionItem[],
  // Scene visibility
  profile_ids:  [] as string[],
  // Icon size and color
  icon_size:      's' as 's' | 'm' | 'l',
  icon_color_hex: '',   // empty = auto (from bg luminance)
  // Custom image
  image:          '',   // filename of uploaded icon, empty = use built-in icon
  image_size:     'm' as 's' | 'm' | 'l',  // image icon size: s=50%, m=75%, l=100% of button
  // Per-item appearance overrides (only saved when useCustomRadius / useCustomBgOpa are enabled)
  radius:         5,
  bg_opa:         255,
});

// ── Per-macro appearance overrides ────────────────────────────────────────
const useCustomRadius = ref(false);
const useCustomBgOpa  = ref(false);
const bgOpaPercent = computed({
  get: () => Math.round((form.bg_opa / 255) * 100),
  set: (v: number) => { form.bg_opa = Math.round((v / 100) * 255); },
});

// ── Custom image gallery ──────────────────────────────────────────────────
const useCustomImage  = ref(false);
const uploadedIcons   = ref<string[]>([]);
const iconPreviews    = ref<Record<string, string>>({});  // name → data: URL
const uploadingIcon   = ref(false);
const uploadError     = ref('');
const sdAvailable     = ref<boolean | null>(null);  // null = unknown, false = no SD

// Icons are stored as per-size variants: "play_s.bin", "play_m.bin", "play_l.bin".
// The gallery shows deduplicated base names ("play.bin"); preview uses the _m variant.
function iconPreviewSrc(name: string): string {
  const cached = iconPreviews.value[name];
  if (cached) return cached;
  const base = loadBaseUrl();
  return `${base}/icons/src/${name}`;
}

// base name ("play.bin") → actual filename used as preview fallback
const _iconPreviewMap = ref<Record<string, string>>({});

async function loadIcons() {
  // If /api/init already told us SD is unavailable, skip the request entirely.
  if (_initSdAvailable.value === false) { sdAvailable.value = false; return; }
  let all: string[];
  try { all = await Api.getIcons(); }
  catch (e: any) {
    // 503 means no SD card on this firmware build.
    if (e?.status === 503 || (typeof e?.message === 'string' && e.message.includes('503'))) {
      sdAvailable.value = false;
    }
    return;
  }
  // Use sd_available from init when known; otherwise infer from the response code (200 = reachable).
  sdAvailable.value = _initSdAvailable.value ?? true;
  const { items, previewMap } = dedupeIconList(all);
  uploadedIcons.value = items;
  _iconPreviewMap.value = previewMap;
  try { iconPreviews.value = await buildIconPreviewMap(items, previewMap); }
  catch { /* previews degrade but list survives */ }
}

async function onFileUpload(e: Event) {
  const raw = (e.target as HTMLInputElement).files?.[0];
  if (!raw) return;
  uploadingIcon.value = true;
  uploadError.value = '';
  try {
    const btnSize = _initSettings.value?.btn_size ?? 70;
    const baseName = await uploadIconWithVariants(raw, btnSize);
    await loadIcons();
    form.image = baseName;
  } catch (err: any) {
    uploadError.value = err.message ?? 'Upload failed';
  } finally {
    uploadingIcon.value = false;
    (e.target as HTMLInputElement).value = '';
  }
}

// No per-macro re-optimization watcher needed:
// all 3 size variants (_s/_m/_l) are created at upload time.
// Re-optimization only happens when btn_size changes globally (in SettingsPage).

async function onDeleteIcon(name: string) {
  if (!confirm(`Delete icon "${name}"?`)) return;
  await Ws.deleteIcon(name);
  if (form.image === name) form.image = '';
  await loadIcons();
}

onMounted(async () => {
  if (isNew.value) {
    // Apply default styling from settings
    const s = _initSettings.value;
    if (s) {
      if (s.def_macro_bg)       form.bg_color_hex = '#' + (s.def_macro_bg & 0xffffff).toString(16).padStart(6, '0');
      if (s.def_macro_icon_clr) form.icon_color_hex = '#' + (s.def_macro_icon_clr & 0xffffff).toString(16).padStart(6, '0');
      if (s.def_macro_icon_sz)  form.icon_size = (['s','m','l'] as const)[s.def_macro_icon_sz] ?? 's';
      if (s.def_macro_image_sz != null) form.image_size = (['s','m','l'] as const)[s.def_macro_image_sz] ?? 'm';
    }
    const typeFromQuery = route.query.type as string;
    if (typeFromQuery && ['keys','command','action','url','scene','toggle','multi'].includes(typeFromQuery)) {
      form.type = typeFromQuery as typeof form.type;
    }
    // Apply template prefill FIRST — before any async that could block the form
    const prefill = sessionStorage.getItem('macro_prefill');
    if (prefill) {
      sessionStorage.removeItem('macro_prefill');
      try {
        const m = JSON.parse(prefill);
        if (m.title)           form.title        = m.title;
        if (m.icon)            form.icon         = m.icon;
        if (m.bg_color)        form.bg_color_hex = numberToHex(m.bg_color);
        if (m.type)            form.type         = m.type;
        if (m.scene_id)        form.scene_id     = m.scene_id;
        if (m.keys?.length)    form.keys         = (m.keys as string[]).map(v => ({ id: genRowId(), value: v }));
        if (m.cmd)             form.cmd          = m.cmd;
        if (m.action_id)       form.action_id    = m.action_id;
        if (m.url)             form.url          = m.url;
        if (m.on_url)          form.on_url       = m.on_url;
        if (m.off_url)         form.off_url      = m.off_url;
        if (m.actions?.length) form.actions      = (m.actions as MultiAction[]).map(a => ({ ...a, id: genRowId() }));
      } catch { /* ignore malformed prefill */ }
    }
    if (!store.items.length) store.load();  // non-blocking, needed for scene dropdowns
  } else {
    if (!store.items.length) await store.load();
    const m = store.items.find(x => x.id === paramId);
    if (m?.type === 'scene') {
      router.replace(`/scene/macros/${paramId}`);
      return;
    }
    if (m) {
      form.title         = m.title ?? '';
      form.icon          = m.icon ?? '';
      form.bg_color_hex  = numberToHex(m.bg_color ?? 0x1e293b);
      form.type          = m.type ?? 'keys';
      form.scene_id      = m.scene_id ?? '';
      form.target_id     = m.target_id ?? '';
      form.keys          = m.keys?.length
        ? m.keys.map(v => ({ id: genRowId(), value: v }))
        : [{ id: genRowId(), value: '' }];
      form.cmd           = m.cmd ?? '';
      form.action_id     = m.action_id ?? '';
      form.url           = m.url ?? '';
      form.on_url        = m.on_url ?? '';
      form.off_url       = m.off_url ?? '';
      form.on_color_hex  = numberToHex(m.on_color  ?? 0x16a34a);
      form.off_color_hex = numberToHex(m.off_color ?? 0xdc2626);
      form.actions       = m.actions ? m.actions.map(a => ({ ...a, id: genRowId() })) : [];
      form.profile_ids   = Array.isArray(m.profile_ids) ? [...m.profile_ids] : [];
      form.icon_size     = (m as any).icon_size ?? 's';
      form.icon_color_hex = (m as any).icon_color != null ? numberToHex((m as any).icon_color) : '';
      form.image          = (m as any).image ?? '';
      form.image_size     = (m as any).image_size ?? 'm';
      if (form.image) { useCustomImage.value = true; }
      if ((m as any).radius != null) { form.radius = (m as any).radius; useCustomRadius.value = true; }
      if ((m as any).bg_opa != null) { form.bg_opa = (m as any).bg_opa;  useCustomBgOpa.value  = true; }
    }
  }

  // Load icons in background — don't block form display/prefill
  loadIcons();
});

const lastFocusedKeyIndex = ref(-1);
function addKey()              { form.keys.push({ id: genRowId(), value: '' }); }
function removeKey(i: number)  { form.keys.splice(i, 1); if (lastFocusedKeyIndex.value >= form.keys.length) lastFocusedKeyIndex.value = -1; }
function appendKey(k: string) {
  const idx = lastFocusedKeyIndex.value;
  if (idx >= 0 && idx < form.keys.length) {
    const cur = form.keys[idx].value;
    form.keys[idx].value = cur ? cur + '+' + k : k;
  } else {
    form.keys.push({ id: genRowId(), value: k });
  }
}

function addAction()            { form.actions.push({ id: genRowId(), type: 'keys', value: '' }); }
function removeAction(i: number){ form.actions.splice(i, 1); }

async function onSave() {
  error.value = '';
  saving.value = true;
  try {
    const id = isNew.value ? generateId() : paramId;
    const macro: any = {
      id,
      title:    form.title.trim(),
      type:     form.type,
      bg_color: hexToNumber(form.bg_color_hex),
    };
    if (form.icon.trim())              macro.icon      = form.icon;
    // Scene assignment only when: not a scene definition (type=scene without target)
    if (form.type !== 'scene' || form.target_id) {
      if (form.scene_id)               macro.scene_id  = form.scene_id;
    }
    if (form.type === 'scene' && form.target_id)
                                       macro.target_id = form.target_id;
    // Profile visibility — only for bare scene folders
    if (form.type === 'scene' && !form.target_id && form.profile_ids.length)
                                       macro.profile_ids = [...form.profile_ids];
    // Custom image takes precedence over built-in icon
    if (useCustomImage.value && form.image) {
      (macro as any).image = form.image;
      if (form.image_size !== 'm') (macro as any).image_size = form.image_size;
      delete macro.icon;  // mutually exclusive
    } else {
      // Icon size + color (only when an icon symbol is set)
      if (form.icon.trim()) {
        if (form.icon_size !== 's')    (macro as any).icon_size  = form.icon_size;
        if (form.icon_color_hex)       (macro as any).icon_color = hexToNumber(form.icon_color_hex);
      }
    }
    if (form.type === 'keys')     macro.keys      = form.keys.map(k => k.value).filter(v => v.trim());
    if (form.type === 'command')  macro.cmd       = form.cmd.trim();
    if (form.type === 'action')   macro.action_id = form.action_id;
    if (form.type === 'url')      macro.url       = form.url.trim();
    if (form.type === 'toggle') {
      macro.on_url   = form.on_url.trim();
      macro.off_url  = form.off_url.trim();
      macro.on_color = hexToNumber(form.on_color_hex);
      macro.off_color= hexToNumber(form.off_color_hex);
    }
    if (form.type === 'multi') {
      // Strip local `id` field before sending to device API — it's UI-only.
      macro.actions = form.actions
        .filter(a => a.type === 'pause' ? true : (a.value ?? '').trim())
        .map(({ id, ...rest }) => rest);
    }
    if (useCustomRadius.value) macro.radius = form.radius;
    if (useCustomBgOpa.value)  macro.bg_opa = form.bg_opa;
    await store.upsert(macro);
    router.push('/macros');
  } catch (e: any) {
    error.value = e.message ?? 'Save failed';
  } finally {
    saving.value = false;
  }
}
</script>

<style scoped>
.field-row { display: flex; gap: 12px; align-items: flex-start; }

.color-row { display: flex; gap: 8px; align-items: center; margin-top: 4px; }
.color-swatch {
  width: 36px; height: 28px;
  padding: 2px; border: 1px solid var(--border);
  border-radius: 4px; cursor: pointer; background: none;
}

.checkbox-label { display: flex; align-items: center; gap: 6px; font-size: 13px; cursor: pointer; margin-bottom: 6px; }
.slider-row { display: flex; align-items: center; gap: 8px; }
.slider { flex: 1; }
.range-value { font-size: 12px; color: var(--text-muted); min-width: 40px; text-align: right; }

.hint-box {
  background: var(--surface2, #f8fafc);
  border: 1px solid var(--border, #e2e8f0);
  border-radius: 6px;
  padding: 8px 12px;
  font-size: 12px;
  color: var(--text-muted, #64748b);
  margin-bottom: 8px;
  line-height: 1.6;
}

.hint-cyrillic {
  display: block;
  margin-top: 6px;
  padding-top: 6px;
  border-top: 1px solid var(--border, #e2e8f0);
  color: var(--text, #334155);
}

.key-row { display: flex; gap: 8px; margin-bottom: 4px; }
.key-row input { flex: 1; }

code {
  background: var(--surface2, #f3f4f6);
  color: var(--text);
  padding: 1px 4px;
  border-radius: 3px;
  font-size: 11px;
}

/* Key reference collapsible */
.key-ref {
  border: 1px solid var(--border);
  border-radius: 6px;
  margin-top: 4px;
  background: var(--surface);
}
.key-ref summary {
  padding: 6px 10px;
  cursor: pointer;
  font-size: 12px;
  color: var(--muted);
  user-select: none;
}
.key-ref-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  padding: 10px;
}
.key-group { min-width: 160px; }
.key-group-label {
  font-size: 11px;
  font-weight: 600;
  color: var(--muted);
  text-transform: uppercase;
  margin-bottom: 4px;
}
.key-chip {
  display: inline-block;
  background: var(--surface2);
  border: 1px solid var(--border);
  color: var(--text);
  border-radius: 4px;
  padding: 2px 6px;
  font-size: 11px;
  font-family: monospace;
  margin: 2px 2px 0 0;
  cursor: pointer;
}
.key-chip:hover { background: var(--border); }

/* ── multi-action editor ── */
.action-row { display: flex; gap: 6px; margin-bottom: 4px; align-items: center; }
.action-type-select { width: 110px; flex-shrink: 0; }
.action-value-input { flex: 1; }

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

.profile-checks {
  display: flex;
  flex-direction: column;
  gap: 6px;
  margin-top: 4px;
}
.profile-check-item {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  cursor: pointer;
}
.profile-check-item input[type="checkbox"] {
  width: 15px;
  height: 15px;
  cursor: pointer;
}

/* ── icon source toggle ── */
.icon-source-toggle {
  display: flex;
  gap: 16px;
  margin-top: 4px;
}
.radio-item {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  cursor: pointer;
}

/* ── custom image gallery ── */
.icon-symbol-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-bottom: 8px;
  max-height: 260px;
  overflow-y: auto;
  padding: 4px;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: var(--surface, #fff);
}
.icon-fa-glyph {
  font-size: 20px;
  line-height: 1;
  color: var(--text, #1e293b);
}
.icon-fa-placeholder {
  font-size: 20px;
  line-height: 1;
  color: var(--muted);
}

.macro-preview-wrap {
  display: flex;
  align-items: center;
  gap: 12px;
}
.macro-preview-btn {
  width: 72px;
  height: 72px;
  border-radius: 4px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  box-shadow: 0 2px 8px rgba(0,0,0,.25);
  overflow: hidden;
}
.macro-preview-btn span {
  font-size: 11px;
  font-weight: 600;
  text-align: center;
  padding: 4px;
  line-height: 1.2;
  word-break: break-word;
}
.macro-preview-img {
  width: 75%;
  height: 75%;
  object-fit: contain;
}
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
.icon-thumb:hover { border-color: var(--accent, #3d7ed4); }
.icon-thumb.selected { border-color: var(--accent, #3d7ed4); background: #eef4ff; }
.icon-thumb img { width: 40px; height: 40px; object-fit: contain; }
.icon-thumb-name {
  font-size: 9px;
  color: var(--muted);
  word-break: break-all;
  text-align: center;
  line-height: 1.2;
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.icon-thumb-del {
  position: absolute;
  top: 2px; right: 2px;
  background: var(--danger, #dc2626);
  color: #fff;
  border: none;
  border-radius: 3px;
  width: 14px; height: 14px;
  font-size: 9px;
  cursor: pointer;
  display: none;
  align-items: center;
  justify-content: center;
  padding: 0;
  line-height: 1;
}
.icon-thumb:hover .icon-thumb-del { display: flex; }

/* Background image thumbnails — wider than icon thumbs */
.bg-thumb { width: 80px; height: 80px; padding: 4px; justify-content: center; }
.bg-thumb-icon { font-size: 28px; line-height: 1; margin-top: 4px; }
.bg-thumb-size { font-size: 8px; color: var(--text-muted, #888); margin-bottom: 14px; }
.bg-thumb .icon-thumb-name { position: absolute; bottom: 0; left: 0; right: 0; background: rgba(0,0,0,.55); color: #fff; }

.upload-row {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

/* dim color swatch when no color selected (auto mode) */
.swatch-auto { opacity: 0.4; }
</style>
