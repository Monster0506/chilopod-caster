<script>
  import { onMount } from 'svelte';
  import { apiGet } from '../lib/api.js';
  import L from 'leaflet';
  import 'leaflet/dist/leaflet.css';

  let table = $state(null);
  let net = $state(null);
  let error = $state('');
  let autoRefresh = $state(true);

  let mapEl;
  let map;
  let baseLayer;
  let roverLayer;
  let fitted = false;
  let roverMarkers = {};

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

  function dotIcon(color) {
    return L.divIcon({
      className: 'map-dot-icon',
      html: `<span class="map-dot" style="background:${color}"></span>`,
      iconSize: [14, 14],
      iconAnchor: [7, 7],
      popupAnchor: [0, -7],
    });
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

  function formatBytes(n) {
    if (n == null) return '—';
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
          ? Object.values(net).filter((c) => c.type === 'client' && (c.mountpoint === key || c.assigned_base === key)).length
          : 0;
        return { key, ...m, live, subscribers };
      });
  }

  function rovers() {
    if (!net) return [];
    return Object.values(net).filter((c) => c.type === 'client');
  }

  function roverPositions() {
    return rovers().filter((c) => c.lat != null && c.lon != null);
  }

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

  function flyToRover(r) {
    if (!map || r.lat == null || r.lon == null) return;
    map.flyTo([r.lat, r.lon], Math.max(map.getZoom(), 12));
    const marker = roverMarkers[r.id];
    if (marker) marker.openPopup();
  }

  function rebuildMarkers() {
    if (!map) return;
    baseLayer.clearLayers();
    roverLayer.clearLayers();
    roverMarkers = {};

    const bounds = [];

    for (const b of baseStations()) {
      const color = b.live ? '#22c55e' : '#64748b';
      const marker = L.marker([b.lat, b.lon], { icon: dotIcon(color) });
      marker.bindPopup(
        `<b>${escapeHtml(b.key)}</b><br>` +
          `${escapeHtml(strField(b.str, 2) || 'Base station')}<br>` +
          `Status: ${b.live ? 'live' : 'declared, offline'}<br>` +
          `Subscribers: ${b.subscribers}<br>` +
          `<span class="map-popup-mono">${b.lat.toFixed(5)}, ${b.lon.toFixed(5)}</span>`,
        { autoPan: false }
      );
      baseLayer.addLayer(marker);
      bounds.push([b.lat, b.lon]);
    }

    for (const r of roverPositions()) {
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

      const marker = L.marker([r.lat, r.lon], { icon: dotIcon('#3b82f6') });
      marker.bindPopup(
        `<b>Rover ${r.id}</b><br>` +
          `IP: ${escapeHtml(r.ip ?? '—')}<br>` +
          `Mountpoint: ${escapeHtml(r.mountpoint ?? r.assigned_base ?? '—')}<br>` +
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

    L.control
      .layers(
        { Satellite: satellite, Street: street },
        { 'Base stations': baseLayer, Rovers: roverLayer }
      )
      .addTo(map);

    const Legend = L.Control.extend({
      options: { position: 'bottomleft' },
      onAdd() {
        const div = L.DomUtil.create('div', 'map-legend');
        div.innerHTML =
          '<div><span class="map-legend-dot" style="background:#22c55e"></span>Live base</div>' +
          '<div><span class="map-legend-dot" style="background:#64748b"></span>Declared, offline</div>' +
          '<div><span class="map-legend-dot" style="background:#3b82f6"></span>Rover</div>';
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

<h3 class="table-title">Rover positions</h3>
<div class="table-wrap">
  <table>
    <thead>
      <tr>
        <th>ID</th>
        <th>IP</th>
        <th>Mountpoint</th>
        <th>Position</th>
        <th>Connected</th>
        <th>Received</th>
      </tr>
    </thead>
    <tbody>
      {#if rovers().length === 0}
        <tr><td colspan="6" class="empty">No rovers connected.</td></tr>
      {/if}
      {#each rovers() as r (r.id)}
        <tr
          class:clickable={r.lat != null}
          onclick={() => flyToRover(r)}
          title={r.lat != null ? 'Click to locate on map' : ''}
        >
          <td class="mono">{r.id}</td>
          <td class="mono">{r.ip}</td>
          <td class="mono">{r.mountpoint ?? r.assigned_base ?? '—'}</td>
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

  button:hover {
    background: #1e40af;
  }

  .map-card {
    border: 1px solid #2a2d3a;
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
    color: #e2e8f0;
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

  tr.clickable {
    cursor: pointer;
  }

  .mono {
    font-family: monospace;
    font-size: 0.82rem;
  }

  .empty {
    text-align: center;
    color: #475569;
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
    border: 2px solid #0f1117;
    box-shadow: 0 0 4px rgba(0, 0, 0, 0.6);
    opacity: 0.82;
  }

  :global(.map-popup-mono) {
    font-family: monospace;
    font-size: 0.78rem;
  }

  :global(.leaflet-popup-content-wrapper) {
    background: #1a1d27;
    color: #e2e8f0;
  }

  :global(.leaflet-popup-tip) {
    background: #1a1d27;
  }

  :global(.map-legend) {
    background: #1a1d27ee;
    border: 1px solid #2a2d3a;
    border-radius: 6px;
    padding: 0.5rem 0.65rem;
    color: #cbd5e1;
    font-size: 0.75rem;
    line-height: 1.6;
    box-shadow: none;
  }

  :global(.map-legend-dot) {
    display: inline-block;
    width: 9px;
    height: 9px;
    border-radius: 50%;
    margin-right: 0.4rem;
    vertical-align: middle;
  }
</style>
