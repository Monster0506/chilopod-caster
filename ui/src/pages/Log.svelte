<script>
  import { apiGet } from '../lib/api.js';

  const LEVEL_NAMES = ['EMERG', 'ALERT', 'CRIT', 'ERR', 'WARNING', 'NOTICE', 'INFO', 'DEBUG', 'EDEBUG'];
  const DEFAULT_LEVELS = ['EMERG', 'ALERT', 'CRIT', 'ERR', 'WARNING', 'NOTICE', 'INFO'];

  let entries = $state([]);
  let error = $state('');
  let autoRefresh = $state(true);
  let selectedLevels = $state(new Set(DEFAULT_LEVELS));
  let filterOpen = $state(false);
  let filterEl;
  let search = $state('');
  let paused = $state(false);
  let scrollEl;

  let lastId = -1;

  function levelName(level) {
    return LEVEL_NAMES[level] ?? 'UNKNOWN';
  }

  async function fetchLog() {
    try {
      const suffix = lastId >= 0 ? `?since=${lastId}` : '';
      const res = await apiGet(`log${suffix}`);
      error = '';
      if (res.length) {
        entries = [...entries, ...res].slice(-1000);
        lastId = res[res.length - 1].id;
      }
    } catch (e) {
      error = e.message;
    }
  }

  function toggleLevel(name) {
    const next = new Set(selectedLevels);
    if (next.has(name)) next.delete(name);
    else next.add(name);
    selectedLevels = next;
  }

  function clearLog() {
    entries = [];
  }

  function onScroll() {
    if (!scrollEl) return;
    const atBottom = scrollEl.scrollHeight - scrollEl.scrollTop - scrollEl.clientHeight < 40;
    paused = !atBottom;
  }

  function formatTime(ts) {
    return new Date(ts).toTimeString().slice(0, 8) + '.' + new Date(ts).getMilliseconds().toString().padStart(3, '0');
  }

  let list = $derived.by(() => {
    const q = search.trim().toLowerCase();
    return entries.filter(e => selectedLevels.has(levelName(e.level)) && (!q || e.message.toLowerCase().includes(q)));
  });

  $effect(() => {
    fetchLog();
    if (!autoRefresh) return;
    const id = setInterval(fetchLog, 1500);
    return () => clearInterval(id);
  });

  $effect(() => {
    list;
    if (!paused && scrollEl) {
      const el = scrollEl;
      requestAnimationFrame(() => { el.scrollTop = el.scrollHeight; });
    }
  });

  $effect(() => {
    if (!filterOpen) return;
    function onDocClick(e) {
      if (filterEl && !filterEl.contains(e.target)) filterOpen = false;
    }
    document.addEventListener('click', onDocClick);
    return () => document.removeEventListener('click', onDocClick);
  });
</script>

