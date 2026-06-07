// One-shot fix: strip orphaned profile_ids from macros (no profiles exist).
// Usage: node scripts/fix_orphan_profiles.mjs [host]
const host = process.argv[2] || 'cyd-dashboard.local';

const macros = await (await fetch(`http://${host}/api/macros`)).json();
const profs  = await (await fetch(`http://${host}/api/profiles`)).json();
const valid  = new Set((profs.list || []).map(p => p.id));

let touched = 0;
for (const m of macros) {
  if (Array.isArray(m.profile_ids)) {
    const kept = m.profile_ids.filter(id => valid.has(id));
    if (kept.length !== m.profile_ids.length) touched++;
    if (kept.length) m.profile_ids = kept; else delete m.profile_ids;
  }
}
console.log(`macros=${macros.length} valid_profiles=${valid.size} cleaned=${touched}`);
if (!touched) { console.log('nothing to fix'); process.exit(0); }

const ws = new WebSocket(`ws://${host}/ws`);
const REQ = 'fix-orphan-1';
ws.onopen = () => ws.send(JSON.stringify({ action: 'enter_service_mode' }));
ws.onmessage = (ev) => {
  let m; try { m = JSON.parse(ev.data); } catch { return; }
  if (m.action === 'service_mode_entered' || m.action === 'service_mode_changed') {
    ws.send(JSON.stringify({ action: 'save', what: 'macros', items: macros, req_id: REQ }));
  } else if (m.req_id === REQ) {
    console.log('save_ack:', JSON.stringify(m));
    ws.send(JSON.stringify({ action: 'exit_service_mode' }));
    setTimeout(() => { ws.close(); process.exit(0); }, 300);
  }
};
ws.onerror = (e) => { console.error('ws error', e.message || e); process.exit(1); };
setTimeout(() => { console.error('timeout'); process.exit(1); }, 12000);
