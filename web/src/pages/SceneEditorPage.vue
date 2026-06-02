<template>
  <div class="page">
    <div class="page-header">
      <h2>{{ isNew ? 'New Scene' : 'Edit Scene' }}</h2>
      <span class="text-muted" style="font-size:11px">ID: {{ paramId === 'new' ? '(new)' : paramId }}</span>
    </div>

    <form class="form" @submit.prevent="onSave">

      <!-- ── Scene type banner ── -->
      <div class="scene-banner">
        <span class="scene-banner-icon">{{ form.target_id ? '🔗' : '📁' }}</span>
        <div v-if="!form.target_id">
          <strong>Scene (folder)</strong><br>
          <span class="text-muted" style="font-size:12px">
            Creates a folder that groups {{ context === 'macros' ? 'macros' : 'widgets' }}.
            On the device this button opens the folder.
          </span>
        </div>
        <div v-else>
          <strong>Scene navigator</strong><br>
          <span class="text-muted" style="font-size:12px">
            A shortcut that opens an existing scene. Can be placed inside any scene,
            allowing the same target scene to be reached from multiple places.
          </span>
        </div>
      </div>

      <!-- ── Title ── -->
      <div class="field">
        <label>Scene name</label>
        <input v-model="form.title" required placeholder="e.g. Home Assistant, Media, Work" />
      </div>

      <!-- ── Icon picker ── -->
      <div class="field">
        <label>Icon</label>
        <div class="icon-symbol-grid">
          <!-- none -->
          <div class="icon-thumb" :class="{ selected: form.icon === '' }" @click="form.icon = ''">
            <span style="font-size:18px;line-height:1;display:block;text-align:center;padding-top:10px">—</span>
            <span class="icon-thumb-name">none</span>
          </div>
          <!-- Built-in LVGL symbols -->
          <div v-for="ic in ICONS" :key="ic"
               class="icon-thumb" :class="{ selected: form.icon === ic }"
               @click="form.icon = ic">
            <i :class="[ICON_FA[ic] ?? 'fas fa-question', 'icon-fa-glyph']"></i>
            <span class="icon-thumb-name">{{ ic }}</span>
          </div>
          <!-- SD icons (base name without _m.bin) -->
          <div v-for="sdIcon in sdIconNames" :key="sdIcon"
               class="icon-thumb sd-icon-thumb" :class="{ selected: form.icon === sdIcon }"
               @click="form.icon = sdIcon">
            <img v-if="sdIconPreviews[sdIcon]" :src="sdIconPreviews[sdIcon]" class="sd-icon-img" />
            <span v-else class="icon-fa-glyph" style="font-size:18px">🖼</span>
            <span class="icon-thumb-name">{{ sdIcon.replace(/_m\.bin$/, '').replace(/\.bin$/, '') }}</span>
          </div>
        </div>
        <div v-if="form.icon" class="hint-box" style="margin-top:6px">
          Selected: <strong>{{ form.icon }}</strong>
        </div>
      </div>

      <!-- ── Navigate-to ── -->
      <div class="field">
        <label>Navigate to <span class="text-muted">(which scene this button opens)</span></label>
        <select v-model="form.target_id">
          <option value="">(This IS the scene folder — creates a new folder)</option>
          <option v-for="s in sceneDefinitions" :key="s.id" :value="s.id">{{ s.title }}</option>
        </select>
      </div>

      <!-- ── Profile visibility (folder only) ── -->
      <div v-if="!form.target_id" class="field">
        <label>Visible in profiles
          <span class="text-muted">(empty = show in all profiles and root)</span>
        </label>
        <div v-if="!availableProfiles.length" class="text-muted" style="font-size:12px">
          No profiles defined yet.
        </div>
        <div v-else class="profile-checks">
          <label v-for="p in availableProfiles" :key="p.id" class="profile-check-item">
            <input type="checkbox" :value="p.id" v-model="form.profile_ids" />
            {{ p.name }}
          </label>
        </div>
        <div class="hint-box" style="margin-top:6px">
          When profiles are selected, this scene button is <strong>only visible</strong> on the device
          when one of those profiles is active. Leave empty to show it always.
        </div>
      </div>

      <!-- ── Scene background (existing folder only) ── -->
      <div v-if="!form.target_id && !isNew" class="field">
        <label>Scene background
          <span class="text-muted">(shown behind {{ context === 'macros' ? 'buttons' : 'widgets' }} when this scene is active)</span>
        </label>
        <div v-if="bgFiles.length === 0 && !currentSceneBg" class="text-muted" style="font-size:12px;margin-bottom:6px">
          No background images on SD card yet.
        </div>
        <div v-if="bgFiles.length > 0 || currentSceneBg" class="icon-gallery" style="margin-bottom:8px">
          <div class="icon-thumb bg-thumb" :class="{ selected: !currentSceneBg }" @click="onBgAssign('')">
            <span style="font-size:20px;line-height:72px;text-align:center;display:block;color:var(--text-muted)">✕</span>
            <span class="icon-thumb-name">none</span>
          </div>
          <div v-for="bg in bgFiles" :key="bg.name"
               class="icon-thumb bg-thumb" :class="{ selected: currentSceneBg === bg.name }"
               @click="onBgAssign(bg.name)">
            <span class="bg-thumb-icon">🖼</span>
            <span class="icon-thumb-name" :title="bg.name">{{ bg.name }}</span>
            <span class="bg-thumb-size">{{ fmtSize(bg.size) }}</span>
          </div>
        </div>
        <div class="upload-row">
          <label class="btn btn-ghost btn-sm" style="cursor:pointer">
            {{ bgUploading ? 'Uploading…' : '+ Upload background' }}
            <input type="file" accept="image/png, image/jpeg, image/webp" style="display:none"
                   :disabled="bgUploading" @change="onBgUpload" />
          </label>
          <span v-if="bgUploadError" class="text-danger" style="font-size:12px">{{ bgUploadError }}</span>
          <span v-if="currentSceneBg" class="text-muted" style="font-size:12px">Current: {{ currentSceneBg }}</span>
        </div>
        <div class="hint-box" style="margin-top:6px">
          PNG/JPEG/WebP accepted — auto-converted to JPEG and resized to fit screen. Stored on the device's SD card.
        </div>
      </div>

      <!-- ── Re-optimize icons (folder only, existing scene) ── -->
      <div v-if="!form.target_id && !isNew" class="field">
        <label>Re-optimize icons
          <span class="text-muted">(rebuild icon size variants for this scene)</span>
        </label>
        <div style="display:flex;align-items:center;gap:10px;flex-wrap:wrap">
          <button type="button" class="btn btn-ghost btn-sm"
                  :disabled="reoptRunning" @click="reoptimizeSceneIcons">
            {{ reoptRunning ? 'Optimizing…' : 'Re-optimize' }}
          </button>
          <span class="text-muted" style="font-size:12px">
            {{ reoptDone ? `Done — ${reoptCount} icon(s) processed` : '' }}
          </span>
        </div>
        <div v-if="reoptRunning || reoptDone" class="progress-bar-wrap" style="margin-top:8px">
          <div class="progress-bar-fill" :style="{ width: (reoptProgress * 100).toFixed(0) + '%' }"></div>
        </div>
        <div class="hint-box" style="margin-top:6px">
          Rebuilds <em>_s/_m/_l</em> variants for icons used by
          {{ context === 'macros' ? 'macros' : 'widgets' }} in this scene.
          Run after changing button size in Settings.
        </div>
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
import { reactive, computed, onMounted, ref, inject, type Ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useMacrosStore } from '../stores/macros';
import { useWidgetsStore } from '../stores/widgets';
import { Api } from '../api/http';
import { awaitInit, initSettings, initProfiles } from '../api/init';
import { Ws } from '../api/wsApi';
import { generateId } from '../utils/id';
import { lvglBinToDataUrl, resizeBackgroundJpeg } from '../utils/image';
import { reoptimizeIcons } from '../utils/icons';
import { loadBaseUrl } from '../api/http';