<div class="page">
  <div class="header">
    <h2>Log</h2>
    <div class="controls">
      <div class="type-filter" bind:this={filterEl}>
        <button type="button" class="filter-toggle" onclick={() => (filterOpen = !filterOpen)}>
          Levels ({selectedLevels.size}) <span class="caret">▾</span>
        </button>
        {#if filterOpen}
          <div class="filter-panel">
            {#each LEVEL_NAMES as name (name)}
              <label class="filter-option">
                <input
                  type="checkbox"
                  checked={selectedLevels.has(name)}
                  onchange={() => toggleLevel(name)}
                />
                <span class="lvl-dot lvl-{name}"></span>
                {name}
              </label>
            {/each}
          </div>
        {/if}
      </div>
      <input class="search-input" type="text" placeholder="Filter message text…" bind:value={search} />
      <label class="toggle">
        <input type="checkbox" bind:checked={autoRefresh} />
        Auto-refresh
      </label>
      <button class="clear-btn" onclick={clearLog}>Clear</button>
    </div>
  </div>

  {#if error}
    <p class="error">{error}</p>
  {/if}

  <div class="log-panel">
    <div class="log-scroll" bind:this={scrollEl} onscroll={onScroll}>
      {#each list as e (e.id)}
        <div class="log-line lvl-row-{levelName(e.level)}">
          <span class="lg-time">{formatTime(e.timestamp)}</span>
          <span class="lg-level lvl-{levelName(e.level)}">{levelName(e.level)}</span>
          <span class="lg-meta">{e.remote_ip ? `${e.remote_ip}:${e.remote_port}` : '-'} {e.connection_id ?? ''}</span>
          <span class="lg-msg">{e.message}</span>
        </div>
      {/each}
    </div>
    <div class="log-footer">
      <span>{list.length} lines ({entries.length} in buffer)</span>
      {#if paused}<span class="paused-badge">Paused (scrolled up)</span>{/if}
    </div>
  </div>
</div>

<style>
  .page {
    padding: 2rem;
    display: flex;
    flex-direction: column;
    height: calc(100vh - 4rem);
  }

  .header {
    display: flex;
    align-items: center;
    gap: 1.5rem;
    margin-bottom: 1.25rem;
    flex-wrap: wrap;
  }

  h2 {
    margin: 0;
    color: var(--text);
    font-size: 1.2rem;
  }

  .controls {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    flex-wrap: wrap;
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
    padding: 0.4rem 0.85rem;
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

  .clear-btn {
    background: transparent;
    border-color: #7f1d1d;
    color: #fca5a5;
  }

  .clear-btn:hover {
    background: #7f1d1d33;
  }

  .search-input {
    padding: 0.4rem 0.65rem;
    background: #12141c;
    border: 1px solid var(--border);
    border-radius: 5px;
    color: var(--text);
    font-size: 0.85rem;
    font-family: inherit;
    width: 220px;
  }

  .search-input:focus {
    outline: none;
    border-color: var(--accent);
  }

  .type-filter {
    position: relative;
  }

  .filter-toggle {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    padding: 0.35rem 0.6rem;
    background: #12141c;
    border: 1px solid var(--border);
    border-radius: 5px;
    color: var(--text-muted);
    font-size: 0.85rem;
    cursor: pointer;
  }

  .filter-toggle:hover {
    border-color: #3a3d4a;
  }

  .caret {
    font-size: 0.7rem;
    color: var(--text-dim);
  }

  .filter-panel {
    position: absolute;
    top: calc(100% + 4px);
    left: 0;
    z-index: 10;
    min-width: 150px;
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    background: #12141c;
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 0.4rem;
    box-shadow: 0 8px 20px rgba(0, 0, 0, 0.35);
  }

  .filter-option {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.3rem 0.4rem;
    border-radius: 4px;
    font-size: 0.82rem;
    color: var(--text-muted);
    cursor: pointer;
    user-select: none;
  }

  .filter-option:hover {
    background: #22263a;
    color: var(--text);
  }

  .lvl-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
  }

  .log-panel {
    flex: 1;
    min-height: 0;
    background: #12141c;
    border: 1px solid var(--border);
    border-radius: 8px;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  .log-scroll {
    flex: 1;
    overflow-y: auto;
    padding: 0.5rem 0;
    font-family: ui-monospace, monospace;
    font-size: 0.8rem;
    line-height: 1.6;
  }

  .log-line {
    display: flex;
    gap: 0.6rem;
    padding: 0.05rem 1rem;
    white-space: pre-wrap;
    word-break: break-word;
  }

  .log-line:hover {
    background: var(--surface)40;
  }

  .lg-time {
    color: var(--text-dim);
    flex-shrink: 0;
  }

  .lg-level {
    flex-shrink: 0;
    min-width: 4.8em;
    white-space: nowrap;
    text-align: center;
    font-weight: 600;
    font-size: 0.72rem;
    border-radius: 3px;
    padding: 0 0.4rem;
    align-self: flex-start;
  }

  .lg-meta {
    color: var(--text-dim);
    flex-shrink: 0;
  }

  .lg-msg {
    color: var(--text-muted);
    flex: 1;
  }

  .lvl-EMERG, .lvl-ALERT, .lvl-CRIT, .lvl-ERR { background: #3a1a1a; color: #fca5a5; }
  .lvl-WARNING { background: #3a2a1a; color: #fdba74; }
  .lvl-NOTICE { background: #2d2a1a; color: #fde68a; }
  .lvl-INFO { background: #1a2d3a; color: #7dd3fc; }
  .lvl-DEBUG, .lvl-EDEBUG { background: #22263a; color: var(--text-dim); }

  .log-line.lvl-row-ERR .lg-msg, .log-line.lvl-row-CRIT .lg-msg,
  .log-line.lvl-row-ALERT .lg-msg, .log-line.lvl-row-EMERG .lg-msg {
    color: #fca5a5;
  }

  .log-line.lvl-row-WARNING .lg-msg {
    color: #fdba74;
  }

  .log-footer {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0.5rem 1rem;
    border-top: 1px solid var(--border);
    font-size: 0.78rem;
    color: var(--text-dim);
  }

  .paused-badge {
    background: #3a2a1a;
    color: #fdba74;
    padding: 0.1rem 0.5rem;
    border-radius: 3px;
    font-size: 0.72rem;
    font-weight: 600;
  }

  .error {
    color: #fca5a5;
    font-size: 0.9rem;
  }
</style>
