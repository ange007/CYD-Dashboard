<template>
  <div class="page">
    <div class="page-header">
      <h2>Macros</h2>
      <div class="toolbar">
        <RouterLink to="/scene/macros/new" class="btn btn-ghost">+ Add scene</RouterLink>
        <button class="btn btn-ghost" @click="showTemplates = true">From template</button>
        <RouterLink to="/macros/new" class="btn">+ Add macro</RouterLink>
      </div>
    </div>

    <p v-if="store.loading" class="text-muted">Loading…</p>
    <p v-else-if="store.error" class="text-danger">{{ store.error }}</p>

    <template v-else>
      <!-- ── Root macros ─────────────────────────────────────────────────── -->
      <div class="tree-section">
        <div class="tree-header tree-header-root">
          <span class="tree-header-title">Root</span>
          <span class="text-muted" style="font-size:11px">{{ rootMacros.length }} macro(s)</span>
          <div class="tree-header-actions">
            <button class="btn btn-ghost btn-sm" title="Root background &amp; re-optimize" @click="openRootPopup">🖼 BG</button>
          </div>
        </div>
        <table v-if="rootMacros.length" class="table tree-table">
          <tbody>
            <tr v-for="(m, idx) in rootMacros" :key="m.id">
              <td class="order-cell">
                <button class="sort-btn" :disabled="idx === 0" @click="store.moveInScene(m.id, -1)" title="Move up">▲</button>
                <button class="sort-btn" :disabled="idx === rootMacros.length - 1" @click="store.moveInScene(m.id, 1)" title="Move down">▼</button>
              </td>
              <td>{{ m.title || '—' }}</td>
              <td class="type-cell">{{ m.type }}</td>
              <td class="icon-cell">{{ m.icon || '—' }}</td>
              <td class="actions-cell">
                <select class="scene-assign" :value="m.scene_id ?? ''" @change="onAssign(m.id, ($event.target as HTMLSelectElement).value)" title="Move to scene">
                  <option value="">Root</option>
                  <option v-for="s in sceneItems" :key="s.id" :value="s.id">📁 {{ s.title }}</option>
                </select>
                <RouterLink :to="`/macros/${m.id}`" class="btn btn-ghost btn-sm">Edit</RouterLink>
                <button class="btn btn-danger btn-sm" @click="onDelete(m.id)">Del</button>
              </td>
            </tr>
          </tbody>
        </table>
        <div v-else class="tree-empty">No root-level macros. Click <strong>+ Add macro</strong>.</div>
      </div>

      <!-- ── Scene sections ──────────────────────────────────────────────── -->
      <div v-for="scene in sceneItems" :key="scene.id" class="tree-section">
        <div class="tree-header tree-header-scene">
          <span class="scene-icon">📁</span>
          <span class="tree-header-title">{{ scene.title || '—' }}</span>
          <span class="text-muted" style="font-size:11px">{{ macrosInScene(scene.id).length }} macro(s)</span>
          <div class="tree-header-actions">
            <RouterLink :to="`/scene/macros/${scene.id}`" class="btn btn-ghost btn-sm">Edit scene</RouterLink>
            <button class="btn btn-danger btn-sm" @click="onDeleteScene(scene.id)">Delete scene</button>
          </div>
        </div>
        <table v-if="macrosInScene(scene.id).length" class="table tree-table">
          <tbody>
            <tr v-for="(m, idx) in macrosInScene(scene.id)" :key="m.id">
              <td class="order-cell">
                <button class="sort-btn" :disabled="idx === 0" @click="store.moveInScene(m.id, -1)" title="Move up">▲</button>
                <button class="sort-btn" :disabled="idx === macrosInScene(scene.id).length - 1" @click="store.moveInScene(m.id, 1)" title="Move down">▼</button>
              </td>
              <td>{{ m.title || '—' }}</td>
              <td class="type-cell">{{ m.type }}</td>
              <td class="icon-cell">{{ m.icon || '—' }}</td>
              <td class="actions-cell">
                <select class="scene-assign" :value="m.scene_id ?? ''" @change="onAssign(m.id, ($event.target as HTMLSelectElement).value)" title="Move to scene">
                  <option value="">Root</option>
                  <option v-for="s in sceneItems" :key="s.id" :value="s.id">📁 {{ s.title }}</option>
                </select>
                <RouterLink :to="`/macros/${m.id}`" class="btn btn-ghost btn-sm">Edit</RouterLink>
                <button class="btn btn-danger btn-sm" @click="onDelete(m.id)">Del</button>
              </td>
            </tr>
          </tbody>
        </table>
        <div v-else class="tree-empty">No macros in this scene yet.</div>
      </div>

      <div v-if="!store.items.length" class="card text-muted" style="margin-top:16px">
        No macros yet. Click <strong>+ Add macro</strong> or <strong>From template</strong>.
      </div>
    </template>
  </div>

  <!-- ── Root popup ────────────────────────────────────────────────────────── -->
  <Teleport to="body">
    <div v-if="rootPopupOpen" class="modal-backdrop" @click.self="rootPopupOpen = false">
      <div class="modal">
        <div class="modal-header">
          <h3>Root — background &amp; re-optimize</h3>
          <button class="btn btn-ghost btn-sm" @click="rootPopupOpen = false">✕</button>
        </div>
        <div class="modal-body">
          <!-- BG picker -->
          <div style="font-weight:600;font-size:13px;margin-bottom:6px">Scene background</div>
          <div v-if="rootBgFiles.length === 0" class="text-muted" style="font-size:12px;margin-bottom:8px">
            No background images on SD card yet.
          </div>
          <div v-if="rootBgFiles.length > 0" class="icon-gallery" style="margin-bottom:8px">
            <div class="icon-thumb bg-thumb" :class="{ selected: !rootBg }" @click="assignRootBg('')">
              <span style="font-size:20px;line-height:72px;text-align:center;display:block;color:var(--text-muted)">✕</span>
              <span class="icon-thumb-name">none</span>
            </div>
            <div v-for="bg in rootBgFiles" :key="bg.name"
                 class="icon-thumb bg-thumb" :class="{ selected: rootBg === bg.name }"
                 @click="assignRootBg(bg.name)">
              <span style="font-size:24px;margin:4px 0">🖼</span>
              <span class="icon-thumb-name" :title="bg.name">{{ bg.name }}</span>
              <span style="font-size:9px;color:var(--muted)">{{ fmtSize(bg.size) }}</span>
            </div>
          </div>

          <!-- Re-optimize -->
          <div style="font-weight:600;font-size:13px;margin:12px 0 6px">Re-optimize icons</div>
          <div style="display:flex;align-items:center;gap:10px;flex-wrap:wrap">
            <button type="button" class="btn btn-ghost btn-sm"
                    :disabled="reoptRootRunning" @click="reoptimizeRoot">
              {{ reoptRootRunning ? 'Optimizing…' : 'Re-optimize root' }}
            </button>
            <span class="text-muted" style="font-size:12px">
              {{ reoptRootDone ? `Done — ${reoptRootCount} icon(s) processed` : '' }}
            </span>
          </div>
          <div v-if="reoptRootRunning || reoptRootDone" class="progress-bar-wrap" style="margin-top:8px">
            <div class="progress-bar-fill" :style="{ width: (reoptRootProgress * 100).toFixed(0) + '%' }"></div>
          </div>
          <div class="hint-box" style="margin-top:8px">
            Rebuilds <em>_s/_m/_l</em> variants for icons used by root-level macros.
            Run after changing button size in Settings.
          </div>
        </div>
      </div>
    </div>
  </Teleport>

  <!-- ── Templates modal ──────────────────────────────────────────────────── -->
  <Teleport to="body">
    <div v-if="showTemplates" class="modal-backdrop" @click.self="showTemplates = false">
      <div class="modal">
        <div class="modal-header">
          <h3>Macro templates</h3>
          <button class="btn btn-ghost btn-sm" @click="showTemplates = false">✕</button>
        </div>
        <div class="tpl-filter-bar">
          <button type="button" class="tpl-filter-pill tpl-filter-ctrl" @click="selectAllCategories">All</button>
          <button type="button" class="tpl-filter-pill tpl-filter-ctrl" @click="deselectAllCategories">None</button>
          <button
            v-for="cat in MACRO_TEMPLATE_CATEGORIES" :key="cat" type="button"
            :class="['tpl-filter-pill', { active: activeCategories.has(cat) }]"
            @click="toggleCategory(cat)"
          >{{ cat }}</button>
        </div>
        <div class="modal-body">
          <template v-for="cat in MACRO_TEMPLATE_CATEGORIES" :key="cat">
            <template v-if="activeCategories.has(cat)">
            <div class="tpl-category">{{ cat }}</div>
            <div v-for="tpl in templatesByCategory(cat)" :key="tpl.name" class="tpl-card" @click="useTemplate(tpl)">
              <span class="tpl-icon">{{ tpl.icon }}</span>
              <div class="tpl-info">
                <strong>{{ tpl.name }}</strong>
                <span class="text-muted">{{ tpl.description }}</span>
              </div>
              <span class="tpl-arrow">→</span>
            </div>
            </template>
          </template>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import { useMacrosStore, type Macro } from '../stores/macros';