const route  = useRoute();
const router = useRouter();

const context = route.params.context as 'macros' | 'widgets';
const paramId = route.params.id as string;
const isNew   = paramId === 'new';

const macrosStore  = useMacrosStore();
const widgetsStore = useWidgetsStore();
const store = context === 'macros' ? macrosStore : widgetsStore;

const _initDisplay = inject<Ref<{ w: number; h: number }>>('initDisplay', ref({ w: 320, h: 240 }));

const saving  = ref(false);
const error   = ref('');

// ── LVGL icon list ────────────────────────────────────────────────────────
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

const ICON_FA: Record<string, string> = {
  BULLET:'fas fa-circle', AUDIO:'fas fa-music', VIDEO:'fas fa-film', LIST:'fas fa-list',
  OK:'fas fa-check', CLOSE:'fas fa-times', POWER:'fas fa-power-off', SETTINGS:'fas fa-cog',
  HOME:'fas fa-home', DOWNLOAD:'fas fa-download', DRIVE:'fas fa-hdd', REFRESH:'fas fa-sync',
  MUTE:'fas fa-volume-mute', VOLUME_MID:'fas fa-volume-down', VOLUME_MAX:'fas fa-volume-up',
  IMAGE:'fas fa-image', TINT:'fas fa-tint', PREV:'fas fa-step-backward', PLAY:'fas fa-play',
  PAUSE:'fas fa-pause', STOP:'fas fa-stop', NEXT:'fas fa-step-forward', EJECT:'fas fa-eject',
  LEFT:'fas fa-caret-left', RIGHT:'fas fa-caret-right', PLUS:'fas fa-plus', MINUS:'fas fa-minus',
  EYE_OPEN:'fas fa-eye', EYE_CLOSE:'fas fa-eye-slash', WARNING:'fas fa-exclamation-triangle',
  SHUFFLE:'fas fa-random', UP:'fas fa-caret-up', DOWN:'fas fa-caret-down', LOOP:'fas fa-redo',
  DIRECTORY:'fas fa-folder', UPLOAD:'fas fa-upload', CALL:'fas fa-phone', CUT:'fas fa-cut',
  COPY:'fas fa-copy', SAVE:'fas fa-save', BARS:'fas fa-bars', ENVELOPE:'fas fa-envelope',
  CHARGE:'fas fa-bolt', PASTE:'fas fa-paste', BELL:'fas fa-bell', KEYBOARD:'fas fa-keyboard',
  GPS:'fas fa-map-marker-alt', FILE:'fas fa-file', WIFI:'fas fa-wifi',
  BATTERY_FULL:'fas fa-battery-full', BATTERY_3:'fas fa-battery-three-quarters',
  BATTERY_2:'fas fa-battery-half', BATTERY_1:'fas fa-battery-quarter',
  BATTERY_EMPTY:'fas fa-battery-empty', USB:'fas fa-plug', BLUETOOTH:'fab fa-bluetooth-b',
  TRASH:'fas fa-trash-alt', EDIT:'fas fa-edit', BACKSPACE:'fas fa-backspace',
  SD_CARD:'fas fa-sd-card', NEW_LINE:'fas fa-level-down-alt', DUMMY:'fas fa-square',
};

