<script>
  import { apiGet, apiPost } from '../lib/api.js';
  import { Pencil, Plus, Trash2, X } from '@lucide/svelte';

  let table = $state(null);
  let net = $state(null);
  let auth = $state(null);
  let error = $state('');
  let autoRefresh = $state(true);
  let showForm = $state(false);
  let submitting = $state(false);
  let formMsg = $state('');
  let removing = $state(new Set());
  let removeMsg = $state('');
  let detecting = $state(new Set());
  let detectMsg = $state('');

  let editingAuth = $state(null);
  let authForm = $state({ auth_user: '', auth_password: '' });
  let savingAuth = $state(false);
  let authMsg = $state('');

  const blankForm = {
    mountpoint: '',
    source_password: '',
    identifier: '',
    format: 'RTCM3',
    format_details: '',
    carrier: '2',
    nav_system: 'GPS+GLO',
    network: 'NONE',
    country: 'NONE',
    lat: '',
    lon: '',
    solution: '0',
    generator: '',
    bitrate: '0',
  };
  let form = $state({ ...blankForm });

  async function fetchAll() {
    try {
      const [tables, connections, authEntries] = await Promise.all([
        apiGet('sourcetables'), apiGet('net'), apiGet('auth'),
      ]);
      error = '';
      table = tables.find((t) => t.host === 'LOCAL') ?? { mountpoints: {} };
      net = connections;
      auth = authEntries;
    } catch (e) {
      error = e.message;
    }
  }

  function liveInfo(mountpoint) {
    if (!net) return null;
    return Object.values(net).find((c) => (c.type === 'source' || c.type === 'source_fetcher') && c.mountpoint === mountpoint) ?? null;
  }

  function authFor(mountpoint) {
    if (!auth) return null;
    return auth.find((a) => a.mountpoint === mountpoint) ?? null;
  }

  function strField(str, i) {
    return str.split(';')[i] ?? '';
  }

  function formatLatency(live) {
    const rtt = live?.tcp_info?.rtt;
    if (rtt == null) return '—';
    return (rtt / 1000).toFixed(1) + ' ms';
  }

  async function submitForm() {
    formMsg = '';
    if (!form.mountpoint || !form.source_password) {
      formMsg = 'Mountpoint and password are required.';
      return;
    }
    submitting = true;
    try {
      const res = await apiPost('sources', { ...form });
      if (res.result === 0) {
        formMsg = `Added ${form.mountpoint}.`;
        form = { ...blankForm };
        showForm = false;
        await fetchAll();
      } else {
        formMsg = res.error ?? 'Failed to add mountpoint.';
      }
    } catch (e) {
      formMsg = `Failed: ${e.message}`;
    } finally {
      submitting = false;
    }
  }

  async function removeSource(mountpoint) {
    if (!confirm(`Remove ${mountpoint}? This deletes its config and drops any active connection.`)) return;
    removing = new Set([...removing, mountpoint]);
    removeMsg = '';
    try {
      const res = await apiPost('sources/remove', { mountpoint });
      removeMsg = res.result === 0
        ? `Removed ${mountpoint}${res.dropped_connections ? ` (dropped ${res.dropped_connections} active connection)` : ''}.`
        : (res.error ?? 'Failed to remove mountpoint.');
      await fetchAll();
    } catch (e) {
      removeMsg = `Failed: ${e.message}`;
    } finally {
      removing = new Set([...removing].filter((x) => x !== mountpoint));
    }
  }

  async function detectTypes(mountpoint) {
    detecting = new Set([...detecting, mountpoint]);
    detectMsg = '';
    try {
      const res = await apiPost('sources/detect', { mountpoint });
      const navNote = res.nav_system ? `, nav-system ${res.nav_system}` : '';
      const posNote = res.lat != null ? `, position ${res.lat.toFixed(6)}, ${res.lon.toFixed(6)}` : '';
      detectMsg = res.result === 0
        ? `${mountpoint}: detected ${res.types}${navNote}${posNote}, updated.`
        : `${mountpoint}: ${res.error ?? 'detection failed.'}`;
      if (res.result === 0) await fetchAll();
    } catch (e) {
      detectMsg = `${mountpoint}: failed (${e.message}).`;
    } finally {
      detecting = new Set([...detecting].filter((x) => x !== mountpoint));
    }
  }

  function startAuthEdit(mountpoint) {
    const existing = authFor(mountpoint);
    editingAuth = mountpoint;
    authForm = { auth_user: existing?.user ?? '', auth_password: existing?.password ?? '' };
    authMsg = '';
  }

  function cancelAuthEdit() {
    editingAuth = null;
  }

  async function saveAuth(mountpoint) {
    if (!authForm.auth_user || !authForm.auth_password) {
      authMsg = 'User and password are required.';
      return;
    }
    savingAuth = true;
    authMsg = '';
    try {
      const res = await apiPost('auth', { mountpoint, ...authForm });
      if (res.error) {
        authMsg = res.error;
      } else {
        editingAuth = null;
        await fetchAll();
      }
    } catch (e) {
      authMsg = `Failed: ${e.message}`;
    } finally {
      savingAuth = false;
    }
  }

  $effect(() => {
    fetchAll();
    if (!autoRefresh) return;
    const id = setInterval(fetchAll, 5000);
    return () => clearInterval(id);
  });
