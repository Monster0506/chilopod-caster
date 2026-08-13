<script>
  import { apiGet, apiPost } from '../lib/api.js';
  import { ChevronDown, ChevronRight } from '@lucide/svelte';

  let data = $state(null);
  let error = $state('');
  let autoRefresh = $state(true);
  let dropping = $state(new Set());
  let dropMsg = $state('');
  let expandedGga = $state(null);

  const GGA_QUALITY = {
    0: 'Invalid', 1: 'GPS (Standalone)', 2: 'DGPS', 3: 'PPS',
    4: 'RTK Fixed', 5: 'RTK Float', 6: 'Estimated', 7: 'Manual', 8: 'Simulation',
  };

  function toggleGga(id) {
    expandedGga = expandedGga === id ? null : id;
  }

  function formatDist(m) {
    if (m == null) return '-';
    if (m >= 1000) return (m / 1000).toFixed(2) + ' km';
    return m.toFixed(1) + ' m';
  }

  function formatAgo(dateStr) {
    if (!dateStr) return '-';
    const sec = Math.floor((Date.now() - new Date(dateStr).getTime()) / 1000);
    if (sec < 0) return 'just now';
    if (sec < 60) return sec + 's ago';
    if (sec < 3600) return Math.floor(sec / 60) + 'm ago';
    return Math.floor(sec / 3600) + 'h ago';
  }

  async function fetchAll() {
    try {
      const net = await apiGet('net');
      error = '';
      data = net;
    } catch (e) {
      error = e.message;
    }
  }

  async function drop(id) {
    dropping = new Set([...dropping, id]);
    dropMsg = '';
    try {
      await apiPost('drop', { id });
      dropMsg = `Connection ${id} dropped.`;
      await fetchAll();
    } catch (e) {
      dropMsg = `Drop failed: ${e.message}`;
    } finally {
      dropping = new Set([...dropping].filter(x => x !== id));
    }
  }

  function formatDuration(start) {
    if (!start) return '-';
    const sec = Math.floor((Date.now() - new Date(start).getTime()) / 1000);
    if (sec < 60) return sec + 's';
    if (sec < 3600) return Math.floor(sec / 60) + 'm ' + (sec % 60) + 's';
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    return h + 'h ' + m + 'm';
  }

  function formatLatency(conn) {
    const rtt = conn.tcp_info?.rtt;
    if (rtt == null) return '-';
    return (rtt / 1000).toFixed(1) + ' ms';
  }

  function rovers(net) {
    return Object.values(net).filter(c => c.type === 'client');
  }

  $effect(() => {
    fetchAll();
    if (!autoRefresh) return;
    const id = setInterval(fetchAll, 3000);
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
  </div>
</div>

{#if dropMsg}
  <p class="drop-msg">{dropMsg}</p>
{/if}

{#if error}
  <p class="error">{error}</p>
{:else if !data}
  <p class="loading">Loading…</p>
{:else}
  {@const list = rovers(data)}
  <div class="table-wrap">
    <table>
      <thead>
        <tr>
          <th>ID</th>
          <th>IP</th>
          <th>Latency</th>
          <th>Requested</th>
          <th>Assigned base</th>
          <th>User</th>
          <th>Connected</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        {#if list.length === 0}
          <tr><td colspan="8" class="empty">No rovers connected.</td></tr>
        {/if}
        {#each list as conn (conn.id)}
          <tr>
            <td class="mono">{conn.id}</td>
            <td class="mono">{conn.ip}</td>
            <td class="mono">{formatLatency(conn)}</td>
            <td class="mono">{conn.mountpoint ?? '-'}</td>
            <td class="mono">{conn.assigned_base ?? '-'}</td>
            <td class="mono">{conn.auth_user ?? '-'}</td>
            <td class="mono">{formatDuration(conn.start)}</td>
            <td class="actions">
              {#if conn.gga}
                <button
                  class="gga-btn"
                  onclick={() => toggleGga(conn.id)}
                  title={expandedGga === conn.id ? 'Hide GGA detail' : 'Show GGA detail'}
                >
                  {#if expandedGga === conn.id}<ChevronDown size={14} />{:else}<ChevronRight size={14} />{/if}
                </button>
              {/if}
              <button
                class="drop-btn"
                onclick={() => drop(conn.id)}
                disabled={dropping.has(conn.id)}
              >
                {dropping.has(conn.id) ? '…' : 'Drop'}
              </button>
            </td>
          </tr>
          {#if expandedGga === conn.id && conn.gga}
            {@const gga = conn.gga}
            <tr class="gga-detail-row">
              <td colspan="8">
                <div class="gga-detail">
                  <div class="gga-detail-section">
                    <h4>GGA fix</h4>
                    <div class="gga-grid">
                      <div><span class="gga-label">Position</span> <span class="mono">{conn.lat.toFixed(7)}, {conn.lon.toFixed(7)}</span></div>
                      {#if conn.dist_to_base_m != null}
                        <div><span class="gga-label">Dist to base</span> <span class="mono">{formatDist(conn.dist_to_base_m)}</span></div>
                      {/if}
                      <div><span class="gga-label">Quality</span> <span class="mono">{GGA_QUALITY[gga.quality] ?? gga.quality}</span></div>
                      {#if gga.nsats != null}
                        <div><span class="gga-label">Sats</span> <span class="mono">{gga.nsats}</span></div>
                      {/if}
                      {#if gga.hdop != null}
                        <div><span class="gga-label">HDOP</span> <span class="mono">{gga.hdop.toFixed(2)}</span></div>
                      {/if}
                      <div><span class="gga-label">Alt</span> <span class="mono">{gga.alt.toFixed(1)} m</span></div>
                      <div><span class="gga-label">Geoid sep</span> <span class="mono">{gga.geoid_sep.toFixed(1)} m</span></div>
                      {#if gga.diff_age != null}
                        <div><span class="gga-label">Diff age</span> <span class="mono">{gga.diff_age.toFixed(1)} s</span></div>
                      {/if}
                      {#if gga.diff_station != null}
                        <div><span class="gga-label">Diff station</span> <span class="mono">{gga.diff_station}</span></div>
                      {/if}
                      <div><span class="gga-label">Fix time</span> <span class="mono">{gga.time || '-'}</span></div>
                      <div><span class="gga-label">Received</span> <span class="mono">{formatAgo(gga.date)}</span></div>
                    </div>
                  </div>
                </div>
              </td>
            </tr>
          {/if}
        {/each}
      </tbody>
    </table>
  </div>
  <p class="count">{list.length} rover{list.length === 1 ? '' : 's'}</p>
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

  .drop-msg {
    margin: 0 0 1rem;
    font-size: 0.85rem;
    color: #64748b;
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

  .drop-btn {
    padding: 0.25rem 0.6rem;
    background: transparent;
    border: 1px solid #7f1d1d;
    border-radius: 4px;
    color: #fca5a5;
    font-size: 0.78rem;
    cursor: pointer;
    transition: background 120ms;
  }

  .drop-btn:hover:not(:disabled) {
    background: #7f1d1d33;
  }

  .drop-btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .actions {
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }

  .gga-btn {
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

  .gga-btn:hover:not(:disabled) {
    background: #1e3a5f33;
  }

  .gga-detail-row td {
    background: #14161f;
    padding: 1rem 1.25rem;
  }

  .gga-detail {
    display: flex;
    flex-wrap: wrap;
    gap: 2rem;
  }

  .gga-detail-section h4 {
    margin: 0 0 0.6rem;
    font-size: 0.78rem;
    font-weight: 600;
    color: #64748b;
    text-transform: uppercase;
    letter-spacing: 0.03em;
  }

  .gga-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
    gap: 0.5rem 1.5rem;
    font-size: 0.82rem;
  }

  .gga-label {
    display: inline-block;
    width: 90px;
    color: #64748b;
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