import { Api } from '../api/http';
import { initSettings } from '../api/init';
import { Ws } from '../api/wsApi';
import { reoptimizeIcons } from '../utils/icons';
import { MACRO_TEMPLATES, MACRO_TEMPLATE_CATEGORIES, type MacroTemplate } from '../utils/macroTemplates';

const store  = useMacrosStore();
const router = useRouter();
const showTemplates = ref(false);
const activeCategories = ref(new Set<string>(MACRO_TEMPLATE_CATEGORIES));

function toggleCategory(cat: string) {
  const s = activeCategories.value;
  if (s.has(cat)) s.delete(cat); else s.add(cat);
  activeCategories.value = new Set(s); // trigger reactivity
}
function selectAllCategories()   { activeCategories.value = new Set(MACRO_TEMPLATE_CATEGORIES); }
function deselectAllCategories() { activeCategories.value = new Set(); }

onMounted(() => { if (!store.items.length) store.load(); });

function templatesByCategory(cat: string) {
  return MACRO_TEMPLATES.filter(t => t.category === cat);
}

function useTemplate(tpl: MacroTemplate) {
  sessionStorage.setItem('macro_prefill', JSON.stringify(tpl.macro));
  showTemplates.value = false;
  router.push('/macros/new');
}

// Scene entries (type==="scene", no parent scene_id)
const sceneItems = computed(() =>
  store.items.filter(m => m.type === 'scene' && !m.scene_id)
);

