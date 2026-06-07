import { defineStore } from 'pinia';
import { ref } from 'vue';
import { awaitInit } from '../api/init';
import { Ws } from '../api/wsApi';

export interface MultiAction {
  type: 'keys' | 'text' | 'pause' | 'url';
  value?: string;  // key string, text, or URL
  ms?: number;     // pause duration in ms
}

export interface Macro {
  id: string;
  title: string;
  type: 'keys' | 'url' | 'command' | 'action' | 'scene' | 'toggle' | 'multi';
  bg_color?: number;
  // Font icon
  icon?: string;
  icon_size?: 's' | 'm' | 'l';
  icon_color?: number;
  // Custom image (.bin on SD)
  image?: string;
  image_size?: 's' | 'm' | 'l';
  // Scene / navigation
  scene_id?: string;
  target_id?: string;
  profile_ids?: string[];
  hide_root?: boolean;
  // Appearance overrides
  radius?: number;
  bg_opa?: number;
  // Action payloads
  keys?: string[];
  cmd?: string;
  action_id?: string;
  url?: string;
  // Toggle
  on_url?: string;
  off_url?: string;
  on_color?: number;
  off_color?: number;
  // Multi-action
  actions?: MultiAction[];
}

export const useMacrosStore = defineStore('macros', () => {
  const items = ref<Macro[]>([]);
  const loading = ref(false);
  const error = ref('');

  async function load() {
    loading.value = true;
    error.value = '';
    try {
      await awaitInit();
    } catch (e: any) {
      error.value = e.message ?? 'Load failed';
    } finally {
      loading.value = false;
    }
  }

  async function save() {
    await Ws.saveMacros(items.value);
  }

  let _saveTimer: ReturnType<typeof setTimeout> | undefined;
  function debouncedSave(ms = 400) {
    clearTimeout(_saveTimer);
    _saveTimer = setTimeout(() => save().catch(() => {}), ms);
  }

  async function upsert(macro: Macro) {
    const idx = items.value.findIndex(m => m.id === macro.id);
    if (idx >= 0) items.value[idx] = macro;
    else items.value.push(macro);
    await save();
  }

  async function remove(id: string) {
    items.value = items.value.filter(m => m.id !== id);
    await save();
  }

  /** Delete a scene entry and move its macros back to root. */
  async function removeScene(sceneId: string) {
    items.value = items.value
      .map(m => m.scene_id === sceneId ? { ...m, scene_id: undefined } : m)
      .filter(m => m.id !== sceneId);
    await save();
  }

  /** Move a macro up/down within its current scope (root or a scene). */
  async function moveInScene(id: string, dir: -1 | 1) {
    const macro = items.value.find(m => m.id === id);
    if (!macro) return;
    const scopeId = macro.scene_id ?? '';
    // Collect ids of items in the same scope, excluding scene-type entries
    const scopeIds = items.value
      .filter(m => (m.scene_id ?? '') === scopeId && m.type !== 'scene')
      .map(m => m.id);
    const visIdx = scopeIds.indexOf(id);
    const targetVisIdx = visIdx + dir;
    if (visIdx < 0 || targetVisIdx < 0 || targetVisIdx >= scopeIds.length) return;
    const targetId = scopeIds[targetVisIdx];
    const i = items.value.findIndex(m => m.id === id);
    const j = items.value.findIndex(m => m.id === targetId);
    if (i < 0 || j < 0) return;
    const arr = [...items.value];
    [arr[i], arr[j]] = [arr[j], arr[i]];
    items.value = arr;
    await save();
  }

  /** Move a macro to a different scene (or to root if sceneId is empty). */
  function assignScene(id: string, sceneId: string) {
    const idx = items.value.findIndex(m => m.id === id);
    if (idx < 0) return;
    const updated = { ...items.value[idx] };
    if (sceneId) updated.scene_id = sceneId;
    else delete updated.scene_id;
    items.value[idx] = updated;
    debouncedSave();
  }

  async function move(id: string, dir: -1 | 1) {
    const i = items.value.findIndex(m => m.id === id);
    const j = i + dir;
    if (i < 0 || j < 0 || j >= items.value.length) return;
    const arr = [...items.value];
    [arr[i], arr[j]] = [arr[j], arr[i]];
    items.value = arr;
    await save();
  }

  /** Populate items externally (e.g. from /api/init) without an API call. */
  function setItems(data: Macro[]) { items.value = data; }

  /** Strip a deleted profile's id from every macro's profile_ids; save if changed. */
  async function removeProfileRef(profileId: string) {
    let changed = false;
    items.value = items.value.map(m => {
      if (!Array.isArray(m.profile_ids) || !m.profile_ids.includes(profileId)) return m;
      changed = true;
      const kept = m.profile_ids.filter(id => id !== profileId);
      const copy = { ...m };
      if (kept.length) copy.profile_ids = kept; else delete copy.profile_ids;
      return copy;
    });
    if (changed) await save();
  }

  return { items, loading, error, load, setItems, upsert, remove, removeScene, moveInScene, assignScene, move, removeProfileRef };
});
