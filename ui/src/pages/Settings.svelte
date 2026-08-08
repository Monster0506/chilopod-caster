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

  async function saveSection(section) {
    savingSection = section.key;
    sectionMsg = { ...sectionMsg, [section.key]: '' };
    const payload = {};
    for (const f of section.fields) payload[f.name] = String(form[f.name] ?? '');
    try {
      const res = await apiPost('settings', payload);
      if (res.result === 0) {
        sectionMsg = { ...sectionMsg, [section.key]: 'Saved.' };
        await fetchSettings();
      } else {
        sectionMsg = { ...sectionMsg, [section.key]: res.error ?? 'Failed to save.' };
      }
    } catch (e) {
      sectionMsg = { ...sectionMsg, [section.key]: `Failed: ${e.message}` };
    } finally {
      savingSection = null;
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