// ── SD icon list ──────────────────────────────────────────────────────────
// Names are the full _m.bin filenames (e.g. "play_m.bin"); displayed without suffix.
const sdIconNames    = ref<string[]>([]);
const sdIconPreviews = ref<Record<string, string>>({});

async function loadSdIcons() {
  try {
    const all = await Api.getIcons();
    sdIconNames.value = all.filter((f: string) => f.endsWith('_m.bin'));
    const base = loadBaseUrl();
    const map: Record<string, string> = {};
    // Prefer compressed source (png/jpg/svg/webp/gif via server fallback),
    // decode ARGB8888 .bin only as last resort. GET is used instead of HEAD
    // because PsychicHttp /icons/* is registered for HTTP_GET only.
    await Promise.all(sdIconNames.value.map(async (name) => {
      const srcKey = name.replace(/_m\.bin$/, '.bin');
      const srcUrl = `${base}/icons/src/${srcKey}`;
      try {
        const resp = await fetch(srcUrl);
        if (resp.ok) {
          const ct = resp.headers.get('content-type') || '';
          if (ct.startsWith('image/')) { map[name] = srcUrl; return; }
        }
      } catch { /* fall through */ }
      try {
        map[name] = await lvglBinToDataUrl(`${base}/icons/${name}`);
      } catch { /* skip preview on error */ }
    }));
    sdIconPreviews.value = map;
  } catch { /* no SD or unavailable */ }
}

