<template>
  <div class="page">
    <div class="page-header">
      <h2>Profiles</h2>
      <button class="btn" @click="onAdd">+ Add profile</button>
    </div>

    <p v-if="loading" class="text-muted">Loading…</p>
    <p v-else-if="error" class="text-danger">{{ error }}</p>

    <template v-else>
      <div v-if="!profiles.length" class="card text-muted" style="margin-top:16px">
        No profiles yet. Click <strong>+ Add profile</strong> to create one.
      </div>

      <div v-for="p in profiles" :key="p.id" class="profile-card" :class="{ active: p.id === activeId }">
        <div class="profile-badge" v-if="p.id === activeId">Active</div>

        <div class="profile-fields">
          <div class="field-row">
            <label class="field-label">Name</label>
            <input class="field-input" v-model="p.name" placeholder="Profile name" @change="onSave" />
          </div>

          <div class="field-row">
            <label class="field-label">Macro scene</label>
            <select class="field-select" v-model="p.macro_scene" @change="onSave">
              <option value="">Root (all)</option>
              <option v-for="s in macroScenes" :key="s.id" :value="s.id">{{ s.title }}</option>
            </select>
          </div>

          <div class="field-row">
            <label class="field-label">Widget scene</label>
            <select class="field-select" v-model="p.widget_scene" @change="onSave">
              <option value="">Root (all)</option>
              <option v-for="s in widgetScenes" :key="s.id" :value="s.id">{{ s.title }}</option>
            </select>
          </div>
        </div>

        <div class="profile-actions">
          <button
            class="btn btn-ghost btn-sm"
            :disabled="p.id === activeId"
            @click="onActivate(p.id)"
          >Set active</button>
          <button class="btn btn-danger btn-sm" @click="onDelete(p.id)">Delete</button>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { awaitInit, initProfiles, initActiveProfile } from '../api/init';
import { Ws } from '../api/wsApi';
import { useMacrosStore } from '../stores/macros';
import { useWidgetsStore } from '../stores/widgets';
import { generateId } from '../utils/id';

interface Profile {
  id: string;
  name: string;
  macro_scene: string;
  widget_scene: string;
}

const macrosStore = useMacrosStore();
const widgetsStore = useWidgetsStore();

const profiles = ref<Profile[]>([]);
const activeId = ref('');
const loading = ref(false);
const error = ref('');

const macroScenes = computed(() =>
  macrosStore.items.filter(m => m.type === 'scene' && !m.scene_id)
);

const widgetScenes = computed(() =>
  widgetsStore.items.filter(w => w.type === 'scene' && !w.scene_id)
);

onMounted(async () => {
  if (!macrosStore.items.length) macrosStore.load();
  if (!widgetsStore.items.length) widgetsStore.load();
  await load();
});

async function load() {
  loading.value = true;
  error.value = '';
  try {
    await awaitInit();
    profiles.value = initProfiles.value ?? [];
    activeId.value = initActiveProfile.value ?? '';
  } catch (e: any) {
    error.value = e.message ?? 'Load failed';
  } finally {
    loading.value = false;
  }
}

let _saveTimer: ReturnType<typeof setTimeout>;
function onSave() {
  clearTimeout(_saveTimer);
  _saveTimer = setTimeout(async () => {
    try {
      await Ws.saveProfiles(profiles.value);
    } catch (e: any) {
      error.value = e.message ?? 'Save failed';
    }
  }, 400);
}

function onAdd() {
  profiles.value.push({ id: generateId(), name: 'New profile', macro_scene: '', widget_scene: '' });
  onSave();
}

async function onDelete(id: string) {
  if (!confirm('Delete this profile?')) return;
  profiles.value = profiles.value.filter(p => p.id !== id);
  if (activeId.value === id) activeId.value = '';
  await onSave();
}

async function onActivate(id: string) {
  try {
    await Ws.setActiveProfile(id);
    activeId.value = id;
  } catch (e: any) {
    error.value = e.message ?? 'Failed to set active profile';
  }
}
</script>

<style scoped>
.profile-card {
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 16px;
  margin-bottom: 12px;
  position: relative;
  background: var(--surface);
  transition: border-color 0.15s;
}

.profile-card.active {
  border-color: #22c55e;
  background: color-mix(in srgb, #22c55e 6%, var(--surface));
}

.profile-badge {
  position: absolute;
  top: 12px;
  right: 12px;
  background: #22c55e;
  color: #fff;
  font-size: 11px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 10px;
}

.profile-fields {
  display: flex;
  flex-direction: column;
  gap: 10px;
  margin-bottom: 14px;
}

.field-row {
  display: flex;
  align-items: center;
  gap: 12px;
}

.field-label {
  width: 120px;
  font-size: 13px;
  color: var(--text-muted);
  flex-shrink: 0;
}

.field-input,
.field-select {
  flex: 1;
  height: 32px;
  padding: 0 8px;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--surface2);
  color: var(--text);
  font-size: 13px;
}

.profile-actions {
  display: flex;
  gap: 8px;
}
</style>
