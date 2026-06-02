import { createRouter, createWebHistory } from 'vue-router';
import { loadBaseUrl } from './api/http';

// Static imports — all pages bundle into the main JS file.
// Eliminates ~20 lazy-chunk HTTP requests that fail when mDNS is unreliable.
// Total gzipped cost: ~45 KB (comparable to vendor.js at 39 KB).
import ConnectPage from './pages/ConnectPage.vue';
import DashboardPage from './pages/DashboardPage.vue';
import MacrosPage from './pages/MacrosPage.vue';
import MacroEditorPage from './pages/MacroEditorPage.vue';
import WidgetsPage from './pages/WidgetsPage.vue';
import WidgetEditorPage from './pages/WidgetEditorPage.vue';
import LogPage from './pages/LogPage.vue';
import WifiPage from './pages/WifiPage.vue';
import SettingsPage from './pages/SettingsPage.vue';
import ProfilesPage from './pages/ProfilesPage.vue';
import SceneEditorPage from './pages/SceneEditorPage.vue';

const routes = [
  { path: '/',           redirect: '/dashboard' },
  { path: '/connect',   component: ConnectPage },
  { path: '/dashboard', component: DashboardPage, meta: { requiresConnection: true } },
  { path: '/macros',    component: MacrosPage,     meta: { requiresConnection: true } },
  { path: '/macros/:id', component: MacroEditorPage, meta: { requiresConnection: true } },
  { path: '/widgets',  component: WidgetsPage,     meta: { requiresConnection: true } },
  { path: '/widgets/:id', component: WidgetEditorPage, meta: { requiresConnection: true } },
  { path: '/log',      component: LogPage,         meta: { requiresConnection: true } },
  { path: '/wifi',     component: WifiPage,        meta: { requiresConnection: true } },
  { path: '/settings', component: SettingsPage,    meta: { requiresConnection: true } },
  { path: '/profiles', component: ProfilesPage,    meta: { requiresConnection: true } },
  { path: '/scene/:context/:id', component: SceneEditorPage, meta: { requiresConnection: true } },
];

const router = createRouter({ history: createWebHistory(), routes });

router.beforeEach(to => {
  if (to.meta.requiresConnection && !loadBaseUrl()) {
    return '/connect';
  }
});

export default router;
