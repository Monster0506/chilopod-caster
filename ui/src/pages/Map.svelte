<script>
  import { onMount } from 'svelte';
  import { apiGet } from '../lib/api.js';
  import { ggaQualityColor, ggaQualityLabel } from '../lib/gga.js';
  import L from 'leaflet';
  import 'leaflet/dist/leaflet.css';

  let table = $state(null);
  let net = $state(null);
  let settings = $state(null);
  let error = $state('');
  let autoRefresh = $state(true);

  let mapEl;
  let map;
  let baseLayer;
  let roverLayer;
  let coverageLayer;
  let baselineLayer;
  let fitted = false;
  let roverMarkers = {};
  let baseMarkers = {};

  const VIEW_STORAGE_KEY = 'chilopod:map:view';

  function loadSavedView() {
    try {
      const raw = localStorage.getItem(VIEW_STORAGE_KEY);
      return raw ? JSON.parse(raw) : null;
    } catch {
      return null;
    }
  }

  function saveView() {
    if (!map) return;
    const center = map.getCenter();
    localStorage.setItem(
      VIEW_STORAGE_KEY,
      JSON.stringify({ lat: center.lat, lng: center.lng, zoom: map.getZoom() })
    );
  }

  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  }

  function dotIcon(color, ringColor) {
    const ringStyle = ringColor ? `border-color:${ringColor};border-width:3px;` : '';
    return L.divIcon({
      className: 'map-dot-icon',
      html: `<span class="map-dot" style="background:${color};${ringStyle}"></span>`,
      iconSize: [14, 14],
      iconAnchor: [7, 7],
      popupAnchor: [0, -7],
    });
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

  function formatBytes(n) {
    if (n == null) return '-';
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
    return (n / (1024 * 1024)).toFixed(1) + ' MB';
  }

  function strField(str, i) {
    return (str ?? '').split(';')[i] ?? '';
  }

  function baseStations() {
    if (!table) return [];
    return Object.entries(table.mountpoints)
      .filter(([, m]) => !m.virtual)
      .map(([key, m]) => {
        const live = net
          ? Object.values(net).find((c) => (c.type === 'source' || c.type === 'source_fetcher') && c.mountpoint === key) ?? null
          : null;
        const subscribers = net
          ? Object.values(net).filter((c) => c.type === 'client' && c.gga != null && (c.mountpoint === key || c.assigned_base === key)).length
          : 0;
        return { key, ...m, live, subscribers };
      });
  }

  function rovers() {
    if (!net) return [];
    return Object.values(net).filter((c) => c.type === 'client' && c.gga != null);
  }

  function roverPositions() {
    return rovers().filter((c) => c.lat != null && c.lon != null);
  }

  async function fetchAll() {
    try {
      const [tables, connections, s] = await Promise.all([
        apiGet('sourcetables'),
        apiGet('net'),
        apiGet('settings'),
      ]);
      error = '';
      table = tables.find((t) => t.host === 'LOCAL') ?? { mountpoints: {} };
      net = connections;
      settings = s;
    } catch (e) {
      error = e.message;
    }
  }

  function flyToRover(r) {
    if (!map || r.lat == null || r.lon == null) return;
    map.flyTo([r.lat, r.lon], Math.max(map.getZoom(), 12));
    const marker = roverMarkers[r.id];
    if (marker) marker.openPopup();
  }

  function flyToBase(b) {
    if (!map || b.lat == null || b.lon == null) return;
    map.flyTo([b.lat, b.lon], Math.max(map.getZoom(), 12));
    const marker = baseMarkers[b.key];
    if (marker) marker.openPopup();
  }

  function rebuildMarkers() {
    if (!map) return;
    baseLayer.clearLayers();
    roverLayer.clearLayers();
    coverageLayer.clearLayers();
    baselineLayer.clearLayers();
    roverMarkers = {};
    baseMarkers = {};

    const bounds = [];
    const bases = baseStations();
    const basesByKey = Object.fromEntries(bases.map((b) => [b.key, b]));
    const lookupRadiusM = settings?.max_nearest_lookup_distance_m;

    for (const b of bases) {
      const color = b.live ? '#22c55e' : '#64748b';
      const marker = L.marker([b.lat, b.lon], { icon: dotIcon(color) });
      marker.bindPopup(
        `<b>${escapeHtml(b.key)}</b><br>` +
          `${escapeHtml(strField(b.str, 2) || 'Base station')}<br>` +
          `Status: <span class="badge ${b.live ? 'badge-live' : 'badge-offline'}">${b.live ? 'live' : 'declared, offline'}</span><br>` +
          `Subscribers: ${b.subscribers}<br>` +
          `<span class="map-popup-mono">${b.lat.toFixed(5)}, ${b.lon.toFixed(5)}</span>`,
        { autoPan: false }
      );
      baseLayer.addLayer(marker);
      baseMarkers[b.key] = marker;
      bounds.push([b.lat, b.lon]);

      // NEAR searches for a candidate base within this radius -- this is
      // often huge (the config default is 1000km), so a filled circle would
      // just wash the whole viewport one color whenever you're zoomed in
      // past its edge. Border only, so it's informative when zoomed out far
      // enough to see the boundary and invisible (correctly) otherwise.
      if (b.live && lookupRadiusM) {
        L.circle([b.lat, b.lon], {
          radius: lookupRadiusM,
          color: '#a78bfa',
          weight: 1.5,
          opacity: 0.45,
          fill: false,
        }).addTo(coverageLayer);
      }
    }

    for (const r of roverPositions()) {
      const assignedKey = r.assigned_base ?? r.mountpoint;
      const assignedBase = assignedKey ? basesByKey[assignedKey] : null;
      if (assignedBase) {
        L.polyline(
          [
            [r.lat, r.lon],
            [assignedBase.lat, assignedBase.lon],
          ],
          { color: '#c2410c', weight: 3.5, opacity: 0.85 }
        ).addTo(baselineLayer);
      }

      if (r.trail && r.trail.length > 1) {
        const segments = r.trail.length - 1;
        for (let i = 1; i < r.trail.length; i++) {
          // Fade older segments out and the newest segment in, so the trail
          // reads as a fading tail behind the rover's current position.
          const opacity = 0.12 + (i / segments) * 0.55;
          L.polyline([r.trail[i - 1], r.trail[i]], {
            color: '#3b82f6',
            weight: 3,
            opacity,
          }).addTo(roverLayer);
        }
      }

      const marker = L.marker([r.lat, r.lon], { icon: dotIcon('#3b82f6', ggaQualityColor(r.gga?.quality)) });
      marker.bindPopup(
        `<b>Rover ${escapeHtml(r.auth_user ?? r.id)}</b><br>` +
          `IP: ${escapeHtml(r.ip ?? '-')}<br>` +
          `Mountpoint: ${escapeHtml(r.mountpoint ?? r.assigned_base ?? '-')}<br>` +
          `Fix: ${escapeHtml(ggaQualityLabel(r.gga?.quality))}<br>` +
          `Connected: ${formatDuration(r.start)}<br>` +
          `Received: ${formatBytes(r.received_bytes)}<br>` +
          `<span class="map-popup-mono">${r.lat.toFixed(5)}, ${r.lon.toFixed(5)}</span>`,
        { autoPan: false }
      );
      roverLayer.addLayer(marker);
      roverMarkers[r.id] = marker;
      bounds.push([r.lat, r.lon]);
    }

    if (!fitted && bounds.length > 0) {
      map.fitBounds(bounds, { padding: [40, 40], maxZoom: 10 });
      fitted = true;
    }
  }

  onMount(() => {
    const savedView = loadSavedView();
    map = L.map(mapEl, {
      center: savedView ? [savedView.lat, savedView.lng] : [39, -98],
      zoom: savedView ? savedView.zoom : 4,
    });
    if (savedView) fitted = true;

    const satellite = L.tileLayer(
      'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',
      { attribution: 'Tiles &copy; Esri', maxZoom: 19 }
    ).addTo(map);
    const street = L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
      attribution: '&copy; OpenStreetMap contributors',
      maxZoom: 19,
    });

    baseLayer = L.layerGroup().addTo(map);
    roverLayer = L.layerGroup().addTo(map);
    // Off by default -- the circle radius is often huge (see
    // max_nearest_lookup_distance_m), so it's opt-in via the layer control
    // rather than shown automatically.
    coverageLayer = L.layerGroup();
    baselineLayer = L.layerGroup().addTo(map);

    L.control
      .layers(
        { Satellite: satellite, Street: street },
        {
          'Base stations': baseLayer,
          Rovers: roverLayer,
          'NEAR coverage': coverageLayer,
          Baselines: baselineLayer,
        }
      )
      .addTo(map);

    const Legend = L.Control.extend({
      options: { position: 'bottomleft' },
      onAdd() {
        const div = L.DomUtil.create('div', 'map-legend');
        const fixRows = Object.entries({ 4: 'RTK Fixed', 5: 'RTK Float', 2: 'DGPS / PPS', 1: 'Standalone', '-1': 'No fix' })
          .map(([q, label]) => {
            const color = q === '-1' ? ggaQualityColor(null) : ggaQualityColor(Number(q));
            return `<div><span class="map-legend-dot map-legend-ring" style="border-color:${color}"></span>${label}</div>`;
          })
          .join('');
        div.innerHTML =
          '<div class="map-legend-columns">' +
          '<div class="map-legend-col">' +
          '<div><span class="map-legend-dot" style="background:#22c55e"></span>Live base</div>' +
          '<div><span class="map-legend-dot" style="background:#64748b"></span>Declared, offline</div>' +
          '<div><span class="map-legend-dot" style="background:#3b82f6"></span>Rover</div>' +
          '<div><span class="map-legend-line" style="background:#c2410c"></span>Baseline</div>' +
          '<div><span class="map-legend-line" style="background:#a78bfa"></span>NEAR coverage</div>' +
          '</div>' +
          `<div class="map-legend-col">${fixRows}</div>` +
          '</div>';
        return div;
      },
    });
    new Legend().addTo(map);

    map.on('moveend zoomend', saveView);

    return () => map.remove();
  });

  $effect(() => {
    if (table || net) rebuildMarkers();
  });

  $effect(() => {
    fetchAll();
    if (!autoRefresh) return;
    const id = setInterval(fetchAll, 5000);
    return () => clearInterval(id);
  });
