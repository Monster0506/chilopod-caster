<script>
  import { apiGet, apiPost } from '../lib/api.js';

  let data = $state(null);
  let error = $state('');
  let reloading = $state(false);
  let reloadMsg = $state('');

  async function fetchAll() {
    try {
      const [net, livesources, mem, nodes] = await Promise.all([
        apiGet('net'),
        apiGet('livesources'),
        apiGet('mem'),
        apiGet('nodes'),
      ]);
      error = '';
      data = { net, livesources, mem, nodes };
    } catch (e) {
      error = e.message;
    }
  }

  // Derived counts
  function connCounts(net) {
    const conns = Object.values(net);
    return {
      total: conns.length,
      clients:  conns.filter(c => c.type === 'client').length,
      sources:  conns.filter(c => c.type === 'source' || c.type === 'source_fetcher').length,
    };
  }

  function liveSourceCount(ls) {
    return Object.keys(ls.LOCAL ?? {}).length;
  }

  function memAllocated(mem) {
    const bytes = mem?.jemalloc?.stats?.allocated;
    if (bytes == null) return '—';
    return (bytes / 1024 / 1024).toFixed(1) + ' MB';
  }

  function nodeCount(nodes) {
    return Object.keys(nodes).length;
  }

  async function reload() {
    reloading = true;
    reloadMsg = '';
    try {
      const r = await apiPost('reload');
      reloadMsg = r.result === 0 ? 'Config reloaded.' : 'Reload returned ' + r.result;
    } catch (e) {
      reloadMsg = 'Reload failed: ' + e.message;
    } finally {
      reloading = false;
    }
  }

  // Initial fetch + 5s interval
  $effect(() => {
    fetchAll();
    const id = setInterval(fetchAll, 5000);
    return () => clearInterval(id);
  });
</script>

<div class="page">
  <div class="header">
    <h2>Dashboard</h2>
    <div class="reload-wrap">
      <button onclick={reload} disabled={reloading}>
        {reloading ? 'Reloading…' : 'Reload Config'}
      </button>
      {#if reloadMsg}<span class="reload-msg">{reloadMsg}</span>{/if}
    </div>
  </div>

  {#if error}
    <p class="error">{error}</p>
  {:else if !data}
    <p class="loading">Loading…</p>
  {:else}
    {@const cc = connCounts(data.net)}
    <div class="cards">
      <div class="card">
        <div class="card-value">{cc.total}</div>
        <div class="card-label">Connections</div>
        <div class="card-sub">{cc.clients} clients · {cc.sources} sources</div>
      </div>

      <div class="card">
        <div class="card-value">{liveSourceCount(data.livesources)}</div>
        <div class="card-label">Live Sources</div>
        <div class="card-sub">local mountpoints active</div>
      </div>

      <div class="card">
        <div class="card-value">{memAllocated(data.mem)}</div>
        <div class="card-label">Memory</div>
        <div class="card-sub">jemalloc allocated</div>
      </div>

      <div class="card">
        <div class="card-value">{nodeCount(data.nodes)}</div>
        <div class="card-label">Nodes</div>
        <div class="card-sub">cluster peers</div>
      </div>
    </div>
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
    margin-bottom: 2rem;
  }

  h2 {
    margin: 0;
    color: #e2e8f0;
    font-size: 1.2rem;
  }

  .reload-wrap {
    display: flex;
    align-items: center;
    gap: 1rem;
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

  .reload-msg {
    font-size: 0.85rem;
    color: #64748b;
  }

  .cards {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 1rem;
  }

  .card {
    background: #1a1d27;
    border: 1px solid #2a2d3a;
    border-radius: 8px;
    padding: 1.5rem;
  }

  .card-value {
    font-size: 2.2rem;
    font-weight: 700;
    color: #e2e8f0;
    line-height: 1;
    margin-bottom: 0.4rem;
  }

  .card-label {
    font-size: 0.9rem;
    color: #94a3b8;
    margin-bottom: 0.3rem;
  }

  .card-sub {
    font-size: 0.75rem;
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
