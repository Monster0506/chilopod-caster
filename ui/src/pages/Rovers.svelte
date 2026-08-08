<script>
  import { apiGet, apiPost } from '../lib/api.js';

  let data = $state(null);
  let error = $state('');
  let autoRefresh = $state(true);
  let dropping = $state(new Set());
  let dropMsg = $state('');

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
    if (!start) return '—';
    const sec = Math.floor((Date.now() - new Date(start).getTime()) / 1000);
    if (sec < 60) return sec + 's';
    if (sec < 3600) return Math.floor(sec / 60) + 'm ' + (sec % 60) + 's';
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    return h + 'h ' + m + 'm';
  }

  function formatLatency(conn) {
    const rtt = conn.tcp_info?.rtt;
    if (rtt == null) return '—';
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
            <td class="mono">{conn.mountpoint ?? '—'}</td>
            <td class="mono">{conn.assigned_base ?? '—'}</td>
            <td class="mono">{conn.auth_user ?? '—'}</td>
            <td class="mono">{formatDuration(conn.start)}</td>
            <td>
              <button
                class="drop-btn"
                onclick={() => drop(conn.id)}
                disabled={dropping.has(conn.id)}
              >
                {dropping.has(conn.id) ? '…' : 'Drop'}
              </button>
            </td>
          </tr>
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