// Root-level macros (no scene_id, not scene-type)
const rootMacros = computed(() =>
  store.items.filter(m => !m.scene_id && m.type !== 'scene')
);

// Macros belonging to a given scene
function macrosInScene(sceneId: string) {
  return store.items.filter(m => m.scene_id === sceneId && m.type !== 'scene');
}

async function onDelete(id: string) {
  if (!confirm('Delete this macro?')) return;
  await store.remove(id);
}

async function onDeleteScene(sceneId: string) {
  if (!confirm('Delete this scene? Its macros will be moved to Root.')) return;
  await store.removeScene(sceneId);
}

async function onAssign(id: string, sceneId: string) {
  await store.assignScene(id, sceneId);
}

// ── Root popup ────────────────────────────────────────────────────────────
type BgFile = { name: string; size: number };
const rootPopupOpen    = ref(false);
const rootBg           = ref('');
const rootBgFiles      = ref<BgFile[]>([]);
const reoptRootRunning  = ref(false);
const reoptRootProgress = ref(0);
const reoptRootDone     = ref(false);
const reoptRootCount    = ref(0);

function fmtSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

async function openRootPopup() {
  rootPopupOpen.value = true;
  try {
    const [files, map] = await Promise.all([Api.getBackgrounds(), Api.getSceneBgs()]);
    rootBgFiles.value = files;
    rootBg.value      = (map as Record<string, string>)[''] ?? '';
  } catch { /* no SD */ }
}

async function assignRootBg(name: string) {
  rootBg.value = name;
  try { await Ws.setSceneBg('', name); } catch { /* ignore */ }
}

async function reoptimizeRoot() {
  const rootMacros = store.items.filter((m: Macro) => !m.scene_id && m.type !== 'scene');
  const usedIcons  = [...new Set(rootMacros.map((m: Macro) => m.icon).filter(Boolean) as string[])];
  if (!usedIcons.length) { reoptRootDone.value = true; reoptRootCount.value = 0; return; }

  reoptRootRunning.value  = true;
  reoptRootProgress.value = 0;
  reoptRootDone.value     = false;

  const btnSize = initSettings.value?.btn_size ?? 70;

  let processed = 0;
  await reoptimizeIcons(usedIcons, btnSize, (done, total) => {
    processed = total;
    reoptRootProgress.value = total > 0 ? done / total : 1;
  });

  reoptRootProgress.value = 1;
  reoptRootRunning.value  = false;
  reoptRootDone.value     = true;
  reoptRootCount.value    = processed;
}
</script>

<style scoped>
/* ── tree sections ── */
.tree-section {
  border: 1px solid var(--border);
  border-radius: 6px;
  margin-bottom: 12px;
  overflow: hidden;
}