</script>

<div class="header">
  <div class="controls">
    <label class="toggle">
      <input type="checkbox" bind:checked={autoRefresh} />
      Auto-refresh
    </label>
    <button onclick={fetchAll}>Refresh</button>
    <button class="add-btn" onclick={() => (showForm = !showForm)} title={showForm ? 'Cancel' : 'Add Mountpoint'}>
      {#if showForm}<X size={16} />{:else}<Plus size={16} />{/if}
    </button>
  </div>
</div>

{#if showForm}
  <form class="add-form" onsubmit={(e) => { e.preventDefault(); submitForm(); }}>
    <div class="grid">
      <label>Mountpoint <input required bind:value={form.mountpoint} placeholder="MYBASE" /></label>
      <label>Password <input required bind:value={form.source_password} placeholder="secret" /></label>
      <label>Identifier <input bind:value={form.identifier} placeholder="defaults to mountpoint" /></label>
      <label>Format <input bind:value={form.format} /></label>
      <label>Format details <input bind:value={form.format_details} placeholder="1004,1006,1012,1033" /></label>
      <label>Carrier <input bind:value={form.carrier} /></label>
      <label>Nav system <input bind:value={form.nav_system} /></label>
      <label>Network <input bind:value={form.network} /></label>
      <label>Country <input bind:value={form.country} /></label>
      <label>Latitude <input bind:value={form.lat} placeholder="41.5 (approximate is fine)" /></label>
      <label>Longitude <input bind:value={form.lon} placeholder="-81.5 (approximate is fine)" /></label>
      <label>Solution <input bind:value={form.solution} /></label>
      <label>Generator <input bind:value={form.generator} placeholder="defaults to unknown" /></label>
      <label>Bitrate <input bind:value={form.bitrate} /></label>
    </div>
    <div class="form-actions">
      <button type="submit" disabled={submitting}>{submitting ? 'Adding…' : 'Add mountpoint'}</button>
      {#if formMsg}<span class="form-msg">{formMsg}</span>{/if}
    </div>
    <p class="hint">
      New mountpoints only appear below once a source actually connects and pushes data --
      registering it here just makes the caster ready to accept the push. Format details and
      position don't need to be exact for RTCM3 sources -- an approximate lat/lon is fine,
      Format details can be left blank, and "Detect" (once connected) fetches both from what
      the caster actually decodes.
    </p>
  </form>
{/if}

{#if removeMsg}
  <p class="remove-msg">{removeMsg}</p>
{/if}
{#if detectMsg}
  <p class="remove-msg">{detectMsg}</p>
{/if}

{#if error}
  <p class="error">{error}</p>
{:else if !table}
  <p class="loading">Loading…</p>
{:else}
  {@const entries = Object.entries(table.mountpoints)}
  <div class="table-wrap">
    <table>
      <thead>
        <tr>
          <th>Mountpoint</th>
          <th>Identifier</th>
          <th>Format</th>
          <th>Nav system</th>
          <th>Position</th>
          <th>Status</th>
          <th>Latency</th>
          <th>Auth</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        {#if entries.length === 0}
          <tr><td colspan="9" class="empty">No mountpoints configured.</td></tr>
        {/if}
        {#each entries as [key, mnt] (key)}
          {@const live = liveInfo(key)}
          {@const authEntry = authFor(key)}
          <tr>
            <td class="mono">{key}</td>
            <td>{strField(mnt.str, 2)}</td>
            <td class="mono">{strField(mnt.str, 3)}</td>
            <td class="mono">{strField(mnt.str, 6)}</td>
            <td class="mono">{mnt.lat.toFixed(4)}, {mnt.lon.toFixed(4)}</td>
            <td>
              {#if mnt.virtual}
                <span class="badge badge-virtual">virtual</span>
              {:else if live}
                <span class="badge badge-connected">connected</span>
              {:else}
                <span class="badge badge-disconnected">no source</span>
              {/if}
            </td>
            <td class="mono">{mnt.virtual ? '—' : formatLatency(live)}</td>
            <td>
              {#if mnt.virtual}
                <span class="auth-na">n/a</span>
              {:else if editingAuth === key}
                <div class="auth-edit">
                  <input class="edit-input" bind:value={authForm.auth_user} placeholder="user" />
                  <input class="edit-input" bind:value={authForm.auth_password} placeholder="password" />
                </div>
                {#if authMsg}<div class="auth-msg">{authMsg}</div>{/if}
              {:else if authEntry}
                <span class="mono">{authEntry.user} / ••••••••</span>
              {:else}
                <span class="auth-na">none set</span>
              {/if}
            </td>
            <td class="actions">
              {#if !mnt.virtual && editingAuth === key}
                <button class="save-btn" onclick={() => saveAuth(key)} disabled={savingAuth}>
                  {savingAuth ? '…' : 'Save'}
                </button>
                <button class="cancel-btn" onclick={cancelAuthEdit} disabled={savingAuth}>Cancel</button>
              {:else}
                {#if !mnt.virtual}
                  <button class="auth-btn" onclick={() => startAuthEdit(key)} title={authEntry ? 'Edit' : 'Add'}>
                    {#if authEntry}<Pencil size={14} />{:else}<Plus size={14} />{/if}
                  </button>
                {/if}
                {#if live && strField(mnt.str, 3) === 'RTCM3'}
                  <button
                    class="detect-btn"
                    onclick={() => detectTypes(key)}
                    disabled={detecting.has(key)}
                    title="Fetch real decoded message types and station position from the live stream, and update this row to match"
                  >
                    {detecting.has(key) ? '…' : 'Detect'}
                  </button>
                {/if}
                <button
                  class="remove-btn"
                  onclick={() => removeSource(key)}
                  disabled={removing.has(key)}
                  title="Remove"
                >
                  {#if removing.has(key)}…{:else}<Trash2 size={14} />{/if}
                </button>
              {/if}
            </td>
          </tr>
        {/each}
      </tbody>
    </table>
  </div>
  <p class="count">{entries.length} mountpoint{entries.length === 1 ? '' : 's'}</p>
{/if}

<style>
  .header {
    display: flex;
    align-items: center;
    gap: 1.5rem;
    margin-bottom: 1.5rem;
  }

  .controls {
    display: flex;
    align-items: center;
    gap: 1rem;
  }

  .toggle {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.85rem;
    color: #64748b;
    cursor: pointer;
    user-select: none;
  }

  button {
    padding: 0.4rem 1rem;
    background: #1e3a5f;
    border: 1px solid #2563eb;
    border-radius: 5px;
    color: #93c5fd;
    font-size: 0.85rem;
    cursor: pointer;
    transition: background 120ms;
  }

  button:hover:not(:disabled) {
    background: #1e40af;
  }

  button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .add-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    padding: 0.4rem;
    background: #1a3a1a;
    border-color: #22c55e;
    color: #86efac;
  }

  .add-btn:hover:not(:disabled) {
    background: #14532d;
  }

  .add-form {
    background: #1a1d27;
    border: 1px solid #2a2d3a;
    border-radius: 8px;
    padding: 1.25rem;
    margin-bottom: 1.5rem;
  }

  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
    gap: 0.85rem;
  }

  label {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
    font-size: 0.78rem;
    color: #64748b;
  }

  input {
    padding: 0.4rem 0.6rem;
    background: #12141c;
    border: 1px solid #2a2d3a;
    border-radius: 5px;
    color: #e2e8f0;
    font-size: 0.85rem;
  }

  input:focus {
    outline: none;
    border-color: #2563eb;
  }

  .edit-input {
    width: 90px;
    padding: 0.3rem 0.45rem;
    font-family: monospace;
    font-size: 0.8rem;
  }

  .form-actions {
    display: flex;
    align-items: center;
    gap: 1rem;
    margin-top: 1rem;
  }

  .form-msg {
    font-size: 0.85rem;
    color: #94a3b8;
  }

  .hint {
    margin: 0.75rem 0 0;
    font-size: 0.78rem;
    color: #475569;
  }

  .remove-msg {
    margin: 0 0 1rem;
    font-size: 0.85rem;
    color: #64748b;
  }

  .actions {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    flex-wrap: wrap;
  }

  .auth-edit {
    display: flex;
    gap: 0.3rem;
  }

  .auth-msg {
    font-size: 0.72rem;
    color: #fca5a5;
    margin-top: 0.2rem;
  }

  .auth-na {
    color: #475569;
    font-size: 0.78rem;
  }

  .auth-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    padding: 0.3rem;
    background: transparent;
    border: 1px solid #1e3a5f;
    border-radius: 4px;
    color: #93c5fd;
    font-size: 0.78rem;
    cursor: pointer;
    transition: background 120ms;
  }

  .auth-btn:hover:not(:disabled) {
    background: #1e3a5f33;
  }

  .save-btn {
    padding: 0.25rem 0.6rem;
    background: #1a3a1a;
    border: 1px solid #22c55e;
    border-radius: 4px;
    color: #86efac;
    font-size: 0.78rem;
    cursor: pointer;
  }

  .save-btn:hover:not(:disabled) {
    background: #14532d;
  }

  .cancel-btn {
    padding: 0.25rem 0.6rem;
    background: transparent;
    border: 1px solid #2a2d3a;
    border-radius: 4px;
    color: #94a3b8;
    font-size: 0.78rem;
    cursor: pointer;
  }

  .cancel-btn:hover:not(:disabled) {
    background: #22263a;
  }

  .detect-btn {
    padding: 0.25rem 0.6rem;
    background: transparent;
    border: 1px solid #1e3a5f;
    border-radius: 4px;
    color: #93c5fd;
    font-size: 0.78rem;
    cursor: pointer;
    transition: background 120ms;
  }

  .detect-btn:hover:not(:disabled) {
    background: #1e3a5f33;
  }

  .detect-btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .remove-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    padding: 0.3rem;
    background: transparent;
    border: 1px solid #7f1d1d;
    border-radius: 4px;
    color: #fca5a5;
    font-size: 0.78rem;
    cursor: pointer;
    transition: background 120ms;
  }

  .remove-btn:hover:not(:disabled) {
    background: #7f1d1d33;
  }

  .remove-btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .table-wrap {
    overflow-x: auto;
  }

  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.85rem;
  }

  th {
    text-align: left;
    padding: 0.5rem 0.75rem;
    color: #475569;
    font-weight: 500;
    border-bottom: 1px solid #2a2d3a;
    white-space: nowrap;
  }

  td {
    padding: 0.5rem 0.75rem;
    border-bottom: 1px solid #1e2130;
    color: #94a3b8;
    vertical-align: middle;
  }

  tr:last-child td {
    border-bottom: none;
  }

  tr:hover td {
    background: #1a1d27;
  }

  .mono {
    font-family: monospace;
    font-size: 0.82rem;
  }

  .badge {
    padding: 0.15rem 0.5rem;
    border-radius: 3px;
    font-size: 0.75rem;
    font-weight: 500;
    white-space: nowrap;
  }

  .badge-connected {
    background: #1a3a1a;
    color: #86efac;
  }

  .badge-disconnected {
    background: #3a1a1a;
    color: #fca5a5;
  }

  .badge-virtual {
    background: #2a1a2d;
    color: #d8b4fe;
  }

  .empty {
    text-align: center;
    color: #475569;
    padding: 2rem;
  }

  .count {
    margin: 0.75rem 0 0;
    font-size: 0.8rem;
    color: #475569;
  }

  .error {
    color: #fca5a5;
    font-size: 0.9rem;
  }

  .loading {
    color: #475569;
    font-size: 0.9rem;
  }
</style>