// ── Profiles ──────────────────────────────────────────────────────────────
const availableProfiles = ref<{ id: string; name: string }[]>([]);

// ── Form ──────────────────────────────────────────────────────────────────
const form = reactive({
  title:       '',
  icon:        '',
  target_id:   '',
  profile_ids: [] as string[],
});

// ── Scene definitions for navigate-to dropdown ────────────────────────────
const sceneDefinitions = computed(() =>
  store.items.filter((s: any) => s.type === 'scene' && !s.target_id && s.id !== paramId)
);

// ── Scene background ──────────────────────────────────────────────────────
type BgFile = { name: string; size: number };
const bgFiles        = ref<BgFile[]>([]);
const bgUploading    = ref(false);
const bgUploadError  = ref('');
const currentSceneBg = ref('');
const bgDirty        = ref(false);

function fmtSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

async function loadSceneBg() {
  if (isNew || form.target_id) return;
  try {
    const [files, map] = await Promise.all([Api.getBackgrounds(), Api.getSceneBgs()]);
    bgFiles.value        = files;
    currentSceneBg.value = (map as Record<string, string>)[paramId] ?? '';
  } catch { /* no SD */ }
}

function onBgAssign(filename: string) {
  currentSceneBg.value = filename;
  bgDirty.value = true;
}

async function onBgUpload(e: Event) {
  const raw = (e.target as HTMLInputElement).files?.[0];
  if (!raw) return;
  bgUploading.value   = true;
  bgUploadError.value = '';
  try {
    const file = await resizeBackgroundJpeg(raw, {
      targetW: _initDisplay.value.w,
      targetH: _initDisplay.value.h,
      fit: 'cover',
    });
    await Api.uploadBackground(file);
    bgFiles.value = await Api.getBackgrounds();
    onBgAssign(file.name);
  } catch (err: any) {
    bgUploadError.value = err.message ?? 'Upload failed';
  } finally {
    bgUploading.value = false;
    (e.target as HTMLInputElement).value = '';
  }
}

// ── Re-optimize ───────────────────────────────────────────────────────────
const reoptRunning  = ref(false);
const reoptProgress = ref(0);
const reoptDone     = ref(false);
const reoptCount    = ref(0);

async function reoptimizeSceneIcons() {
  const sceneItems = store.items.filter(
    (m: any) => m.scene_id === paramId && m.type !== 'scene'
  );
  const usedIcons = [...new Set(
    sceneItems.map((m: any) => m.icon).filter(Boolean) as string[]
  )];
  if (!usedIcons.length) { reoptDone.value = true; reoptCount.value = 0; return; }

  reoptRunning.value  = true;
  reoptProgress.value = 0;
  reoptDone.value     = false;

  const btnSize = initSettings.value?.btn_size ?? 70;

  let processed = 0;
  await reoptimizeIcons(usedIcons, btnSize, (done, total) => {
    processed = total;
    reoptProgress.value = total > 0 ? done / total : 1;
  });

  reoptProgress.value = 1;
  reoptRunning.value  = false;
  reoptDone.value     = true;
  reoptCount.value    = processed;
}