</script>

<div class="header">
  <h2>Map</h2>
  <div class="controls">
    <label class="toggle">
      <input type="checkbox" bind:checked={autoRefresh} />
      Auto-refresh
    </label>
    <button onclick={fetchAll}>Refresh</button>
  </div>
</div>

{#if error}
  <p class="error">{error}</p>
{/if}

<div class="map-card">
  <div class="map-container" bind:this={mapEl}></div>
</div>

<h3 class="table-title">Base stations</h3>
<div class="table-wrap">
  <table>
    <thead>
      <tr>
        <th>Mountpoint</th>
        <th>Identifier</th>
        <th>Status</th>
        <th>Subscribers</th>
        <th>Position</th>
      </tr>
    </thead>
    <tbody>
      {#if baseStations().length === 0}
        <tr><td colspan="5" class="empty">No base stations declared.</td></tr>
      {/if}
      {#each baseStations() as b (b.key)}
        <tr class="clickable" onclick={() => flyToBase(b)} title="Click to locate on map">
          <td class="mono">{b.key}</td>
          <td>{strField(b.str, 2)}</td>
          <td>
            {#if b.live}
              <span class="badge badge-live">live</span>
            {:else}
              <span class="badge badge-offline">declared, offline</span>
            {/if}
          </td>
          <td class="mono">{b.subscribers}</td>
          <td class="mono">{b.lat.toFixed(5)}, {b.lon.toFixed(5)}</td>
        </tr>
      {/each}
    </tbody>
  </table>
</div>

<h3 class="table-title">Rover positions</h3>
<div class="table-wrap">
  <table>
    <thead>
      <tr>
        <th>User</th>
        <th>IP</th>
        <th>Mountpoint</th>
        <th>Fix</th>
        <th>Position</th>
        <th>Connected</th>
        <th>Received</th>
      </tr>
    </thead>
    <tbody>
      {#if rovers().length === 0}
        <tr><td colspan="7" class="empty">No rovers connected.</td></tr>
      {/if}
      {#each rovers() as r (r.id)}
        <tr
          class:clickable={r.lat != null}
          onclick={() => flyToRover(r)}
          title={r.lat != null ? 'Click to locate on map' : ''}
        >
          <td class="mono">{r.auth_user ?? '-'}</td>
          <td class="mono">{r.ip}</td>
          <td class="mono">{r.mountpoint ?? r.assigned_base ?? '-'}</td>
          <td><span class="fix-cell"><span class="dot" style="background:{ggaQualityColor(r.gga?.quality)}"></span>{ggaQualityLabel(r.gga?.quality)}</span></td>
          <td class="mono">{r.lat != null ? `${r.lat.toFixed(5)}, ${r.lon.toFixed(5)}` : 'no GGA yet'}</td>
          <td class="mono">{formatDuration(r.start)}</td>
          <td class="mono">{formatBytes(r.received_bytes)}</td>
        </tr>
      {/each}
    </tbody>
  </table>
</div>

<style>
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 1rem;
  }

  h2 {
    margin: 0;
    color: var(--text);
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

  button:hover {
    background: #1e40af;
  }

  .map-card {
    border: 1px solid var(--border);
    border-radius: 8px;
    overflow: hidden;
    margin-bottom: 1.5rem;
  }

  .map-container {
    height: 480px;
    width: 100%;
    background: #12141c;
  }

  .table-title {
    margin: 0 0 0.75rem;
    font-size: 0.95rem;
    color: var(--text);
  }

  .table-title:not(:first-of-type) {
    margin-top: 2rem;
  }

  .badge {
    padding: 0.15rem 0.5rem;
    border-radius: 3px;
    font-size: 0.75rem;
    font-weight: 500;
    white-space: nowrap;
  }

  .badge-live {
    background: #1a3a1a;
    color: #86efac;
  }

  .badge-offline {
    background: var(--border);
    color: var(--text-muted);
  }

  .dot {
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 50%;
  }

  .fix-cell {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    white-space: nowrap;
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

  tr.clickable {
    cursor: pointer;
  }

  .mono {
    font-family: monospace;
    font-size: 0.82rem;
  }

  .empty {
    text-align: center;
    color: var(--text-dim);
    padding: 2rem;
  }

  .error {
    color: #fca5a5;
    font-size: 0.9rem;
    margin-bottom: 1rem;
  }

  :global(.map-dot-icon) {
    background: transparent;
    border: none;
  }

  :global(.map-dot) {
    display: block;
    width: 14px;
    height: 14px;
    border-radius: 50%;
    border: 2px solid var(--bg);
    box-shadow: 0 0 4px rgba(0, 0, 0, 0.6);
    opacity: 0.82;
  }

  :global(.map-popup-mono) {
    font-family: monospace;
    font-size: 0.78rem;
  }

  :global(.leaflet-popup-content-wrapper) {
    background: var(--surface);
    color: var(--text);
  }

  :global(.leaflet-popup-tip) {
    background: var(--surface);
  }

  :global(.map-legend) {
    background: var(--surface)ee;
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 0.5rem 0.65rem;
    color: #cbd5e1;
    font-size: 0.75rem;
    line-height: 1.6;
    box-shadow: none;
  }

  :global(.map-legend-columns) {
    display: flex;
    gap: 0.9rem;
  }

  :global(.map-legend-col) {
    display: flex;
    flex-direction: column;
  }

  :global(.map-legend-col:first-child) {
    padding-right: 0.9rem;
    border-right: 1px solid var(--border);
  }

  :global(.map-legend-dot) {
    display: inline-block;
    width: 9px;
    height: 9px;
    border-radius: 50%;
    margin-right: 0.4rem;
    vertical-align: middle;
  }

  :global(.map-legend-ring) {
    background: transparent;
    border: 2px solid;
    box-sizing: border-box;
  }

  :global(.map-legend-line) {
    display: inline-block;
    width: 12px;
    height: 2px;
    margin-right: 0.4rem;
    vertical-align: middle;
  }
</style>
