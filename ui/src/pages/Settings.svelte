<script>
  import { apiGet, apiPost, getCredentials, setCredentials } from '../lib/api.js';
  import Switch from '../lib/Switch.svelte';
  import { pushToast, updateToast, dismissToast } from '../lib/toast.js';

  const LOG_LEVELS = ['EMERG', 'ALERT', 'CRIT', 'ERR', 'WARNING', 'NOTICE', 'INFO', 'DEBUG', 'EDEBUG'];

  const SECTIONS = [
    {
      key: 'general',
      title: 'General',
      fields: [
        { name: 'log_level', label: 'Log level', type: 'select', options: LOG_LEVELS },
        { name: 'admin_user', label: 'Admin user', type: 'text', hint: 'Which source_auth entry is treated as the admin account.' },
      ],
    },
    {
      key: 'virtual',
      title: 'NEAR virtual base',
      fields: [
        { name: 'hysteresis_m', label: 'Hysteresis (meters)', type: 'number', step: 'any', hint: 'Distance a rover must move before NEAR reconsiders which base to assign.' },
        { name: 'max_nearest_lookup_distance_m', label: 'Max lookup distance (meters)', type: 'number', step: 'any', hint: 'Prunes the sourcetable to this radius when computing nearest bases.' },
        { name: 'nearest_base_count_target', label: 'Target base count', type: 'number', hint: 'Candidate base count NEAR aims for by adjusting the lookup distance.' },
        { name: 'min_nearest_recompute_interval', label: 'Min recompute interval (s)', type: 'number' },
        { name: 'max_nearest_recompute_interval', label: 'Max recompute interval (s)', type: 'number' },
        { name: 'min_nearest_recompute_pos_delta', label: 'Min recompute position delta (m)', type: 'number', step: 'any' },
      ],
    },
    {
      key: 'timeouts',
      title: 'Protocol timeouts',
      fields: [
        { name: 'source_read_timeout', label: 'Source read timeout (s)', type: 'number' },
        { name: 'ntripcli_default_read_timeout', label: 'NTRIP client read timeout (s)', type: 'number' },
        { name: 'ntripcli_default_write_timeout', label: 'NTRIP client write timeout (s)', type: 'number' },
        { name: 'ntripsrv_default_read_timeout', label: 'NTRIP server read timeout (s)', type: 'number' },
        { name: 'ntripsrv_default_write_timeout', label: 'NTRIP server write timeout (s)', type: 'number' },
      ],
    },
    {
      key: 'buffers',
      title: 'Buffers & limits',
      fields: [
        { name: 'backlog_socket', label: 'Socket send buffer (bytes)', type: 'number' },
        { name: 'backlog_evbuffer', label: 'Max client backlog (bytes)', type: 'number', hint: 'A client exceeding this backlog gets dropped.' },
        { name: 'http_header_max_size', label: 'Max HTTP header size (bytes)', type: 'number' },
        { name: 'http_content_length_max', label: 'Max HTTP content length (bytes)', type: 'number' },
        { name: 'min_raw_packet', label: 'Min raw packet size', type: 'number' },
        { name: 'max_raw_packet', label: 'Max raw packet size', type: 'number' },
      ],
    },
    {
      key: 'timing',
      title: 'Source timing',
      fields: [
        { name: 'idle_max_delay', label: 'Idle max delay (s)', type: 'number', hint: 'Seconds before closing a pulled on-demand source with no subscribers.' },
        { name: 'reconnect_delay', label: 'Reconnect delay (s)', type: 'number' },
        { name: 'on_demand_source_timeout', label: 'On-demand source timeout (s)', type: 'number' },
        { name: 'sourcetable_priority', label: 'Sourcetable priority', type: 'number' },
      ],
    },
    {
      key: 'paths',
      title: 'Files & paths',
      fields: [
        { name: 'host_auth_filename', label: 'Host auth file', type: 'text' },
        { name: 'source_auth_filename', label: 'Source auth file', type: 'text' },
        { name: 'blocklist_filename', label: 'Blocklist file', type: 'text' },
        { name: 'sourcetable_filename', label: 'Sourcetable file', type: 'text' },
        { name: 'sidecar_stats_filename', label: 'Sidecar stats file', type: 'text', hint: 'Written by the sidecar decoder; leave unset to disable the readiness check.' },
        { name: 'access_log', label: 'Access log', type: 'text' },
        { name: 'log', label: 'Log file', type: 'text' },
        { name: 'ui_dir', label: 'UI static files dir', type: 'text' },
      ],
    },
    {
      key: 'network',
      title: 'Network',
      fields: [
        { name: 'trusted_http_ip_header', label: 'Trusted HTTP IP header', type: 'text', hint: 'e.g. X-Forwarded-For, when running behind a reverse proxy.' },
      ],
    },
  ];

  let form = $state({});
  let savedForm = $state({});
  let error = $state('');
  let loading = $state(true);
  let savingSection = $state(null);

  let newPassword = $state('');
  let confirmPassword = $state('');
  let savingPassword = $state(false);

  function scrollToSection(id) {
    document.getElementById(id)?.scrollIntoView({ behavior: 'smooth', block: 'start' });
  }

  async function fetchSettings() {
    try {
      const res = await apiGet('settings');
      error = '';
      form = { ...res };
      savedForm = structuredClone(res);
    } catch (e) {
      error = e.message;
    } finally {
      loading = false;
    }
  }

  // Refresh only the given keys after a save, instead of a full
  // fetchSettings() that would clobber unsaved edits in other blocks.
  async function refreshAfterSave(keys) {
    const res = await apiGet('settings');
    for (const k of keys) {
      form[k] = res[k];
      savedForm[k] = structuredClone(res[k]);
    }
  }

  function sectionDirty(section) {
    return section.fields.some((f) => String(form[f.name] ?? '') !== String(savedForm[f.name] ?? ''));
  }

  let dirtyBlocks = $derived.by(() => {
    const blocks = [];
    if (form.alarms && JSON.stringify(form.alarms) !== JSON.stringify(savedForm.alarms)) blocks.push('Alarms');
    if (newPassword || confirmPassword) blocks.push('Admin password');
    for (const s of SECTIONS) if (sectionDirty(s)) blocks.push(s.title);
    return blocks;
  });

  let unsavedToastId = null;
  $effect(() => {
    if (dirtyBlocks.length === 0) {
      if (unsavedToastId != null) { dismissToast(unsavedToastId); unsavedToastId = null; }
      return;
    }
    const text = `Unsaved changes: ${dirtyBlocks.join(', ')}`;
    if (unsavedToastId == null) unsavedToastId = pushToast(text, 'warn', { sticky: true });
    else updateToast(unsavedToastId, text);
  });

  let mountpointNames = $state([]);

  async function fetchMountpointNames() {
    try {
      const tables = await apiGet('sourcetables');
      const local = tables.find((t) => t.host === 'LOCAL') ?? { mountpoints: {} };
      mountpointNames = Object.entries(local.mountpoints)
        .filter(([, mnt]) => !mnt.virtual)
        .map(([name]) => name);
    } catch (e) {
      // Non-critical; the filter form still works without autocomplete.
    }
  }

  async function saveSection(section) {
    savingSection = section.key;
    const payload = {};
    for (const f of section.fields) payload[f.name] = String(form[f.name] ?? '');
    try {
      const res = await apiPost('settings', payload);
      if (res.error) {
        // Validation failure; nothing was written, no need to resync.
        pushToast(res.error, 'error');
      } else {
        // Value is on disk before reload runs, so it's live even if reload
        // itself had trouble (e.g. reopening log files). Always resync.
        pushToast(res.result === 0 ? `${section.title} saved.` : 'Saved, but reload reported an issue -- showing current server value.');
        await refreshAfterSave(section.fields.map((f) => f.name));
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      savingSection = null;
    }
  }

  const TLS_OPTIONS = ['none', 'starttls', 'smtps'];

  const ALARM_TYPES = [
    { key: 'station_offline', label: 'Offline' },
    { key: 'station_online', label: 'Back online' },
    { key: 'low_sv_count', label: 'Low SV count' },
    { key: 'position_drift', label: 'Position drift' },
  ];

  // null alarm_types means "receives everything" (the default, unfiltered).
  function recipientWants(r, key) {
    return !r.alarm_types || r.alarm_types.includes(key);
  }

  function toggleRecipientType(r, key) {
    const current = r.alarm_types ?? ALARM_TYPES.map((t) => t.key);
    const next = current.includes(key) ? current.filter((k) => k !== key) : [...current, key];
    if (next.length === 0) {
      recipientMsg = 'A recipient must receive at least one alarm type -- use Remove to delete it instead.';
      return;
    }
    recipientMsg = '';
    r.alarm_types = next.length === ALARM_TYPES.length ? null : next;
  }

  const ALARM_TYPE_DEFAULTS = {
    station_offline: { after_minutes: 0 },
    station_online: { after_minutes: 0 },
    low_sv_count: { min_sats: 0, after_minutes: 0 },
    position_drift: { lat_mm: 0, lon_mm: 0, alt_mm: 0, after_minutes: 0 },
  };

  function toggleAlarmType(key) {
    form.alarms[key] = form.alarms[key] ? null : { ...ALARM_TYPE_DEFAULTS[key] };
  }

  async function saveAlarms() {
    savingSection = 'alarms';
    const a = form.alarms;
    const payload = {
      'alarms.subject': a.subject ?? '',
      'alarms.min_interval_minutes': String(a.min_interval_minutes ?? ''),
      'alarms.ruckus_path': a.ruckus_path ?? '',
      'alarms.email_template': a.email_template ?? '',
    };
    if (a.smtp) {
      payload['alarms.smtp.host'] = a.smtp.host ?? '';
      payload['alarms.smtp.port'] = String(a.smtp.port ?? '');
      payload['alarms.smtp.tls'] = a.smtp.tls ?? '';
      payload['alarms.smtp.auth_file'] = a.smtp.auth_file ?? '';
    }
    if (a.station_offline) payload['alarms.station_offline.after_minutes'] = String(a.station_offline.after_minutes ?? '');
    if (a.station_online) payload['alarms.station_online.after_minutes'] = String(a.station_online.after_minutes ?? '');
    if (a.low_sv_count) {
      payload['alarms.low_sv_count.min_sats'] = String(a.low_sv_count.min_sats ?? '');
      payload['alarms.low_sv_count.after_minutes'] = String(a.low_sv_count.after_minutes ?? '');
    }
    if (a.position_drift) {
      payload['alarms.position_drift.lat_mm'] = String(a.position_drift.lat_mm ?? '');
      payload['alarms.position_drift.lon_mm'] = String(a.position_drift.lon_mm ?? '');
      payload['alarms.position_drift.alt_mm'] = String(a.position_drift.alt_mm ?? '');
      payload['alarms.position_drift.after_minutes'] = String(a.position_drift.after_minutes ?? '');
    }
    for (const t of ALARM_TYPES) {
      const wasEnabled = !!savedForm.alarms?.[t.key];
      const isEnabled = !!a[t.key];
      if (isEnabled && !wasEnabled) payload[`alarms.${t.key}.enable`] = '1';
      else if (!isEnabled && wasEnabled) payload[`alarms.${t.key}.remove`] = '1';
    }
    payload['alarms.recipients.set'] = JSON.stringify((a.recipients ?? []).map((r) => ({
      email: r.email ?? '',
      name: r.name ?? '',
      alarm_types: (r.alarm_types ?? []).join(','),
    })));
    payload['alarms.mountpoints.set'] = JSON.stringify((a.mountpoints ?? []).map((m) => ({
      mountpoint: m.mountpoint,
      alarm_types: (m.alarm_types ?? []).join(','),
    })));

    try {
      const res = await apiPost('settings', payload);
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        pushToast(res.result === 0 ? 'Alarms saved.' : 'Saved, but reload reported an issue -- showing current server value.');
        await refreshAfterSave(['alarms']);
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      savingSection = null;
    }
  }

  let newRecipientName = $state('');
  let newRecipientEmail = $state('');
  let recipientMsg = $state('');

  function addRecipient() {
    if (!newRecipientEmail) { recipientMsg = 'Email is required.'; return; }
    recipientMsg = '';
    form.alarms.recipients = [...form.alarms.recipients, { name: newRecipientName || null, email: newRecipientEmail, alarm_types: null }];
    newRecipientName = '';
    newRecipientEmail = '';
  }

  function removeRecipient(i) {
    form.alarms.recipients = form.alarms.recipients.filter((_, idx) => idx !== i);
  }

  function mountpointFilterWants(m, key) {
    return (m.alarm_types ?? []).includes(key);
  }

  function toggleMountpointFilterType(m, key) {
    const current = m.alarm_types ?? [];
    m.alarm_types = current.includes(key) ? current.filter((k) => k !== key) : [...current, key];
  }

  let newMountpointName = $state('');
  let newMountpointTypes = $state([]);
  let mountpointFilterMsg = $state('');

  function toggleNewMountpointType(key) {
    newMountpointTypes = newMountpointTypes.includes(key)
      ? newMountpointTypes.filter((k) => k !== key)
      : [...newMountpointTypes, key];
  }

  function addMountpointFilter() {
    if (!newMountpointName) { mountpointFilterMsg = 'Mountpoint name is required.'; return; }
    if (form.alarms.mountpoints.some((m) => m.mountpoint === newMountpointName)) {
      mountpointFilterMsg = 'That mountpoint already has an alarm filter.';
      return;
    }
    mountpointFilterMsg = '';
    form.alarms.mountpoints = [...form.alarms.mountpoints, { mountpoint: newMountpointName, alarm_types: [...newMountpointTypes] }];
    newMountpointName = '';
    newMountpointTypes = [];
  }

  function removeMountpointFilter(mountpoint) {
    form.alarms.mountpoints = form.alarms.mountpoints.filter((m) => m.mountpoint !== mountpoint);
  }

  async function changePassword(e) {
    e.preventDefault();
    if (!newPassword) { pushToast('New password is required.', 'error'); return; }
    if (newPassword !== confirmPassword) { pushToast('Passwords do not match.', 'error'); return; }
    savingPassword = true;
    try {
      const adminUser = form.admin_user;
      const res = await apiPost('auth', { mountpoint: adminUser, auth_user: adminUser, auth_password: newPassword });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        pushToast('Password changed.');
        newPassword = '';
        confirmPassword = '';
        // Keep the current session's stored credentials in sync, since the
        // admin's own password just changed under it.
        const { user } = getCredentials();
        if (user === adminUser) setCredentials(user, res.password);
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      savingPassword = false;
    }
  }

  fetchSettings();
  fetchMountpointNames();
</script>

<div class="page">
  <div class="header">
    <h2>Settings</h2>
    <button onclick={fetchSettings}>Refresh</button>
  </div>

  {#if error}
    <p class="error">{error}</p>
  {:else if loading}
    <p class="loading">Loading...</p>
  {:else}
  <div class="settings-layout">
  <div class="settings-main">
    <div class="card" id="alarms">
      <h3>Alarms</h3>
      {#if !form.alarms}
        <p class="field-hint">Alarms are not configured. Add an <code>alarms:</code> block to caster.yaml to enable them.</p>
      {:else}
        <form onsubmit={(e) => { e.preventDefault(); saveAlarms(); }}>
          <h4 id="alarms-thresholds">Thresholds</h4>
          <div class="grid">
            {#if form.alarms.station_offline}
              <label>Station offline after (minutes) <input type="number" bind:value={form.alarms.station_offline.after_minutes} /></label>
            {/if}
            {#if form.alarms.station_online}
              <label>Station online after (minutes) <input type="number" bind:value={form.alarms.station_online.after_minutes} /></label>
            {/if}
            {#if form.alarms.low_sv_count}
              <label>Low SV min sats <input type="number" bind:value={form.alarms.low_sv_count.min_sats} /></label>
              <label>Low SV after (minutes) <input type="number" bind:value={form.alarms.low_sv_count.after_minutes} /></label>
            {/if}
            {#if form.alarms.position_drift}
              <label>Drift lat threshold (mm) <input type="number" bind:value={form.alarms.position_drift.lat_mm} /></label>
              <label>Drift lon threshold (mm) <input type="number" bind:value={form.alarms.position_drift.lon_mm} /></label>
              <label>Drift alt threshold (mm) <input type="number" bind:value={form.alarms.position_drift.alt_mm} /></label>
              <label>Drift after (minutes) <input type="number" bind:value={form.alarms.position_drift.after_minutes} /></label>
            {/if}
          </div>

          <h4 id="alarms-types">Alarm types</h4>
          <div class="alarm-type-list">
            {#each ALARM_TYPES as t (t.key)}
              <label class="switch-row">
                <span>{t.label}</span>
                <Switch checked={!!form.alarms[t.key]} onchange={() => toggleAlarmType(t.key)} />
              </label>
            {/each}
          </div>

          <h4 id="alarms-filters">Per-base alarm filters</h4>
          {#if form.alarms.mountpoints.length === 0}
            <p class="field-hint">No overrides - every mountpoint is checked for every alarm type.</p>
          {:else}
            <div class="recipient-list">
              {#each form.alarms.mountpoints as m, i (i)}
                <div class="recipient-row">
                  <span class="mountpoint-name">{m.mountpoint}</span>
                  <button type="button" class="remove-btn" onclick={() => removeMountpointFilter(m.mountpoint)}>Remove</button>
                </div>
                <div class="recipient-types">
                  <span class="field-hint">Checked for:</span>
                  {#each ALARM_TYPES as t (t.key)}
                    <label class="checkbox-label">
                      <input type="checkbox" checked={mountpointFilterWants(m, t.key)} onchange={() => toggleMountpointFilterType(m, t.key)} />
                      {t.label}
                    </label>
                  {/each}
                  {#if (m.alarm_types ?? []).length === 0}
                    <span class="field-hint">All alarms suppressed for this mountpoint.</span>
                  {/if}
                </div>
              {/each}
            </div>
          {/if}

          <div class="recipient-row recipient-add">
            <label>
              Mountpoint
              <input type="text" bind:value={newMountpointName} list="mountpoint-names" />
              <datalist id="mountpoint-names">
                {#each mountpointNames as name (name)}<option value={name}></option>{/each}
              </datalist>
            </label>
          </div>
          <div class="recipient-types">
            {#each ALARM_TYPES as t (t.key)}
              <label class="checkbox-label">
                <input type="checkbox" checked={newMountpointTypes.includes(t.key)} onchange={() => toggleNewMountpointType(t.key)} />
                {t.label}
              </label>
            {/each}
            <button type="button" onclick={addMountpointFilter}>+ Add filter</button>
          </div>
          {#if newMountpointTypes.length === 0}
            <p class="field-hint">No types checked -- this will suppress every alarm for that mountpoint.</p>
          {/if}
          {#if mountpointFilterMsg}<div class="msg">{mountpointFilterMsg}</div>{/if}

          <h4 id="alarms-recipients">Recipients</h4>
          {#if form.alarms.recipients.length === 0}
            <p class="field-hint">No recipients configured.</p>
          {:else}
            <div class="recipient-list">
              {#each form.alarms.recipients as r, i (i)}
                <div class="recipient-row">
                  <label>Name <input type="text" bind:value={r.name} /></label>
                  <label>Email <input type="text" bind:value={r.email} /></label>
                  <button type="button" class="remove-btn" onclick={() => removeRecipient(i)}>Remove</button>
                </div>
                <div class="recipient-types">
                  <span class="field-hint">Receives:</span>
                  {#each ALARM_TYPES as t (t.key)}
                    <label class="checkbox-label">
                      <input type="checkbox" checked={recipientWants(r, t.key)} onchange={() => toggleRecipientType(r, t.key)} />
                      {t.label}
                    </label>
                  {/each}
                </div>
              {/each}
            </div>
          {/if}

          <div class="recipient-row recipient-add">
            <label>New name (optional) <input type="text" bind:value={newRecipientName} /></label>
            <label>New email <input type="text" bind:value={newRecipientEmail} /></label>
            <button type="button" onclick={addRecipient}>+ Add recipient</button>
          </div>
          {#if recipientMsg}<div class="msg">{recipientMsg}</div>{/if}

          <div class="grid">
            <label>Subject <input type="text" bind:value={form.alarms.subject} /></label>
            <label>Min interval between sends (minutes) <input type="number" bind:value={form.alarms.min_interval_minutes} /></label>
          </div>

          <div class="grid">
            <label>Ruckus binary path <input type="text" bind:value={form.alarms.ruckus_path} /></label>
            <label>Email template path <input type="text" bind:value={form.alarms.email_template} /></label>
          </div>

          {#if form.alarms.smtp}
            <h4 id="alarms-smtp">SMTP</h4>
            <div class="grid">
              <label>Host <input type="text" bind:value={form.alarms.smtp.host} /></label>
              <label>Port <input type="number" bind:value={form.alarms.smtp.port} /></label>
              <label>
                TLS
                <select bind:value={form.alarms.smtp.tls}>
                  {#each TLS_OPTIONS as opt}<option value={opt}>{opt}</option>{/each}
                </select>
              </label>
              <label>
                Auth file <input type="text" bind:value={form.alarms.smtp.auth_file} />
                <span class="field-hint">Credentials are managed on the Auth page.</span>
              </label>
            </div>
          {/if}

          <div class="card-actions">
            <button type="submit" disabled={savingSection === 'alarms'}>{savingSection === 'alarms' ? 'Saving...' : 'Save'}</button>
          </div>
        </form>
      {/if}
    </div>

    <div class="card" id="admin-password">
      <h3>Admin password</h3>
      <form onsubmit={changePassword}>
        <div class="grid">
          <label>New password <input type="password" bind:value={newPassword} /></label>
          <label>Confirm password <input type="password" bind:value={confirmPassword} /></label>
        </div>
        <div class="card-actions">
          <button type="submit" disabled={savingPassword}>{savingPassword ? 'Saving...' : 'Change password'}</button>
        </div>
      </form>
    </div>

    {#each SECTIONS as section (section.key)}
      <div class="card" id="section-{section.key}">
        <h3>{section.title}</h3>
        <form onsubmit={(e) => { e.preventDefault(); saveSection(section); }}>
          <div class="grid">
            {#each section.fields as f (f.name)}
              <label>
                {f.label}
                {#if f.type === 'select'}
                  <select bind:value={form[f.name]}>
                    {#each f.options as opt}<option value={opt}>{opt}</option>{/each}
                  </select>
                {:else}
                  <input type={f.type} step={f.step} bind:value={form[f.name]} />
                {/if}
                {#if f.hint}<span class="field-hint">{f.hint}</span>{/if}
              </label>
            {/each}
          </div>
          <div class="card-actions">
            <button type="submit" disabled={savingSection === section.key}>
              {savingSection === section.key ? 'Saving...' : 'Save'}
            </button>
          </div>
        </form>
      </div>
    {/each}
  </div>

  <nav class="settings-nav">
    <button type="button" onclick={() => scrollToSection('alarms')}>Alarms</button>
    {#if form.alarms}
      <button type="button" class="sub" onclick={() => scrollToSection('alarms-thresholds')}>Thresholds</button>
      <button type="button" class="sub" onclick={() => scrollToSection('alarms-types')}>Alarm types</button>
      <button type="button" class="sub" onclick={() => scrollToSection('alarms-filters')}>Per-base filters</button>
      <button type="button" class="sub" onclick={() => scrollToSection('alarms-recipients')}>Recipients</button>
      {#if form.alarms.smtp}<button type="button" class="sub" onclick={() => scrollToSection('alarms-smtp')}>SMTP</button>{/if}
    {/if}
    <button type="button" onclick={() => scrollToSection('admin-password')}>Admin password</button>
    {#each SECTIONS as section (section.key)}
      <button type="button" onclick={() => scrollToSection(`section-${section.key}`)}>{section.title}</button>
    {/each}
  </nav>
  </div>
  {/if}
</div>

<style>
  .page {
    padding: 2rem;
    max-width: 980px;
  }

  .settings-layout {
    display: flex;
    align-items: flex-start;
    gap: 2rem;
  }

  .settings-main {
    flex: 1;
    min-width: 0;
    max-width: 760px;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }

  .settings-nav {
    position: sticky;
    top: 1.5rem;
    flex-shrink: 0;
    width: 150px;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    font-size: 0.8rem;
  }

  .settings-nav button {
    cursor: pointer;
    text-align: left;
    background: none !important;
    border: none !important;
    border-radius: 0 !important;
    padding: 0 !important;
    color: var(--text-muted) !important;
    text-transform: none !important;
    letter-spacing: normal !important;
    font-size: 0.8rem !important;
  }

  .settings-nav button:hover {
    color: var(--text) !important;
  }

  .settings-nav button.sub {
    padding-left: 0.85rem !important;
    font-size: 0.75rem !important;
    color: var(--text-dim) !important;
  }

  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 1.5rem;
  }

  h2 {
    margin: 0;
    color: var(--text);
    font-size: 1.2rem;
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

  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 1.25rem;
  }

  .card h3 {
    margin: 0 0 1rem;
    font-size: 0.95rem;
    color: var(--text);
  }

  .card h4 {
    margin: 1.25rem 0 0.75rem;
    font-size: 0.8rem;
    font-weight: 600;
    letter-spacing: 0.02em;
    text-transform: uppercase;
    color: var(--text-dim);
  }

  .card code {
    background: #12141c;
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 0.1rem 0.35rem;
    font-size: 0.85em;
  }

  .recipient-list {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
  }

  .recipient-row {
    display: flex;
    align-items: flex-end;
    gap: 0.75rem;
  }

  .recipient-row label {
    flex: 1;
    min-width: 0;
  }

  .recipient-row button {
    flex-shrink: 0;
  }

  .mountpoint-name {
    flex: 1;
    min-width: 0;
    font-family: monospace;
    font-variant-numeric: tabular-nums;
    color: var(--text);
    align-self: center;
  }

  .remove-btn {
    background: #3a1414;
    border-color: #7f1d1d;
    color: #fca5a5;
  }

  .remove-btn:hover:not(:disabled) {
    background: #4a1818;
  }

  .recipient-add {
    margin-top: 0.75rem;
    padding-top: 0.75rem;
    border-top: 1px solid #1e2130;
  }

  .recipient-types {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.75rem;
    margin: -0.15rem 0 0.4rem;
    padding-left: 0.1rem;
  }

  .checkbox-label {
    display: inline-flex;
    flex-direction: row;
    align-items: center;
    gap: 0.35rem;
    font-size: 0.78rem;
    color: var(--text-muted);
  }

  .checkbox-label input[type='checkbox'] {
    width: auto;
    accent-color: var(--accent);
  }

  .alarm-type-list {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
  }

  .switch-row {
    display: flex;
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
    gap: 0.75rem;
    padding: 0.4rem 0;
    color: var(--text);
    font-size: 0.9rem;
    cursor: pointer;
  }

  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
    gap: 0.85rem;
  }

  label {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
    font-size: 0.78rem;
    color: var(--text-dim);
  }

  input, select {
    padding: 0.4rem 0.6rem;
    background: #12141c;
    border: 1px solid var(--border);
    border-radius: 5px;
    color: var(--text);
    font-size: 0.85rem;
    font-family: inherit;
  }

  input:focus, select:focus {
    outline: none;
    border-color: var(--accent);
  }

  .field-hint {
    font-size: 0.72rem;
    color: var(--text-dim);
  }

  .card-actions {
    display: flex;
    align-items: center;
    gap: 1rem;
    margin-top: 1rem;
  }

  .msg {
    font-size: 0.85rem;
    color: var(--text-muted);
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
