<script>
  import { apiGet, apiPost, getCredentials, setCredentials } from '../lib/api.js';

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
  let error = $state('');
  let loading = $state(true);
  let savingSection = $state(null);
  let sectionMsg = $state({});

  let newPassword = $state('');
  let confirmPassword = $state('');
  let savingPassword = $state(false);
  let passwordMsg = $state('');

  async function fetchSettings() {
    try {
      const res = await apiGet('settings');
      error = '';
      form = { ...res };
    } catch (e) {
      error = e.message;
    } finally {
      loading = false;
    }
  }

  let mountpointNames = $state([]);

  async function fetchMountpointNames() {
    try {
      const tables = await apiGet('sourcetables');
      const local = tables.find((t) => t.host === 'LOCAL') ?? { mountpoints: {} };
      mountpointNames = Object.entries(local.mountpoints)
        .filter(([, mnt]) => !mnt.virtual)
        .map(([name]) => name);
    } catch (e) {
      // Non-critical -- the filter form still works without autocomplete.
    }
  }

  async function saveSection(section) {
    savingSection = section.key;
    sectionMsg = { ...sectionMsg, [section.key]: '' };
    const payload = {};
    for (const f of section.fields) payload[f.name] = String(form[f.name] ?? '');
    try {
      const res = await apiPost('settings', payload);
      if (res.error) {
        // Validation failure -- nothing was written, no need to resync.
        sectionMsg = { ...sectionMsg, [section.key]: res.error };
      } else {
        // The value is written to disk before reload runs, so it's already
        // live even if reload itself (res.result !== 0) had trouble with an
        // unrelated step (e.g. reopening log files). Always resync to show
        // the server's actual current value rather than assuming failure.
        sectionMsg = {
          ...sectionMsg,
          [section.key]: res.result === 0 ? 'Saved.' : 'Saved, but reload reported an issue -- showing current server value.',
        };
        await fetchSettings();
      }
    } catch (e) {
      sectionMsg = { ...sectionMsg, [section.key]: `Failed: ${e.message}` };
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

  let togglingAlarmType = $state(null);
  let alarmTypeMsg = $state('');

  async function enableAlarmType(key) {
    togglingAlarmType = key;
    alarmTypeMsg = '';
    try {
      const res = await apiPost('settings', { [`alarms.${key}.enable`]: '1' });
      if (res.error) {
        alarmTypeMsg = res.error;
      } else {
        await fetchSettings();
      }
    } catch (e) {
      alarmTypeMsg = `Failed: ${e.message}`;
    } finally {
      togglingAlarmType = null;
    }
  }

  async function disableAlarmType(key) {
    togglingAlarmType = key;
    alarmTypeMsg = '';
    try {
      const res = await apiPost('settings', { [`alarms.${key}.remove`]: '1' });
      if (res.error) {
        alarmTypeMsg = res.error;
      } else {
        await fetchSettings();
      }
    } catch (e) {
      alarmTypeMsg = `Failed: ${e.message}`;
    } finally {
      togglingAlarmType = null;
    }
  }

  async function saveAlarms() {
    savingSection = 'alarms';
    sectionMsg = { ...sectionMsg, alarms: '' };
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
    // Only touch a recipient's name if it's actually set - an unset
    // (optional) name has no key to overwrite in the config file yet.
    (a.recipients ?? []).forEach((r, i) => {
      if (r.name) payload[`alarms.recipients[${i}].name`] = r.name;
      payload[`alarms.recipients[${i}].email`] = r.email ?? '';
      payload[`alarms.recipients[${i}].alarm_types`] = (r.alarm_types ?? []).join(',');
    });
    (a.mountpoints ?? []).forEach((m, i) => {
      payload[`alarms.mountpoints[${i}].alarm_types`] = (m.alarm_types ?? []).join(',');
    });

    try {
      const res = await apiPost('settings', payload);
      if (res.error) {
        sectionMsg = { ...sectionMsg, alarms: res.error };
      } else {
        sectionMsg = {
          ...sectionMsg,
          alarms: res.result === 0 ? 'Saved.' : 'Saved, but reload reported an issue -- showing current server value.',
        };
        await fetchSettings();
      }
    } catch (e) {
      sectionMsg = { ...sectionMsg, alarms: `Failed: ${e.message}` };
    } finally {
      savingSection = null;
    }
  }

  let newRecipientName = $state('');
  let newRecipientEmail = $state('');
  let addingRecipient = $state(false);
  let removingIndex = $state(null);
  let recipientMsg = $state('');

  async function addRecipient() {
    if (!newRecipientEmail) { recipientMsg = 'Email is required.'; return; }
    addingRecipient = true;
    recipientMsg = '';
    try {
      const payload = { 'alarms.recipients.add.email': newRecipientEmail };
      if (newRecipientName) payload['alarms.recipients.add.name'] = newRecipientName;
      const res = await apiPost('settings', payload);
      if (res.error) {
        recipientMsg = res.error;
      } else {
        newRecipientName = '';
        newRecipientEmail = '';
        await fetchSettings();
      }
    } catch (e) {
      recipientMsg = `Failed: ${e.message}`;
    } finally {
      addingRecipient = false;
    }
  }

  async function removeRecipient(i) {
    removingIndex = i;
    recipientMsg = '';
    try {
      const res = await apiPost('settings', { 'alarms.recipients.remove': String(i) });
      if (res.error) {
        recipientMsg = res.error;
      } else {
        await fetchSettings();
      }
    } catch (e) {
      recipientMsg = `Failed: ${e.message}`;
    } finally {
      removingIndex = null;
    }
  }

  function mountpointFilterWants(m, key) {
    return (m.alarm_types ?? []).includes(key);
  }

  function toggleMountpointFilterType(m, key) {
    const current = m.alarm_types ?? [];
    const next = current.includes(key) ? current.filter((k) => k !== key) : [...current, key];
    if (next.length === 0) {
      mountpointFilterMsg = 'A mountpoint filter must keep at least one alarm type checked -- use Remove to delete it instead.';
      return;
    }
    mountpointFilterMsg = '';
    m.alarm_types = next;
  }

  let newMountpointName = $state('');
  let newMountpointTypes = $state([]);
  let addingMountpointFilter = $state(false);
  let removingMountpointFilter = $state(null);
  let mountpointFilterMsg = $state('');

  function toggleNewMountpointType(key) {
    newMountpointTypes = newMountpointTypes.includes(key)
      ? newMountpointTypes.filter((k) => k !== key)
      : [...newMountpointTypes, key];
  }

  async function addMountpointFilter() {
    if (!newMountpointName) { mountpointFilterMsg = 'Mountpoint name is required.'; return; }
    if (newMountpointTypes.length === 0) { mountpointFilterMsg = 'Select at least one alarm type.'; return; }
    addingMountpointFilter = true;
    mountpointFilterMsg = '';
    try {
      const res = await apiPost('settings', {
        'alarms.mountpoints.add.mountpoint': newMountpointName,
        'alarms.mountpoints.add.alarm_types': newMountpointTypes.join(','),
      });
      if (res.error) {
        mountpointFilterMsg = res.error;
      } else {
        newMountpointName = '';
        newMountpointTypes = [];
        await fetchSettings();
      }
    } catch (e) {
      mountpointFilterMsg = `Failed: ${e.message}`;
    } finally {
      addingMountpointFilter = false;
    }
  }

  async function removeMountpointFilter(mountpoint) {
    removingMountpointFilter = mountpoint;
    mountpointFilterMsg = '';
    try {
      const res = await apiPost('settings', { 'alarms.mountpoints.remove': mountpoint });
      if (res.error) {
        mountpointFilterMsg = res.error;
      } else {
        await fetchSettings();
      }
    } catch (e) {
      mountpointFilterMsg = `Failed: ${e.message}`;
    } finally {
      removingMountpointFilter = null;
    }
  }

  async function changePassword(e) {
    e.preventDefault();
    passwordMsg = '';
    if (!newPassword) { passwordMsg = 'New password is required.'; return; }
    if (newPassword !== confirmPassword) { passwordMsg = 'Passwords do not match.'; return; }
    savingPassword = true;
    try {
      const adminUser = form.admin_user;
      const res = await apiPost('auth', { mountpoint: adminUser, auth_user: adminUser, auth_password: newPassword });
      if (res.error) {
        passwordMsg = res.error;
      } else {
        passwordMsg = 'Password changed.';
        newPassword = '';
        confirmPassword = '';
        // Keep the current session's stored credentials in sync, since the
        // admin's own password just changed under it.
        const { user } = getCredentials();
        if (user === adminUser) setCredentials(user, res.password);
      }
    } catch (e) {
      passwordMsg = `Failed: ${e.message}`;
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
    <p class="loading">Loading…</p>
  {:else}
    <div class="card">
      <h3>Alarms</h3>
      {#if !form.alarms}
        <p class="field-hint">Alarms are not configured. Add an <code>alarms:</code> block to caster.yaml to enable them.</p>
      {:else}
        <form onsubmit={(e) => { e.preventDefault(); saveAlarms(); }}>
          <h4>Thresholds</h4>
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

          <h4>Alarm types</h4>
          <div class="recipient-list">
            {#each ALARM_TYPES as t (t.key)}
              <div class="recipient-row">
                <span>{t.label}</span>
                {#if form.alarms[t.key]}
                  <button type="button" class="remove-btn" onclick={() => disableAlarmType(t.key)} disabled={togglingAlarmType === t.key}>
                    {togglingAlarmType === t.key ? 'Disabling…' : 'Disable'}
                  </button>
                {:else}
                  <button type="button" onclick={() => enableAlarmType(t.key)} disabled={togglingAlarmType === t.key}>
                    {togglingAlarmType === t.key ? 'Enabling…' : 'Enable'}
                  </button>
                {/if}
              </div>
            {/each}
          </div>
          {#if alarmTypeMsg}<div class="msg">{alarmTypeMsg}</div>{/if}

          <h4>Per-base alarm filters</h4>
          {#if form.alarms.mountpoints.length === 0}
            <p class="field-hint">No overrides - every mountpoint is checked for every alarm type.</p>
          {:else}
            <div class="recipient-list">
              {#each form.alarms.mountpoints as m, i (i)}
                <div class="recipient-row">
                  <span class="mountpoint-name">{m.mountpoint}</span>
                  <button type="button" class="remove-btn" onclick={() => removeMountpointFilter(m.mountpoint)} disabled={removingMountpointFilter === m.mountpoint}>
                    {removingMountpointFilter === m.mountpoint ? 'Removing…' : 'Remove'}
                  </button>
                </div>
                <div class="recipient-types">
                  <span class="field-hint">Checked for:</span>
                  {#each ALARM_TYPES as t (t.key)}
                    <label class="checkbox-label">
                      <input type="checkbox" checked={mountpointFilterWants(m, t.key)} onchange={() => toggleMountpointFilterType(m, t.key)} />
                      {t.label}
                    </label>
                  {/each}
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
            <button type="button" onclick={addMountpointFilter} disabled={addingMountpointFilter}>{addingMountpointFilter ? 'Adding…' : '+ Add filter'}</button>
          </div>
          {#if mountpointFilterMsg}<div class="msg">{mountpointFilterMsg}</div>{/if}

          <h4>Recipients</h4>
          {#if form.alarms.recipients.length === 0}
            <p class="field-hint">No recipients configured.</p>
          {:else}
            <div class="recipient-list">
              {#each form.alarms.recipients as r, i (i)}
                <div class="recipient-row">
                  <label>Name <input type="text" bind:value={r.name} /></label>
                  <label>Email <input type="text" bind:value={r.email} /></label>
                  <button type="button" class="remove-btn" onclick={() => removeRecipient(i)} disabled={removingIndex === i}>
                    {removingIndex === i ? 'Removing…' : 'Remove'}
                  </button>
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
            <button type="button" onclick={addRecipient} disabled={addingRecipient}>{addingRecipient ? 'Adding…' : '+ Add recipient'}</button>
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
            <h4>SMTP</h4>
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
            <button type="submit" disabled={savingSection === 'alarms'}>{savingSection === 'alarms' ? 'Saving…' : 'Save'}</button>
            {#if sectionMsg.alarms}<span class="msg">{sectionMsg.alarms}</span>{/if}
          </div>
        </form>
      {/if}
    </div>

    <div class="card">
      <h3>Admin password</h3>
      <form onsubmit={changePassword}>
        <div class="grid">
          <label>New password <input type="password" bind:value={newPassword} /></label>
          <label>Confirm password <input type="password" bind:value={confirmPassword} /></label>
        </div>
        <div class="card-actions">
          <button type="submit" disabled={savingPassword}>{savingPassword ? 'Saving…' : 'Change password'}</button>
          {#if passwordMsg}<span class="msg">{passwordMsg}</span>{/if}
        </div>
      </form>
    </div>

    {#each SECTIONS as section (section.key)}
      <div class="card">
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
              {savingSection === section.key ? 'Saving…' : 'Save'}
            </button>
            {#if sectionMsg[section.key]}<span class="msg">{sectionMsg[section.key]}</span>{/if}
          </div>
        </form>
      </div>
    {/each}
  {/if}
</div>

<style>
  .page {
    padding: 2rem;
    max-width: 760px;
  }

  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 1.5rem;
  }

  h2 {
    margin: 0;
    color: #e2e8f0;
    font-size: 1.2rem;
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

  .card {
    background: #1a1d27;
    border: 1px solid #2a2d3a;
    border-radius: 8px;
    padding: 1.25rem;
    margin-bottom: 1.25rem;
  }

  .card h3 {
    margin: 0 0 1rem;
    font-size: 0.95rem;
    color: #e2e8f0;
  }

  .card h4 {
    margin: 1.25rem 0 0.75rem;
    font-size: 0.8rem;
    font-weight: 600;
    letter-spacing: 0.02em;
    text-transform: uppercase;
    color: #64748b;
  }

  .card code {
    background: #12141c;
    border: 1px solid #2a2d3a;
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
    color: #e2e8f0;
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
    color: #94a3b8;
  }

  .checkbox-label input[type='checkbox'] {
    width: auto;
    accent-color: #2563eb;
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
    color: #64748b;
  }

  input, select {
    padding: 0.4rem 0.6rem;
    background: #12141c;
    border: 1px solid #2a2d3a;
    border-radius: 5px;
    color: #e2e8f0;
    font-size: 0.85rem;
    font-family: inherit;
  }

  input:focus, select:focus {
    outline: none;
    border-color: #2563eb;
  }

  .field-hint {
    font-size: 0.72rem;
    color: #475569;
  }

  .card-actions {
    display: flex;
    align-items: center;
    gap: 1rem;
    margin-top: 1rem;
  }

  .msg {
    font-size: 0.85rem;
    color: #94a3b8;
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