.tree-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  background: var(--surface2);
  border-bottom: 1px solid var(--border);
}
.tree-header-root { background: var(--surface2); }
.tree-header-scene { background: #1f252d; border-left: 3px solid #3d7ed4; }

.tree-header-title { font-weight: 600; font-size: 13px; flex: 1; }
.scene-icon { font-size: 15px; flex-shrink: 0; }

.tree-header-actions { display: flex; gap: 6px; margin-left: auto; }

.tree-table { border: none; margin: 0; }
.tree-table td { border-top: 1px solid var(--border); }
.tree-table tr:first-child td { border-top: none; }

.tree-empty {
  padding: 10px 14px;
  font-size: 12px;
  color: var(--muted);
}

/* ── sort buttons ── */
.order-cell { display: flex; gap: 2px; align-items: center; width: 56px; }
.sort-btn {
  background: var(--surface2);
  border: 1px solid var(--border);
  border-radius: 3px;
  padding: 2px 6px;
  font-size: 11px;
  cursor: pointer;
  color: var(--text);
  line-height: 1;
}
.sort-btn:hover:not(:disabled) { background: var(--border); }
.sort-btn:disabled { opacity: 0.3; cursor: not-allowed; }

/* ── table columns ── */
.type-cell { color: var(--muted); font-size: 12px; width: 100px; }
.icon-cell { color: var(--muted); font-size: 12px; width: 80px; }
.actions-cell { display: flex; gap: 6px; align-items: center; white-space: nowrap; }

/* ── modal ── */
.modal-backdrop {
  position: fixed; inset: 0; background: rgba(0,0,0,.55);
  display: flex; align-items: center; justify-content: center; z-index: 1000;
}
.modal {
  background: var(--surface); border: 1px solid var(--border); border-radius: 8px;
  width: min(480px, 96vw); max-height: 80vh; display: flex; flex-direction: column; overflow: hidden;
}
.modal-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 14px 16px 10px; border-bottom: 1px solid var(--border);
}
.modal-header h3 { margin: 0; font-size: 15px; }
.modal-body { overflow-y: auto; padding: 10px 12px 14px; display: flex; flex-direction: column; gap: 4px; }
.tpl-category {
  font-size: 10px; font-weight: 700; text-transform: uppercase; letter-spacing: .07em;
  color: var(--muted); padding: 10px 4px 4px;
}
.tpl-category:first-child { padding-top: 2px; }
.tpl-card {
  display: flex; align-items: center; gap: 12px; padding: 10px 12px;
  border: 1px solid var(--border); border-radius: 6px; cursor: pointer;
  transition: border-color .15s, background .15s;
}
.tpl-card:hover { border-color: var(--accent); background: var(--surface2); }
.tpl-icon { font-size: 20px; width: 28px; text-align: center; flex-shrink: 0; }
.tpl-info { flex: 1; display: flex; flex-direction: column; gap: 2px; font-size: 13px; }
.tpl-info .text-muted { font-size: 11px; }
.tpl-arrow { color: var(--muted); font-size: 16px; }

/* ── template filter pills ── */
.tpl-filter-bar {
  display: flex; flex-wrap: wrap; gap: 6px;
  padding: 10px 12px 6px; border-bottom: 1px solid var(--border);
}
.tpl-filter-pill {
  padding: 3px 10px; border: 1px solid var(--border); border-radius: 12px;
  font-size: 11px; cursor: pointer; background: var(--surface2); color: var(--muted);
  transition: all .15s;
}
.tpl-filter-pill.active {
  background: var(--accent); color: #fff; border-color: var(--accent);
}
.tpl-filter-ctrl {
  border-style: dashed;
  color: var(--muted);
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

/* ── BG gallery (root popup) ── */
.icon-gallery { display: flex; flex-wrap: wrap; gap: 6px; }
.icon-thumb {
  display: flex; flex-direction: column; align-items: center;
  border: 1px solid var(--border); border-radius: 5px;
  cursor: pointer; background: var(--surface2);
  transition: border-color .12s, background .12s; user-select: none;
}
.icon-thumb:hover   { border-color: var(--accent); background: var(--surface); }
.icon-thumb.selected { border-color: var(--accent); background: #1b3a5c; }
.icon-thumb-name { font-size: 9px; color: var(--muted); text-align: center; word-break: break-all; max-width: 100%; }
.bg-thumb { width: 72px; height: 80px; justify-content: space-between; padding: 4px; }
.progress-bar-wrap { height: 6px; background: var(--border,#ddd); border-radius: 3px; overflow: hidden; }
.progress-bar-fill { height: 100%; background: var(--accent,#2196f3); transition: width .1s linear; }

/* ── scene assign dropdown ── */
.scene-assign {
  font-size: 11px;
  padding: 2px 4px;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--surface);
  color: var(--text);
  cursor: pointer;
  max-width: 110px;
}
</style>
