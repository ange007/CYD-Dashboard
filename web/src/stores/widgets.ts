import { defineStore } from 'pinia';
import { ref } from 'vue';
import { awaitInit } from '../api/init';
import { Ws } from '../api/wsApi';

export interface Widget {
  id: string;
  title: string;
  icon?: string;
  type: 'text' | 'chart' | 'progress' | 'scene' | 'counter' | 'timer' | 'image';
  scene_id?: string;   // if set: this widget belongs to the scene with this id
  target_id?: string;  // if set (type=scene): navigates to this scene id; if empty: this IS the scene folder
  style?: 0 | 1 | 2;                       // progress: 0=horizontal 1=vertical 2=arc
  update?: number;                          // refresh interval, seconds (0=manual)
  data_target?: 'url' | 'url_system' | 'system';
  url?: string;
  metric_id?: string;
  parse_type?: 'regex' | 'json';           // how to extract data from the response
  // --- regex mode ---
  regex?: string;
  template?: string;
  // --- json mode ---
  json_keys?: string[];
  // --- display ---
  span?: 0 | 1 | 2 | 3;
  height?: number;
  bg_color?: number;
  bg_style?: 'solid' | 'gradient';
  // --- request headers ---
  headers?: Array<{ key: string; value: string }>;
  // --- counter ---
  step?: number;
  min?: number;
  max?: number;
  // --- timer ---
  duration?: number;   // seconds
  done_url?: string;
  // --- profile visibility ---
  profile_ids?: string[];  // empty/absent = visible in all profiles
  hide_root?: boolean;     // true = keep scene folder out of the root grid
  // --- image widget ---
  image_src?: string;
  // --- icon positioning ---
  icon_pos?: 'top' | 'bottom' | 'left' | 'right';
  // --- per-item opacity override ---
  bg_opa?: number;
}

export const useWidgetsStore = defineStore('widgets', () => {
  const items = ref<Widget[]>([]);
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
    await Ws.saveWidgets(items.value);
  }

  let _saveTimer: ReturnType<typeof setTimeout> | undefined;
  function debouncedSave(ms = 400) {
    clearTimeout(_saveTimer);
    _saveTimer = setTimeout(() => save().catch(() => {}), ms);
  }

  async function upsert(widget: Widget) {
    const idx = items.value.findIndex(w => w.id === widget.id);
    if (idx >= 0) items.value[idx] = widget;
    else items.value.push(widget);
    await save();
  }

  async function remove(id: string) {
    items.value = items.value.filter(w => w.id !== id);
    await save();
  }

  /** Delete a scene entry and move its widgets back to root. */
  async function removeScene(sceneId: string) {
    items.value = items.value
      .map(w => w.scene_id === sceneId ? { ...w, scene_id: undefined } : w)
      .filter(w => w.id !== sceneId);
    await save();
  }

  /** Move a widget up/down within its current scope (root or a scene). */
  async function moveInScene(id: string, dir: -1 | 1) {
    const widget = items.value.find(w => w.id === id);
    if (!widget) return;
    const scopeId = widget.scene_id ?? '';
    const scopeIds = items.value
      .filter(w => (w.scene_id ?? '') === scopeId && w.type !== 'scene')
      .map(w => w.id);
    const visIdx = scopeIds.indexOf(id);
    const targetVisIdx = visIdx + dir;
    if (visIdx < 0 || targetVisIdx < 0 || targetVisIdx >= scopeIds.length) return;
    const targetId = scopeIds[targetVisIdx];
    const i = items.value.findIndex(w => w.id === id);
    const j = items.value.findIndex(w => w.id === targetId);
    if (i < 0 || j < 0) return;
    const arr = [...items.value];
    [arr[i], arr[j]] = [arr[j], arr[i]];
    items.value = arr;
    await save();
  }

  /** Move a widget to a different scene (or to root if sceneId is empty). */
  function assignScene(id: string, sceneId: string) {
    const idx = items.value.findIndex(w => w.id === id);
    if (idx < 0) return;
    const updated = { ...items.value[idx] };
    if (sceneId) updated.scene_id = sceneId;
    else delete updated.scene_id;
    items.value[idx] = updated;
    debouncedSave();
  }

  async function move(id: string, dir: -1 | 1) {
    const i = items.value.findIndex(w => w.id === id);
    const j = i + dir;
    if (i < 0 || j < 0 || j >= items.value.length) return;
    const arr = [...items.value];
    [arr[i], arr[j]] = [arr[j], arr[i]];
    items.value = arr;
    await save();
  }

  /** Populate items externally (e.g. from /api/init) without an API call. */
  function setItems(data: Widget[]) { items.value = data; }

  /** Strip a deleted profile's id from every widget's profile_ids; save if changed. */
  async function removeProfileRef(profileId: string) {
    let changed = false;
    items.value = items.value.map(w => {
      if (!Array.isArray(w.profile_ids) || !w.profile_ids.includes(profileId)) return w;
      changed = true;
      const kept = w.profile_ids.filter(id => id !== profileId);
      const copy = { ...w };
      if (kept.length) copy.profile_ids = kept; else delete copy.profile_ids;
      return copy;
    });
    if (changed) await save();
  }

  return { items, loading, error, load, setItems, upsert, remove, removeScene, moveInScene, assignScene, move, removeProfileRef };
});
