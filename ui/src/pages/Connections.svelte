<script>
  import { apiGet, apiPost } from '../lib/api.js';

  let data = $state(null);
  let error = $state('');
  let autoRefresh = $state(true);
  let typeFilter = $state('all');
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

  function formatBytes(n) {
    if (n == null) return '—';
    if (n < 1024) return n + ' B';
    if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
    return (n / 1048576).toFixed(1) + ' MB';
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

  function rows(net) {
    const all = Object.values(net);
    if (typeFilter === 'all') return all;
    return all.filter(c => c.type === typeFilter);
  }

  $effect(() => {
    fetchAll();
    if (!autoRefresh) return;
    const id = setInterval(fetchAll, 3000);
    return () => clearInterval(id);
  });
</script>

<div class="page">
  <div class="header">
    <h2>Connections</h2>
    <div class="controls">
      <select bind:value={typeFilter}>
        <option value="all">All types</option>
        <option value="client">Clients</option>
        <option value="source">Sources</option>
        <option value="source_fetcher">Fetchers</option>
        <option value="adm">Admin</option>
      </select>
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
    {@const list = rows(data)}
    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>ID</th>
            <th>Type</th>
            <th>IP</th>
            <th>Mountpoint</th>
            <th>User Agent</th>
            <th>Received</th>
            <th>Sent</th>
            <th>Connected</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          {#if list.length === 0}
            <tr><td colspan="9" class="empty">No connections.</td></tr>
          {/if}
          {#each list as conn (conn.id)}
            <tr>
              <td class="mono">{conn.id}</td>
              <td><span class="badge badge-{conn.type}">{conn.type}</span></td>
              <td class="mono">{conn.ip}</td>
              <td class="mono">{conn.mountpoint ?? '—'}</td>
              <td class="ua" title={conn.user_agent}>{conn.user_agent ?? '—'}</td>
              <td class="mono">{formatBytes(conn.received_bytes)}</td>
              <td class="mono">{formatBytes(conn.sent_bytes)}</td>
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
    <p class="count">{list.length} connection{list.length === 1 ? '' : 's'}</p>
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

  select {
    padding: 0.35rem 0.6rem;
    background: #1a1d27;
    border: 1px solid #2a2d3a;
    border-radius: 5px;
    color: #94a3b8;
    font-size: 0.85rem;
    cursor: pointer;
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

  .ua {
    max-width: 180px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .badge {
    padding: 0.15rem 0.5rem;
    border-radius: 3px;
    font-size: 0.75rem;
    font-weight: 500;
    white-space: nowrap;
  }

  .badge-client {
    background: #1e3a5f;
    color: #93c5fd;
  }

  .badge-source {
    background: #1a3a1a;
    color: #86efac;
  }

  .badge-source_fetcher {
    background: #2d2a1a;
    color: #fde68a;
  }

  .badge-adm {
    background: #2a1a2d;
    color: #d8b4fe;
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
