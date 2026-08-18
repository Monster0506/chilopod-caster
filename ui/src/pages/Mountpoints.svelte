<script>
  import { apiGet, apiPost } from '../lib/api.js';
  import JsonTree from '../lib/JsonTree.svelte';
  import RevealSecret from '../lib/RevealSecret.svelte';
  import { pushToast } from '../lib/toast.js';
  import { ChevronDown, ChevronRight, Pencil, Plus, Trash2, X } from '@lucide/svelte';

  let table = $state(null);
  let net = $state(null);
  let auth = $state(null);
  let rtcm = $state(null);
  let expandedRtcm = $state(null);
  let selectedMessage = $state(null);
  let error = $state('');
  let autoRefresh = $state(true);
  let showForm = $state(false);
  let submitting = $state(false);
  let removing = $state(new Set());
  let detecting = $state(new Set());

  let editingMountpoint = $state(null);
  let editForm = $state({ group: '', identifier: '', auth_user: '', auth_password: '' });
  let savingEdit = $state(false);
  let editMsg = $state('');

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
      const [tables, connections, authEntries, rtcmInfo] = await Promise.all([
        apiGet('sourcetables'), apiGet('net'), apiGet('auth'), apiGet('rtcm'),
      ]);
      error = '';
      table = tables.find((t) => t.host === 'LOCAL') ?? { mountpoints: {} };
      net = connections;
      auth = authEntries;
      rtcm = rtcmInfo;
    } catch (e) {
      error = e.message;
    }
  }

  function rtcmFor(mountpoint) {
    return rtcm?.[mountpoint] ?? null;
  }

  function rtcmTypes(info) {
    if (!info?.types) return [];
    return info.types.split(',').filter(Boolean);
  }

  function toggleRtcmDetail(mountpoint) {
    expandedRtcm = expandedRtcm === mountpoint ? null : mountpoint;
  }

  function openMessageDetail(mountpoint, msgType) {
    const data = rtcmFor(mountpoint)?.sidecar?.last_messages?.[msgType] ?? null;
    selectedMessage = { mountpoint, msgType, data };
  }

  function closeMessageDetail() {
    selectedMessage = null;
  }

  function messageTypeName(msgType) {
    return rtcm?._type_names?.[msgType] ?? `RTCM ${msgType}`;
  }

  function sidecarConstellations(info) {
    const c = info?.sidecar?.constellations;
    if (!c) return [];
    return Object.entries(c).sort(([a], [b]) => a.localeCompare(b));
  }

  const CONSTELLATION_ORDER = [
    ['GPS', 'GPS'],
    ['GLONASS', 'GLO'],
    ['Galileo', 'GAL'],
    ['BeiDou', 'BDS'],
    ['QZSS', 'QZS'],
    ['SBAS', 'SBAS'],
  ];

  function navSystemDisplay(key, mnt) {
    const c = rtcmFor(key)?.sidecar?.constellations;
    if (c) {
      const parts = CONSTELLATION_ORDER
        .filter(([name]) => c[name] > 0)
        .map(([name, abbr]) => `${c[name]}${abbr}`);
      if (parts.length > 0) return parts.join('+');
    }
    return strField(mnt.str, 6);
  }

  function hasAntennaInfo(sidecar) {
    return !!(sidecar?.antenna_descriptor || sidecar?.receiver_type);
  }

  function formatAgo(dateStr) {
    if (!dateStr) return '-';
    const sec = Math.floor((Date.now() - new Date(dateStr).getTime()) / 1000);
    if (sec < 0) return 'just now';
    if (sec < 60) return sec + 's ago';
    if (sec < 3600) return Math.floor(sec / 60) + 'm ago';
    return Math.floor(sec / 3600) + 'h ago';
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
    if (rtt == null) return '-';
    return (rtt / 1000).toFixed(1) + ' ms';
  }

  async function submitForm() {
    if (!form.mountpoint || !form.source_password) {
      pushToast('Mountpoint and password are required.', 'error');
      return;
    }
    submitting = true;
    try {
      const res = await apiPost('sources', { ...form });
      if (res.result === 0) {
        pushToast(`Added ${form.mountpoint}.`);
        form = { ...blankForm };
        showForm = false;
        await fetchAll();
      } else {
        pushToast(res.error ?? 'Failed to add mountpoint.', 'error');
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      submitting = false;
    }
  }

  async function removeSource(mountpoint) {
    if (!confirm(`Remove ${mountpoint}? This deletes its config and drops any active connection.`)) return;
    removing = new Set([...removing, mountpoint]);
    try {
      const res = await apiPost('sources/remove', { mountpoint });
      if (res.result === 0) {
        pushToast(`Removed ${mountpoint}${res.dropped_connections ? ` (dropped ${res.dropped_connections} active connection)` : ''}.`);
      } else {
        pushToast(res.error ?? 'Failed to remove mountpoint.', 'error');
      }
      await fetchAll();
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      removing = new Set([...removing].filter((x) => x !== mountpoint));
    }
  }

  async function detectTypes(mountpoint) {
    detecting = new Set([...detecting, mountpoint]);
    try {
      const res = await apiPost('sources/detect', { mountpoint });
      const navNote = res.nav_system ? `, nav-system ${res.nav_system}` : '';
      const posNote = res.lat != null ? `, position ${res.lat.toFixed(6)}, ${res.lon.toFixed(6)}` : '';
      if (res.result === 0) {
        pushToast(`${mountpoint}: detected ${res.types}${navNote}${posNote}, updated.`);
      } else {
        pushToast(`${mountpoint}: ${res.error ?? 'detection failed.'}`, 'error');
      }
      if (res.result === 0) await fetchAll();
    } catch (e) {
      pushToast(`${mountpoint}: failed (${e.message}).`, 'error');
    } finally {
      detecting = new Set([...detecting].filter((x) => x !== mountpoint));
    }
  }

  function startEdit(mountpoint, mnt) {
    const existingAuth = authFor(mountpoint);
    editingMountpoint = mountpoint;
    editForm = {
      group: strField(mnt.str, 7),
      identifier: strField(mnt.str, 2),
      auth_user: existingAuth?.user ?? '',
      auth_password: existingAuth?.password ?? '',
    };
    editMsg = '';
  }

  function cancelEdit() {
    editingMountpoint = null;
  }

  async function saveEdit(mountpoint, mnt) {
    savingEdit = true;
    editMsg = '';
    try {
      const res = await apiPost('sources/edit', {
        mountpoint,
        group: editForm.group,
        identifier: editForm.identifier,
      });
      if (res.result !== 0) {
        editMsg = res.error ?? 'Failed to update mountpoint.';
        return;
      }
      if (!mnt.virtual && editForm.auth_user && editForm.auth_password) {
        const authRes = await apiPost('auth', {
          mountpoint,
          auth_user: editForm.auth_user,
          auth_password: editForm.auth_password,
        });
        if (authRes.error) {
          editMsg = authRes.error;
          return;
        }
      }
      editingMountpoint = null;
      await fetchAll();
    } catch (e) {
      editMsg = `Failed: ${e.message}`;
    } finally {
      savingEdit = false;
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
      <label>Group <input bind:value={form.network} /></label>
      <label>Country <input bind:value={form.country} /></label>
      <label>Latitude <input bind:value={form.lat} placeholder="41.5 (approximate is fine)" /></label>
      <label>Longitude <input bind:value={form.lon} placeholder="-81.5 (approximate is fine)" /></label>
      <label>Solution <input bind:value={form.solution} /></label>
      <label>Generator <input bind:value={form.generator} placeholder="defaults to unknown" /></label>
      <label>Bitrate <input bind:value={form.bitrate} /></label>
    </div>
    <div class="form-actions">
      <button type="submit" disabled={submitting}>{submitting ? 'Adding…' : 'Add mountpoint'}</button>
    </div>
  </form>
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
          <th>Group</th>
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
          <tr><td colspan="10" class="empty">No mountpoints configured.</td></tr>
        {/if}
        {#each entries as [key, mnt] (key)}
          {@const live = liveInfo(key)}
          {@const authEntry = authFor(key)}
          <tr>
            <td class="mono">{key}</td>
            <td>
              {#if editingMountpoint === key}
                <input class="edit-input" bind:value={editForm.group} placeholder="NONE" />
              {:else}
                {strField(mnt.str, 7)}
              {/if}
            </td>
            <td>
              {#if editingMountpoint === key}
                <input class="edit-input" bind:value={editForm.identifier} />
              {:else}
                {strField(mnt.str, 2)}
              {/if}
            </td>
            <td class="mono">{strField(mnt.str, 3)}</td>
            <td class="mono">{navSystemDisplay(key, mnt)}</td>
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
            <td class="mono">{mnt.virtual ? '-' : formatLatency(live)}</td>
            <td>
              {#if mnt.virtual}
                <span class="auth-na">n/a</span>
              {:else if editingMountpoint === key}
                <div class="auth-edit">
                  <input class="edit-input" bind:value={editForm.auth_user} placeholder="user" />
                  <input class="edit-input" bind:value={editForm.auth_password} placeholder="password" />
                </div>
              {:else if authEntry}
                <span class="mono">
                  {authEntry.user} /
                  <RevealSecret value={authEntry.password} />
                </span>
              {:else}
                <span class="auth-na">none set</span>
              {/if}
            </td>
            <td class="actions">
              {#if editingMountpoint === key}
                <button class="save-btn" onclick={() => saveEdit(key, mnt)} disabled={savingEdit}>
                  {savingEdit ? '…' : 'Save'}
                </button>
                <button class="cancel-btn" onclick={cancelEdit} disabled={savingEdit}>Cancel</button>
                {#if editMsg}<div class="auth-msg">{editMsg}</div>{/if}
              {:else}
                {#if !mnt.virtual}
                  <button
                    class="rtcm-btn"
                    onclick={() => toggleRtcmDetail(key)}
                    title={expandedRtcm === key ? 'Hide RTCM detail' : 'Show RTCM detail'}
                  >
                    {#if expandedRtcm === key}<ChevronDown size={14} />{:else}<ChevronRight size={14} />{/if}
                  </button>
                {/if}
                <button class="auth-btn" onclick={() => startEdit(key, mnt)} title="Edit mountpoint">
                  <Pencil size={14} />
                </button>
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
          {#if expandedRtcm === key}
            {@const info = rtcmFor(key)}
            {@const types = rtcmTypes(info)}
            <tr class="rtcm-detail-row">
              <td colspan="10">
                <div class="rtcm-detail">
                  <div class="rtcm-detail-section">
                    <h4>Message types seen</h4>
                    {#if types.length === 0}
                      <p class="rtcm-empty">No RTCM messages received yet.</p>
                    {:else}
                      <div class="rtcm-types">
                        {#each types as t (t)}
                          <button
                            type="button"
                            class="rtcm-type-badge rtcm-type-badge-btn"
                            onclick={() => openMessageDetail(key, t)}
                            title="Show decoded message"
                          >
                            {t}
                          </button>
                        {/each}
                      </div>
                    {/if}
                  </div>
                  {#if info?.pos}
                    <div class="rtcm-detail-section">
                      <h4>Station position (from RTCM 1005/1006)</h4>
                      <div class="rtcm-pos-grid">
                        <div><span class="rtcm-label">Lat</span> <span class="mono">{info.pos.lat.toFixed(7)}</span></div>
                        <div><span class="rtcm-label">Lon</span> <span class="mono">{info.pos.lon.toFixed(7)}</span></div>
                        <div><span class="rtcm-label">Alt</span> <span class="mono">{info.pos.alt.toFixed(3)} m</span></div>
                        <div><span class="rtcm-label">ECEF X</span> <span class="mono">{(info.pos.x / 10000).toFixed(4)} m</span></div>
                        <div><span class="rtcm-label">ECEF Y</span> <span class="mono">{(info.pos.y / 10000).toFixed(4)} m</span></div>
                        <div><span class="rtcm-label">ECEF Z</span> <span class="mono">{(info.pos.z / 10000).toFixed(4)} m</span></div>
                        <div><span class="rtcm-label">Updated</span> <span class="mono">{formatAgo(info.pos.date)}</span></div>
                      </div>
                    </div>
                  {/if}
                  {#if info?.sidecar}
                    {@const sc = info.sidecar}
                    {@const constellations = sidecarConstellations(info)}
                    <div class="rtcm-detail-section">
                      <h4>
                        Satellites
                        <span class="sidecar-status" class:sidecar-connected={sc.connected} class:sidecar-disconnected={!sc.connected}>
                          {sc.connected ? 'sidecar connected' : 'sidecar disconnected'}
                        </span>
                      </h4>
                      {#if constellations.length === 0}
                        <p class="rtcm-empty">No satellites decoded yet.</p>
                      {:else}
                        <div class="rtcm-types">
                          {#each constellations as [name, count] (name)}
                            <span class="rtcm-type-badge">{name}: {count}</span>
                          {/each}
                        </div>
                        <p class="sidecar-total mono">{sc.satellite_count} total</p>
                      {/if}
                      {#if sc.last_error}
                        <p class="sidecar-error">{sc.last_error}</p>
                      {/if}
                      <p class="sidecar-updated mono">Updated {formatAgo(sc.last_updated)}</p>
                    </div>
                    {#if hasAntennaInfo(sc)}
                      <div class="rtcm-detail-section">
                        <h4>Antenna / receiver (from RTCM 1008/1033)</h4>
                        <div class="rtcm-pos-grid">
                          {#if sc.antenna_descriptor}
                            <div><span class="rtcm-label">Antenna</span> <span class="mono">{sc.antenna_descriptor}</span></div>
                          {/if}
                          {#if sc.antenna_serial}
                            <div><span class="rtcm-label">Serial</span> <span class="mono">{sc.antenna_serial}</span></div>
                          {/if}
                          {#if sc.receiver_type}
                            <div><span class="rtcm-label">Receiver</span> <span class="mono">{sc.receiver_type}</span></div>
                          {/if}
                          {#if sc.firmware_version}
                            <div><span class="rtcm-label">Firmware</span> <span class="mono">{sc.firmware_version}</span></div>
                          {/if}
                          {#if sc.receiver_serial}
                            <div><span class="rtcm-label">Rcv serial</span> <span class="mono">{sc.receiver_serial}</span></div>
                          {/if}
                        </div>
                      </div>
                    {/if}
                  {/if}
                </div>
              </td>
            </tr>
          {/if}
        {/each}
      </tbody>
    </table>
  </div>
  <p class="count">{entries.length} mountpoint{entries.length === 1 ? '' : 's'}</p>
{/if}

<svelte:window onkeydown={(e) => { if (e.key === 'Escape') closeMessageDetail(); }} />

{#if selectedMessage}
  <div class="modal-overlay">
    <button type="button" class="modal-backdrop" onclick={closeMessageDetail} aria-label="Close"></button>
    <div class="modal" role="dialog" aria-modal="true" aria-label="Decoded RTCM message">
      <div class="modal-header">
        <h3>
          {selectedMessage.mountpoint}
          <span class="mono">- {messageTypeName(selectedMessage.msgType)} ({selectedMessage.msgType})</span>
        </h3>
        <button type="button" class="modal-close" onclick={closeMessageDetail} title="Close">
          <X size={16} />
        </button>
      </div>
      <div class="modal-body">
        {#if selectedMessage.data === null}
          <p class="rtcm-empty">
            No decoded message cached yet for this type. The sidecar may not be running, or hasn't
            decoded one since it started.
          </p>
        {:else}
          <JsonTree value={selectedMessage.data} />
        {/if}
      </div>
    </div>
  </div>
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
    color: var(--text-dim);
    cursor: pointer;
    user-select: none;
  }

  button {
    padding: 0.4rem 1rem;
    background: #1e3a5f;
    border: 1px solid var(--accent);
    border-radius: 5px;
    color: var(--accent-2);
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
    border-color: var(--good);
    color: #86efac;
  }

  .add-btn:hover:not(:disabled) {
    background: #14532d;
  }

  .add-form {
    background: var(--surface);
    border: 1px solid var(--border);
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
    color: var(--text-dim);
  }

  input {
    padding: 0.4rem 0.6rem;
    background: #12141c;
    border: 1px solid var(--border);
    border-radius: 5px;
    color: var(--text);
    font-size: 0.85rem;
  }

  input:focus {
    outline: none;
    border-color: var(--accent);
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
    color: var(--text-dim);
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
    color: var(--accent-2);
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
    border: 1px solid var(--good);
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
    border: 1px solid var(--border);
    border-radius: 4px;
    color: var(--text-muted);
    font-size: 0.78rem;
    cursor: pointer;
  }

  .cancel-btn:hover:not(:disabled) {
    background: #22263a;
  }

  .rtcm-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    padding: 0.3rem;
    background: transparent;
    border: 1px solid #1e3a5f;
    border-radius: 4px;
    color: var(--accent-2);
    font-size: 0.78rem;
    cursor: pointer;
    transition: background 120ms;
  }

  .rtcm-btn:hover:not(:disabled) {
    background: #1e3a5f33;
  }

  .rtcm-detail-row td {
    background: #14161f;
    padding: 1rem 1.25rem;
  }

  .rtcm-detail {
    display: flex;
    flex-wrap: wrap;
    gap: 2rem;
  }

  .rtcm-detail-section h4 {
    margin: 0 0 0.6rem;
    font-size: 0.78rem;
    font-weight: 600;
    color: var(--text-dim);
    text-transform: uppercase;
    letter-spacing: 0.03em;
  }

  .rtcm-empty {
    margin: 0;
    font-size: 0.82rem;
    color: var(--text-dim);
  }

  .rtcm-types {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
    max-width: 480px;
  }

  .rtcm-type-badge {
    padding: 0.15rem 0.5rem;
    border-radius: 3px;
    font-size: 0.75rem;
    font-family: monospace;
    background: #1e3a5f;
    color: var(--accent-2);
  }

  .rtcm-type-badge-btn {
    border: none;
    cursor: pointer;
    transition: background 120ms;
    -webkit-tap-highlight-color: transparent;
  }

  .rtcm-type-badge-btn:hover:not(:disabled) {
    background: var(--accent);
    color: #dbeafe;
  }

  .rtcm-pos-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
    gap: 0.5rem 1.5rem;
    font-size: 0.82rem;
  }

  .rtcm-label {
    display: inline-block;
    width: 60px;
    color: var(--text-dim);
  }

  .sidecar-status {
    margin-left: 0.5rem;
    padding: 0.1rem 0.4rem;
    border-radius: 3px;
    font-size: 0.68rem;
    font-weight: 500;
    text-transform: none;
    letter-spacing: normal;
  }

  .sidecar-connected {
    background: #1a3a1a;
    color: #86efac;
  }

  .sidecar-disconnected {
    background: #3a1a1a;
    color: #fca5a5;
  }

  .sidecar-total {
    margin: 0.5rem 0 0;
    font-size: 0.78rem;
    color: var(--text-dim);
  }

  .sidecar-error {
    margin: 0.4rem 0 0;
    font-size: 0.78rem;
    color: #fca5a5;
  }

  .sidecar-updated {
    margin: 0.3rem 0 0;
    font-size: 0.75rem;
    color: var(--text-dim);
  }

  .detect-btn {
    padding: 0.25rem 0.6rem;
    background: transparent;
    border: 1px solid #1e3a5f;
    border-radius: 4px;
    color: var(--accent-2);
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
    color: var(--text-dim);
    font-weight: 500;
    border-bottom: 1px solid var(--border);
    white-space: nowrap;
  }

  td {
    padding: 0.5rem 0.75rem;
    border-bottom: 1px solid #1e2130;
    color: var(--text-muted);
    vertical-align: middle;
  }

  tr:last-child td {
    border-bottom: none;
  }

  tr:hover td {
    background: var(--surface);
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
    color: var(--text-dim);
    padding: 2rem;
  }

  .count {
    margin: 0.75rem 0 0;
    font-size: 0.8rem;
    color: var(--text-dim);
  }

  .error {
    color: #fca5a5;
    font-size: 0.9rem;
  }

  .loading {
    color: var(--text-dim);
    font-size: 0.9rem;
  }

  .modal-overlay {
    position: fixed;
    inset: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 100;
  }

  .modal-backdrop {
    position: absolute;
    inset: 0;
    z-index: 0;
    padding: 0;
    background: rgba(8, 10, 16, 0.7);
    border: none;
    cursor: default;
    -webkit-tap-highlight-color: transparent;
  }

  .modal-backdrop:hover:not(:disabled) {
    background: rgba(8, 10, 16, 0.7);
  }

  .modal {
    position: relative;
    z-index: 1;
    width: min(640px, 90vw);
    max-height: 80vh;
    display: flex;
    flex-direction: column;
    background: #14161f;
    border: 1px solid var(--border);
    border-radius: 8px;
    box-shadow: 0 12px 40px rgba(0, 0, 0, 0.5);
  }

  .modal-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0.85rem 1.1rem;
    border-bottom: 1px solid var(--border);
  }

  .modal-header h3 {
    margin: 0;
    font-size: 0.95rem;
    color: var(--text);
    font-weight: 600;
  }

  .modal-close {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    padding: 0.3rem;
    background: transparent;
    border: 1px solid var(--border);
    border-radius: 4px;
    color: var(--text-muted);
    -webkit-tap-highlight-color: transparent;
  }

  .modal-close:hover:not(:disabled) {
    background: #22263a;
  }

  .modal-body {
    padding: 1rem 1.1rem;
    overflow: auto;
  }
</style>
