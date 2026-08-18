<script>
  import { apiGet, apiPost } from '../lib/api.js';
  import { ggaQualityColor, ggaQualityLabel } from '../lib/gga.js';

  const HISTORY_MAX = 20;
  const POLL_MS = 5000;
  const CONN_WINDOW_S = 10;
  const CONN_WINDOW_SAMPLES = Math.round(CONN_WINDOW_S / (POLL_MS / 1000)) + 1;

  let net = $state(null);
  let livesources = $state(null);
  let mem = $state(null);
  let sourcetable = $state(null);
  let rtcm = $state(null);
  let logEntries = $state([]);
  let alarmEntries = $state([]);
  let settings = $state(null);
  let error = $state('');
  let reloading = $state(false);
  let reloadMsg = $state('');

  let connHistory = $state([]);
  let churnHistory = $state([]);
  let previousConnIds = null;

  function strField(str, i) {
    return str.split(';')[i] ?? '';
  }

  function liveInfo(mountpoint) {
    if (!net) return null;
    return Object.values(net).find((c) => (c.type === 'source' || c.type === 'source_fetcher') && c.mountpoint === mountpoint) ?? null;
  }

  function formatAgo(dateStr) {
    if (!dateStr) return '-';
    const sec = Math.floor((Date.now() - new Date(dateStr).getTime()) / 1000);
    if (sec < 0) return 'just now';
    if (sec < 60) return sec + 's ago';
    if (sec < 3600) return Math.floor(sec / 60) + 'm ago';
    return Math.floor(sec / 3600) + 'h ago';
  }

  const ALARM_TYPE_LABELS = {
    station_offline: 'Offline',
    station_online: 'Back online',
    low_sv_count: 'Low SV count',
    position_drift: 'Position drift',
  };

  function alarmTypeLabel(type) {
    return ALARM_TYPE_LABELS[type] ?? type;
  }

  function formatDist(m) {
    if (m == null) return '-';
    if (m >= 1000) return (m / 1000).toFixed(2) + ' km';
    return m.toFixed(0) + ' m';
  }

  function readinessState(status, notConfiguredLabel, notReadyLabel, readyLabel) {
    if (!status?.configured) return { cls: 'off', label: notConfiguredLabel };
    if (!status.ready) return { cls: 'warn', label: notReadyLabel };
    return { cls: 'good', label: readyLabel };
  }

  function sidecarStatus() {
    return readinessState(settings?.sidecar, 'not configured', 'configured, no data', 'active');
  }

  function ruckusStatus() {
    return readinessState(settings?.ruckus, 'not configured', 'binary not found', 'ready');
  }

  function formatLatency(ms) {
    if (ms == null) return '-';
    if (ms >= 1000) return (ms / 1000).toFixed(1) + 's';
    return Math.round(ms) + 'ms';
  }

  function pushHistory(arr, value, max = HISTORY_MAX) {
    const next = [...arr, value];
    return next.length > max ? next.slice(-max) : next;
  }

  function trend(history) {
    if (history.length < 2) return null;
    const first = history[0];
    const last = history[history.length - 1];
    const elapsedS = (history.length - 1) * (POLL_MS / 1000);
    return { delta: last - first, elapsedS };
  }

  function formatElapsed(s) {
    if (s < 60) return `${Math.round(s)}s`;
    return `${Math.round(s / 60)}m`;
  }

  async function fetchAll() {
    try {
      const [netRes, livesourcesRes, memRes, tablesRes, rtcmRes, logRes, alarmsRes, settingsRes] = await Promise.all([
        apiGet('net'), apiGet('livesources'), apiGet('mem'),
        apiGet('sourcetables'), apiGet('rtcm'), apiGet('log'), apiGet('alarms'), apiGet('settings'),
      ]);
      error = '';
      net = netRes;
      livesources = livesourcesRes;
      mem = memRes;
      sourcetable = tablesRes.find((t) => t.host === 'LOCAL') ?? { mountpoints: {} };
      rtcm = rtcmRes;
      logEntries = logRes.filter((e) => e.level <= 6).slice(-6).reverse();
      alarmEntries = alarmsRes.slice(0, 6);
      settings = settingsRes;

      const cc = connCounts(net);
      connHistory = pushHistory(connHistory, cc.total, CONN_WINDOW_SAMPLES);

      const currentIds = new Set(Object.keys(net));
      if (previousConnIds) {
        const connects = [...currentIds].filter((id) => !previousConnIds.has(id)).length;
        const disconnects = [...previousConnIds].filter((id) => !currentIds.has(id)).length;
        churnHistory = pushHistory(churnHistory, connects + disconnects);
      }
      previousConnIds = currentIds;
    } catch (e) {
      error = e.message;
    }
  }

  function connCounts(n) {
    const conns = Object.values(n);
    return {
      total: conns.length,
      clients: conns.filter((c) => c.type === 'client').length,
      sources: conns.filter((c) => c.type === 'source' || c.type === 'source_fetcher').length,
    };
  }

  function memAllocated(m) {
    const bytes = m?.jemalloc?.stats?.allocated;
    if (bytes == null) return null;
    return bytes / 1024 / 1024;
  }

  function memUnavailable(m) {
    return !!m?.err;
  }

  function rovers(n) {
    return Object.values(n).filter((c) => c.type === 'client' && c.gga != null);
  }

  function fixBreakdown(roverList) {
    const counts = { standalone: 0, dgps: 0, float: 0, fixed: 0, other: 0 };
    for (const r of roverList) {
      const q = r.gga?.quality;
      if (q === 1) counts.standalone++;
      else if (q === 2 || q === 3) counts.dgps++;
      else if (q === 5) counts.float++;
      else if (q === 4) counts.fixed++;
      else if (q != null) counts.other++;
    }
    return counts;
  }

  function localMountpoints() {
    if (!sourcetable) return [];
    return Object.entries(sourcetable.mountpoints).filter(([, mnt]) => !mnt.virtual);
  }

  function referenceStations() {
    return localMountpoints().map(([key, mnt]) => {
      const info = rtcm?.[key];
      const live = !!liveInfo(key);
      return {
        key,
        group: strField(mnt.str, 7),
        live,
        svs: info?.sidecar?.satellite_count ?? null,
        latencyMs: info?.sidecar?.latency_ms ?? null,
      };
    });
  }

  function groupSummary() {
    const map = new Map();
    for (const [key, mnt] of localMountpoints()) {
      const group = strField(mnt.str, 7) || 'NONE';
      const live = !!liveInfo(key);
      if (!map.has(group)) map.set(group, { live: 0, total: 0 });
      const g = map.get(group);
      g.total++;
      if (live) g.live++;
    }
    return [...map.entries()].sort(([a], [b]) => a.localeCompare(b));
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

  function sparkPath(history, w, h) {
    if (history.length < 2) return null;
    const max = Math.max(...history), min = Math.min(...history);
    const range = max - min || 1;
    const step = w / (history.length - 1);
    const coords = history.map((v, i) => [i * step, h - ((v - min) / range) * (h - 4) - 2]);
    const path = coords.map((c, i) => (i === 0 ? 'M' : 'L') + c[0].toFixed(1) + ',' + c[1].toFixed(1)).join(' ');
    return { path, area: path + ` L${w},${h} L0,${h} Z` };
  }

  $effect(() => {
    fetchAll();
    const id = setInterval(fetchAll, POLL_MS);
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
  {:else if !net || !sourcetable}
    <p class="loading">Loading…</p>
  {:else}
    {@const cc = connCounts(net)}
    {@const roverList = rovers(net)}
    {@const fixCounts = fixBreakdown(roverList)}
    {@const connTrend = trend(connHistory)}
    {@const stations = referenceStations()}
    {@const groups = groupSummary()}
    {@const memMB = memAllocated(mem)}
    {@const connSpark = sparkPath(connHistory, 64, 26)}
    {@const localCount = localMountpoints().length}
    {@const liveCount = stations.filter((s) => s.live).length}
    {@const churnW = 1040}
    {@const churnBarW = (churnW - 19 * 4) / 20}
    {@const maxChurn = Math.max(1, ...churnHistory)}
    {@const sidecar = sidecarStatus()}
    {@const ruckus = ruckusStatus()}

    <div class="bare-strip">
      <span class="strip-label">Groups</span>
      {#if groups.length === 0}
        <span class="strip-empty">No local mountpoints configured.</span>
      {:else}
        {#each groups as [name, g] (name)}
          <span class="chip">
            <span class="dot" class:good={g.live === g.total} class:warn={g.live > 0 && g.live < g.total} class:off={g.live === 0}></span>
            {name} {g.live}/{g.total}
          </span>
        {/each}
      {/if}
      <span class="strip-label strip-label-right">System</span>
      <span class="chip" title="Sidecar stats file: {settings?.sidecar?.configured ? 'configured' : 'not configured'}">
        <span class="dot" class:good={sidecar.cls === 'good'} class:warn={sidecar.cls === 'warn'} class:off={sidecar.cls === 'off'}></span>
        Sidecar: {sidecar.label}
      </span>
      <span class="chip" title={settings?.ruckus?.path ? `ruckus_path: ${settings.ruckus.path}` : 'alarms not configured'}>
        <span class="dot" class:good={ruckus.cls === 'good'} class:warn={ruckus.cls === 'warn'} class:off={ruckus.cls === 'off'}></span>
        Ruckus: {ruckus.label}
      </span>
    </div>

    <div class="board">
      <div class="card s3">
        <p class="card-title">Connections</p>
        <div class="stat-row">
          <div class="stat-value">{cc.total}</div>
          {#if connSpark}
            <svg width="64" height="26" viewBox="0 0 64 26">
              <path d={connSpark.area} fill="#93c5fd" opacity="0.14" />
              <path d={connSpark.path} fill="none" stroke="#93c5fd" stroke-width="2" stroke-linecap="round" />
            </svg>
          {/if}
        </div>
        <div class="stat-sub">{cc.clients} clients &middot; {cc.sources} sources</div>
        {#if connTrend}
          <div class="trend" class:up={connTrend.delta > 0} class:down={connTrend.delta < 0}>
            {connTrend.delta > 0 ? '▲' : connTrend.delta < 0 ? '▼' : '–'} {Math.abs(connTrend.delta)}
            <span class="dim">vs {formatElapsed(connTrend.elapsedS)} ago</span>
          </div>
        {/if}
      </div>

      <div class="card s3">
        <p class="card-title">Live Sources</p>
        <div class="stat-row">
          <div class="stat-value">{liveCount}<span class="stat-sub-inline">/{localCount}</span></div>
        </div>
        <div class="stat-sub">local mountpoints active</div>
      </div>

      <div class="card s3">
        <p class="card-title">Memory</p>
        {#if memMB != null}
          <div class="stat-row">
            <div class="stat-value">{memMB.toFixed(1)}<span class="stat-sub-inline">MB</span></div>
          </div>
          <div class="stat-sub">jemalloc allocated</div>
        {:else if memUnavailable(mem)}
          <div class="stat-value" style="font-size:1rem;color:#64748b">Not available</div>
          <div class="stat-sub">requires a -DDEBUG_JEMALLOC build</div>
        {:else}
          <div class="stat-value">-</div>
        {/if}
      </div>

      <div class="card s3">
        <p class="card-title">Rovers</p>
        <div class="stat-row">
          <div class="stat-value">{roverList.length}</div>
        </div>
        <div class="chip-row">
          {#if fixCounts.fixed}<span class="chip"><span class="dot good"></span>{fixCounts.fixed} fixed</span>{/if}
          {#if fixCounts.float}<span class="chip"><span class="dot warn"></span>{fixCounts.float} float</span>{/if}
          {#if fixCounts.dgps}<span class="chip"><span class="dot" style="background:#93c5fd"></span>{fixCounts.dgps} dgps</span>{/if}
          {#if fixCounts.standalone}<span class="chip"><span class="dot off"></span>{fixCounts.standalone} standalone</span>{/if}
          {#if roverList.length === 0}<span class="stat-sub">no rovers connected</span>{/if}
        </div>
      </div>

      <div class="card s7">
        <p class="card-title">Reference Stations</p>
        <div class="table-scroll">
          <table class="mini">
            <thead><tr><th></th><th>Station</th><th>Group</th><th>SVs</th><th>Latency</th></tr></thead>
            <tbody>
              {#if stations.length === 0}
                <tr><td colspan="5" class="empty-cell">No local mountpoints configured.</td></tr>
              {/if}
              {#each stations as s (s.key)}
                <tr>
                  <td><span class="dot" class:good={s.live} class:off={!s.live}></span></td>
                  <td class="mono">{s.key}</td>
                  <td>{s.group || '—'}</td>
                  <td class="mono">{s.svs ?? '—'}</td>
                  <td class="mono">{formatLatency(s.latencyMs)}</td>
                </tr>
              {/each}
            </tbody>
          </table>
        </div>
      </div>

      <div class="card s5">
        <p class="card-title">Alarms</p>
        {#if alarmEntries.length === 0}
          <p class="stat-sub">No alarms yet.</p>
        {:else}
          {#each alarmEntries as a, i (a.time + i)}
            <div class="list-row alarm-row">
              <span class="dot" class:good={a.sent} class:warn={!a.sent && a.suppressed} class:bad={!a.sent && !a.suppressed}></span>
              <span class="t">{formatAgo(a.time)}</span>
              <span class="alarm-body">
                <span class="alarm-mp mono">{a.mountpoint}</span>
                <span class="alarm-type">{alarmTypeLabel(a.type)}</span>
                {#if !a.sent}<span class="alarm-fail">{a.suppressed ? 'send suppressed' : 'send failed'}</span>{/if}
              </span>
            </div>
          {/each}
        {/if}
      </div>

      <div class="card s7">
        <div class="card-title-row">
          <p class="card-title">Active Rovers</p>
          <div class="chip-row">
            {#if fixCounts.fixed}<span class="chip"><span class="dot good"></span>{fixCounts.fixed} fixed</span>{/if}
            {#if fixCounts.float}<span class="chip"><span class="dot warn"></span>{fixCounts.float} float</span>{/if}
            {#if fixCounts.standalone}<span class="chip"><span class="dot off"></span>{fixCounts.standalone} standalone</span>{/if}
          </div>
        </div>
        <div class="table-scroll">
          <table class="mini">
            <thead><tr><th></th><th>User</th><th>Base</th><th>Dist</th><th>Fix</th></tr></thead>
            <tbody>
              {#if roverList.length === 0}
                <tr><td colspan="5" class="empty-cell">No rovers connected.</td></tr>
              {/if}
              {#each roverList as r (r.id)}
                <tr>
                  <td><span class="dot" style="background:{ggaQualityColor(r.gga?.quality)}"></span></td>
                  <td class="mono">{r.auth_user ?? r.ip}</td>
                  <td class="mono">{r.assigned_base ?? r.mountpoint ?? '—'}</td>
                  <td class="mono">{formatDist(r.dist_to_base_m)}</td>
                  <td>{ggaQualityLabel(r.gga?.quality)}</td>
                </tr>
              {/each}
            </tbody>
          </table>
        </div>
      </div>

      <div class="card s5">
        <p class="card-title">Activity</p>
        {#if logEntries.length === 0}
          <p class="stat-sub">No recent activity.</p>
        {:else}
          {#each logEntries as e (e.id)}
            <div class="list-row"><span class="t">{formatAgo(e.timestamp)}</span><span>{e.message}</span></div>
          {/each}
        {/if}
      </div>

      <div class="bare-chart">
        <svg width="100%" height="60" viewBox="0 0 {churnW} 60" preserveAspectRatio="none">
          {#each churnHistory as v, i (i)}
            {@const h = Math.max(2, (v / maxChurn) * 56)}
            {@const x = i * (churnBarW + 4)}
            <rect x={x.toFixed(1)} y={(60 - h).toFixed(1)} width={churnBarW.toFixed(1)} height={h.toFixed(1)} rx="2" fill={v > maxChurn * 0.6 ? '#fbbf24' : '#64748b'} />
          {/each}
        </svg>
        <p class="chart-caption">
          Connection Churn &middot; connects + disconnects per {POLL_MS / 1000}s poll, observed since this page opened
        </p>
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
    margin-bottom: 1.5rem;
  }

  h2 {
    margin: 0;
    color: var(--text);
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

  .reload-msg {
    font-size: 0.85rem;
    color: var(--text-dim);
  }

  .bare-strip {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    flex-wrap: wrap;
    padding-bottom: 1rem;
    margin-bottom: 1.15rem;
    border-bottom: 1px solid #1e2130;
  }

  .strip-label {
    font-size: 0.72rem;
    font-weight: 600;
    letter-spacing: 0.03em;
    text-transform: uppercase;
    color: var(--text-dim);
    margin-right: 0.3rem;
  }

  .strip-empty {
    font-size: 0.78rem;
    color: var(--text-dim);
  }

  .strip-label-right {
    margin-left: auto;
  }

  .board {
    display: grid;
    grid-template-columns: repeat(12, 1fr);
    gap: 1rem;
  }

  .card {
    grid-column: span 12;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 1.1rem 1.25rem;
    min-width: 0;
  }

  .s3 { grid-column: span 3; }
  .s5 { grid-column: span 5; }
  .s7 { grid-column: span 7; }

  @media (max-width: 1000px) {
    .s3, .s5, .s7 { grid-column: span 12; }
  }

  .card-title {
    font-size: 0.72rem;
    font-weight: 600;
    letter-spacing: 0.03em;
    text-transform: uppercase;
    color: var(--text-dim);
    margin: 0 0 0.85rem;
  }

  .card-title-row {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 0.75rem;
    margin-bottom: 0.85rem;
  }
  .card-title-row .card-title { margin: 0; }

  .stat-value {
    font-family: monospace;
    font-variant-numeric: tabular-nums;
    font-size: 1.9rem;
    font-weight: 700;
    color: var(--text);
    line-height: 1;
  }

  .stat-sub-inline {
    font-size: 1rem;
    color: var(--text-dim);
  }

  .stat-sub {
    font-size: 0.75rem;
    color: var(--text-dim);
    margin-top: 0.35rem;
  }

  .stat-row {
    display: flex;
    align-items: flex-end;
    justify-content: space-between;
    gap: 0.5rem;
  }

  .trend {
    display: flex;
    align-items: center;
    gap: 0.25rem;
    font-family: monospace;
    font-size: 0.72rem;
    margin-top: 0.5rem;
    color: var(--text-muted);
  }
  .trend.up { color: #86efac; }
  .trend.down { color: #fca5a5; }
  .trend .dim { color: var(--text-dim); }

  .dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
    display: inline-block;
    background: var(--text-dim);
  }
  .dot.good { background: var(--good); box-shadow: 0 0 0 2px #14301b; }
  .dot.warn { background: var(--warn); box-shadow: 0 0 0 2px #3a2a0f; }
  .dot.off { background: var(--text-dim); }
  .dot.bad { background: var(--bad); box-shadow: 0 0 0 2px #3a1414; }

  .chip-row {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
    margin-top: 0.5rem;
  }

  .chip {
    font-size: 0.74rem;
    padding: 0.3rem 0.6rem;
    border-radius: 999px;
    background: #14161f;
    border: 1px solid #1e2130;
    color: var(--text-muted);
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
  }

  .mono {
    font-family: monospace;
    font-variant-numeric: tabular-nums;
  }

  .table-scroll {
    max-height: 226px;
    overflow-y: auto;
    overflow-x: auto;
  }
  .table-scroll table.mini thead th {
    position: sticky;
    top: 0;
    background: var(--surface);
    z-index: 1;
  }

  table.mini {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.78rem;
  }
  table.mini th {
    text-align: left;
    font-weight: 500;
    color: var(--text-dim);
    padding: 0 0.5rem 0.4rem 0;
    border-bottom: 1px solid #1e2130;
  }
  table.mini td {
    padding: 0.4rem 0.5rem 0.4rem 0;
    border-bottom: 1px solid #1e2130;
    color: var(--text-muted);
  }
  table.mini tr:last-child td { border-bottom: none; }

  .empty-cell {
    color: var(--text-dim);
    padding: 0.6rem 0;
  }

  .list-row {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    padding: 0.35rem 0;
    font-size: 0.78rem;
    color: var(--text-muted);
    border-bottom: 1px solid #1e2130;
  }
  .list-row:last-child { border-bottom: none; }
  .list-row .t {
    color: var(--text-dim);
    font-family: monospace;
    font-size: 0.68rem;
    width: 3.8rem;
    flex-shrink: 0;
  }

  .alarm-row .t { width: 3.2rem; }
  .alarm-body {
    display: flex;
    align-items: baseline;
    gap: 0.5rem;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .alarm-mp { color: var(--text); flex-shrink: 0; }
  .alarm-type { color: var(--text-muted); }
  .alarm-fail { color: #fca5a5; font-size: 0.72rem; }

  .bare-chart {
    grid-column: span 12;
    padding-top: 0.25rem;
  }
  .chart-caption {
    margin: 0.7rem 0 0;
    text-align: center;
    font-size: 0.75rem;
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
</style>
