<script>
  import { apiGet, apiPost } from '../lib/api.js';

  let table = $state(null);
  let net = $state(null);
  let error = $state('');
  let autoRefresh = $state(true);
  let showForm = $state(false);
  let submitting = $state(false);
  let formMsg = $state('');
  let removing = $state(new Set());
  let removeMsg = $state('');

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
      const [tables, connections] = await Promise.all([apiGet('sourcetables'), apiGet('net')]);
      error = '';
      table = tables.find((t) => t.host === 'LOCAL') ?? { mountpoints: {} };
      net = connections;
    } catch (e) {
      error = e.message;
    }
  }

  function liveInfo(mountpoint) {
    if (!net) return null;
    return Object.values(net).find((c) => c.type === 'source' && c.mountpoint === mountpoint) ?? null;
  }

  function strField(str, i) {
    return str.split(';')[i] ?? '';
  }

  function formatBytes(n) {
    if (n == null) return '—';
    if (n < 1024) return n + ' B';
    if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
    return (n / 1048576).toFixed(1) + ' MB';
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
        formMsg = res.error ?? 'Failed to add source.';
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
        : (res.error ?? 'Failed to remove source.');
      await fetchAll();
    } catch (e) {
      removeMsg = `Failed: ${e.message}`;
    } finally {
      removing = new Set([...removing].filter((x) => x !== mountpoint));
    }
  }

  $effect(() => {
    fetchAll();
    if (!autoRefresh) return;
    const id = setInterval(fetchAll, 5000);
    return () => clearInterval(id);
  });
</script>

<div class="page">
  <div class="header">
    <h2>Sources</h2>
    <div class="controls">
      <label class="toggle">
        <input type="checkbox" bind:checked={autoRefresh} />
        Auto-refresh
      </label>
      <button onclick={fetchAll}>Refresh</button>
      <button class="add-btn" onclick={() => (showForm = !showForm)}>
        {showForm ? 'Cancel' : '+ Add Source'}
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
        <label>Latitude <input required bind:value={form.lat} placeholder="41.5" /></label>
        <label>Longitude <input required bind:value={form.lon} placeholder="-81.5" /></label>
        <label>Solution <input bind:value={form.solution} /></label>
        <label>Generator <input bind:value={form.generator} placeholder="defaults to unknown" /></label>
        <label>Bitrate <input bind:value={form.bitrate} /></label>
      </div>
      <div class="form-actions">
        <button type="submit" disabled={submitting}>{submitting ? 'Adding…' : 'Add source'}</button>
        {#if formMsg}<span class="form-msg">{formMsg}</span>{/if}
      </div>
      <p class="hint">
        New mountpoints only appear below once a source actually connects and pushes data --
        registering it here just makes the caster ready to accept the push.
      </p>
    </form>
  {/if}

  {#if removeMsg}
    <p class="remove-msg">{removeMsg}</p>
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
            <th>Received</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          {#if entries.length === 0}
            <tr><td colspan="8" class="empty">No sources configured.</td></tr>
          {/if}
          {#each entries as [key, mnt] (key)}
            {@const live = liveInfo(key)}
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
              <td class="mono">{live ? formatBytes(live.received_bytes) : '—'}</td>
              <td>
                <button
                  class="remove-btn"
                  onclick={() => removeSource(key)}
                  disabled={removing.has(key)}
                >
                  {removing.has(key) ? '…' : 'Remove'}
                </button>
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
    <p class="count">{entries.length} mountpoint{entries.length === 1 ? '' : 's'}</p>
  {/if}
</div>

<style>
  .page {
    padding: 2rem;
  }

  .header {
    display: flex;
    align-items: center;
    gap: 1.5rem;
    margin-bottom: 1.5rem;
  }

  h2 {
    margin: 0;
    color: #e2e8f0;
    font-size: 1.2rem;
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

  .remove-btn {
    padding: 0.25rem 0.6rem;
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