// ── onMounted ─────────────────────────────────────────────────────────────
onMounted(async () => {
  if (!store.items.length) await store.load();

  if (!isNew) {
    const item = store.items.find((x: any) => x.id === paramId);
    if (item) {
      form.title       = (item as any).title       ?? '';
      form.icon        = (item as any).icon        ?? '';
      form.target_id   = (item as any).target_id   ?? '';
      form.profile_ids = Array.isArray((item as any).profile_ids)
                           ? [...(item as any).profile_ids] : [];
    }
  }

  awaitInit().then(() => {
    availableProfiles.value = (initProfiles.value ?? []).map((x: any) => ({ id: x.id, name: x.name }));
  });
  loadSdIcons();
  loadSceneBg();
});

// ── Save ──────────────────────────────────────────────────────────────────
async function onSave() {
  error.value  = '';
  saving.value = true;
  try {
    const id   = isNew ? generateId() : paramId;
    const item: any = {
      id,
      title: form.title.trim(),
      type:  'scene',
    };
    if (form.icon.trim())                          item.icon        = form.icon.trim();
    if (form.target_id)                            item.target_id   = form.target_id;
    if (!form.target_id && form.profile_ids.length) item.profile_ids = [...form.profile_ids];

    await store.upsert(item);
    if (bgDirty.value) {
      await Ws.setSceneBg(id, currentSceneBg.value);
      bgDirty.value = false;
    }
    router.back();
  } catch (err: any) {
    error.value = err.message ?? 'Save failed';
  } finally {
    saving.value = false;
  }
}
</script>

<style scoped>
/* ── icon grid ── */
.icon-symbol-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 6px;
  max-height: 260px;
  overflow-y: auto;
  padding: 4px 2px;
}

.icon-thumb {
  display: flex;
  flex-direction: column;
  align-items: center;
  width: 60px;
  padding: 6px 4px 4px;
  border: 1px solid var(--border);
  border-radius: 5px;
  cursor: pointer;
  background: var(--surface2);
  transition: border-color .12s, background .12s;
  user-select: none;
}
.icon-thumb:hover   { border-color: var(--accent); background: var(--surface); }
.icon-thumb.selected { border-color: var(--accent); background: #1b3a5c; }

.icon-fa-glyph {
  font-size: 18px;
  line-height: 1;
  margin-bottom: 4px;
  color: var(--text);
}

.sd-icon-img {
  width: 28px;
  height: 28px;
  object-fit: contain;
  image-rendering: pixelated;
  margin-bottom: 2px;
}

.icon-thumb-name {
  font-size: 9px;
  color: var(--muted);
  text-align: center;
  line-height: 1.2;
  word-break: break-all;
  max-width: 100%;
}

/* ── BG gallery ── */
.bg-thumb {
  width: 72px;
  height: 80px;
  justify-content: space-between;
  padding: 4px;
}
.bg-thumb-icon { font-size: 24px; margin: 4px 0; }
.bg-thumb-size { font-size: 9px; color: var(--muted); }

.upload-row { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }

/* ── profile checks ── */
.profile-checks { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 4px; }
.profile-check-item {
  display: flex; align-items: center; gap: 5px;
  font-size: 13px; cursor: pointer;
}

/* ── progress bar ── */
.progress-bar-wrap {
  height: 6px;
  background: var(--border, #ddd);
  border-radius: 3px;
  overflow: hidden;
}
.progress-bar-fill {
  height: 100%;
  background: var(--accent, #2196f3);
  transition: width .1s linear;
}

/* ── hint box ── */
.hint-box {
  background: var(--surface2, #1e293b);
  border: 1px solid var(--border);
  border-radius: 4px;
  padding: 6px 10px;
  font-size: 12px;
  color: var(--muted);
  line-height: 1.5;
}

/* ── scene banner ── */
.scene-banner {
  display: flex; align-items: flex-start; gap: 10px;
  background: var(--surface2);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 10px 12px;
  margin-bottom: 4px;
  font-size: 13px;
}
.scene-banner-icon { font-size: 20px; line-height: 1.2; flex-shrink: 0; }
</style>
