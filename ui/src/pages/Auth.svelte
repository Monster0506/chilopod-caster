<script>
  import { apiGet, apiPost } from '../lib/api.js';
  import RevealSecret from '../lib/RevealSecret.svelte';
  import Switch from '../lib/Switch.svelte';
  import { pushToast } from '../lib/toast.js';

  let form = $state({});
  let error = $state('');
  let loading = $state(true);

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

  let roverAuthFilenameInput = $state('');
  let savingRoverAuthFilename = $state(false);
  let newRoverUser = $state('');
  let newRoverPassword = $state('');
  let addingRoverAccount = $state(false);
  let removingRoverUser = $state(null);
  let roverPasswordInputs = $state({});
  let settingRoverPasswordUser = $state(null);

  async function enableRoverAuth() {
    if (!roverAuthFilenameInput) { pushToast('Filename is required.', 'error'); return; }
    savingRoverAuthFilename = true;
    try {
      const res = await apiPost('settings', { rover_auth_filename: roverAuthFilenameInput });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        pushToast('Enabled. Add at least one account below - until then, every rover request is rejected.');
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      savingRoverAuthFilename = false;
    }
  }

  async function addRoverAccount() {
    if (!newRoverUser || !newRoverPassword) { pushToast('Username and password are both required.', 'error'); return; }
    addingRoverAccount = true;
    try {
      const res = await apiPost('settings', { 'rover_auth.add.user': newRoverUser, 'rover_auth.add.password': newRoverPassword });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        newRoverUser = '';
        newRoverPassword = '';
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      addingRoverAccount = false;
    }
  }

  async function removeRoverAccount(user) {
    removingRoverUser = user;
    try {
      const res = await apiPost('settings', { 'rover_auth.remove': user });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      removingRoverUser = null;
    }
  }

  async function toggleRoverAccount(account) {
    const next = !account.enabled;
    try {
      const res = await apiPost('settings', { 'rover_auth.set_enabled.user': account.user, 'rover_auth.set_enabled.value': next ? 'Y' : 'N' });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    }
  }

  async function setRoverPassword(user) {
    const value = roverPasswordInputs[user];
    if (!value) return;
    settingRoverPasswordUser = user;
    try {
      const res = await apiPost('settings', { 'rover_auth.set_password.user': user, 'rover_auth.set_password.value': value });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        roverPasswordInputs = { ...roverPasswordInputs, [user]: '' };
        pushToast(`Password updated for ${user}.`);
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      settingRoverPasswordUser = null;
    }
  }

  let newSmtpAuthHost = $state('');
  let newSmtpAuthUser = $state('');
  let newSmtpAuthPassword = $state('');
  let savingSmtpAuth = $state(false);
  let removingSmtpAuthHost = $state(null);

  async function setSmtpAuthCredential() {
    if (!newSmtpAuthHost || !newSmtpAuthUser || !newSmtpAuthPassword) {
      pushToast('Host, username and password are all required.', 'error');
      return;
    }
    savingSmtpAuth = true;
    try {
      const res = await apiPost('settings', {
        'alarms.smtp_auth.set.host': newSmtpAuthHost,
        'alarms.smtp_auth.set.user': newSmtpAuthUser,
        'alarms.smtp_auth.set.password': newSmtpAuthPassword,
      });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        newSmtpAuthHost = '';
        newSmtpAuthUser = '';
        newSmtpAuthPassword = '';
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      savingSmtpAuth = false;
    }
  }

  async function removeSmtpAuthCredential(host) {
    removingSmtpAuthHost = host;
    try {
      const res = await apiPost('settings', { 'alarms.smtp_auth.remove': host });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      removingSmtpAuthHost = null;
    }
  }

  const ROLES = [
    { key: 'viewer', label: 'Viewer' },
    { key: 'admin', label: 'Admin' },
  ];

  let userAuthFilenameInput = $state('');
  let savingUserAuthFilename = $state(false);
  let newConsoleUser = $state('');
  let newConsolePassword = $state('');
  let newConsoleRole = $state('viewer');
  let addingConsoleUser = $state(false);
  let removingConsoleUser = $state(null);
  let consolePasswordInputs = $state({});
  let settingConsolePasswordUser = $state(null);

  async function enableUserAuth() {
    if (!userAuthFilenameInput) { pushToast('Filename is required.', 'error'); return; }
    savingUserAuthFilename = true;
    try {
      const res = await apiPost('settings', { user_auth_filename: userAuthFilenameInput });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        pushToast('Enabled. Add at least one admin or viewer account below.');
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      savingUserAuthFilename = false;
    }
  }

  async function addConsoleUser() {
    if (!newConsoleUser || !newConsolePassword) { pushToast('Username and password are both required.', 'error'); return; }
    addingConsoleUser = true;
    try {
      const res = await apiPost('settings', {
        'user_auth.add.user': newConsoleUser,
        'user_auth.add.password': newConsolePassword,
        'user_auth.add.role': newConsoleRole,
      });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        newConsoleUser = '';
        newConsolePassword = '';
        newConsoleRole = 'viewer';
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      addingConsoleUser = false;
    }
  }

  async function removeConsoleUser(user) {
    removingConsoleUser = user;
    try {
      const res = await apiPost('settings', { 'user_auth.remove': user });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      removingConsoleUser = null;
    }
  }

  async function toggleConsoleUser(account) {
    const next = !account.enabled;
    try {
      const res = await apiPost('settings', { 'user_auth.set_enabled.user': account.user, 'user_auth.set_enabled.value': next ? 'Y' : 'N' });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    }
  }

  async function setConsoleRole(user, role) {
    try {
      const res = await apiPost('settings', { 'user_auth.set_role.user': user, 'user_auth.set_role.value': role });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        await fetchSettings();
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    }
  }

  async function setConsolePassword(user) {
    const value = consolePasswordInputs[user];
    if (!value) return;
    settingConsolePasswordUser = user;
    try {
      const res = await apiPost('settings', { 'user_auth.set_password.user': user, 'user_auth.set_password.value': value });
      if (res.error) {
        pushToast(res.error, 'error');
      } else {
        consolePasswordInputs = { ...consolePasswordInputs, [user]: '' };
        pushToast(`Password updated for ${user}.`);
      }
    } catch (e) {
      pushToast(`Failed: ${e.message}`, 'error');
    } finally {
      settingConsolePasswordUser = null;
    }
  }

  fetchSettings();
</script>

<div class="page">
  <div class="header">
    <h2>Auth</h2>
    <button onclick={fetchSettings}>Refresh</button>
  </div>

  {#if error}
    <p class="error">{error}</p>
  {:else if loading}
    <p class="loading">Loading…</p>
  {:else}
    <div class="card">
      <h3>Rover Authentication</h3>
      {#if !form.rover_auth?.configured}
        <p class="field-hint">Not configured. RTCM streams are open to any client with no credentials. Set an auth file to require a matching username/password.</p>
        <div class="grid">
          <label>Rover auth file <input type="text" bind:value={roverAuthFilenameInput} placeholder="rover.auth" /></label>
        </div>
        <div class="card-actions">
          <button type="button" onclick={enableRoverAuth} disabled={savingRoverAuthFilename}>
            {savingRoverAuthFilename ? 'Enabling…' : 'Enable'}
          </button>
        </div>
      {:else}
        <p class="field-hint">Auth file: <code>{form.rover_auth.filename}</code></p>

        {#if form.rover_auth.accounts.length > 0}
          <div class="recipient-list">
            {#each form.rover_auth.accounts as a (a.user)}
              <div class="recipient-row">
                <label class="checkbox-label">
                  <Switch checked={a.enabled} onchange={() => toggleRoverAccount(a)} />
                  {a.user}
                </label>
                <span class="mono">
                  <RevealSecret value={a.password} />
                </span>
                <label>New password <input type="text" bind:value={roverPasswordInputs[a.user]} placeholder="(unchanged)" /></label>
                <button type="button" onclick={() => setRoverPassword(a.user)} disabled={!roverPasswordInputs[a.user] || settingRoverPasswordUser === a.user}>
                  {settingRoverPasswordUser === a.user ? 'Saving…' : 'Set password'}
                </button>
                <button type="button" class="remove-btn" onclick={() => removeRoverAccount(a.user)} disabled={removingRoverUser === a.user}>
                  {removingRoverUser === a.user ? 'Removing…' : 'Remove'}
                </button>
              </div>
            {/each}
          </div>
        {/if}

        <div class="recipient-row recipient-add">
          <label>New username <input type="text" bind:value={newRoverUser} /></label>
          <label>New password <input type="text" bind:value={newRoverPassword} /></label>
          <button type="button" onclick={addRoverAccount} disabled={addingRoverAccount}>{addingRoverAccount ? 'Adding…' : '+ Add account'}</button>
        </div>
      {/if}
    </div>

    <div class="card">
      <h3>SMTP Credentials</h3>
      {#if !form.alarms?.smtp}
        <p class="field-hint">SMTP is not configured. Add an <code>alarms.smtp</code> block to caster.yaml to enable email alerts.</p>
      {:else}
        <p class="field-hint">Auth file: <code>{form.alarms.smtp.auth_file}</code></p>
        {#if form.alarms.smtp.credentials.length === 0}
          <p class="field-hint">No credentials on file. Sends go out unauthenticated unless you add one.</p>
        {:else}
          <div class="recipient-list">
            {#each form.alarms.smtp.credentials as c (c.host)}
              <div class="recipient-row">
                <span class="mountpoint-name">{c.host}</span>
                <span class="field-hint">{c.user}</span>
                <span class="mono">
                  <RevealSecret value={c.password} />
                </span>
                <button type="button" class="remove-btn" onclick={() => removeSmtpAuthCredential(c.host)} disabled={removingSmtpAuthHost === c.host}>
                  {removingSmtpAuthHost === c.host ? 'Removing…' : 'Remove'}
                </button>
              </div>
            {/each}
          </div>
        {/if}

        <div class="recipient-row recipient-add">
          <label>Host <input type="text" bind:value={newSmtpAuthHost} placeholder={form.alarms.smtp.host} /></label>
          <label>Username <input type="text" bind:value={newSmtpAuthUser} /></label>
          <label>Password <input type="password" bind:value={newSmtpAuthPassword} /></label>
          <button type="button" onclick={setSmtpAuthCredential} disabled={savingSmtpAuth}>{savingSmtpAuth ? 'Saving…' : 'Set credential'}</button>
        </div>
        <span class="field-hint">Setting a host that already has a credential replaces it.</span>
      {/if}
    </div>

    <div class="card">
      <h3>Console Users</h3>
      {#if !form.user_auth?.configured}
        <p class="field-hint">Not configured. Only the single <code>admin_user</code> account can sign in. Set an auth file to add more admin or view-only accounts.</p>
        <div class="grid">
          <label>Console auth file <input type="text" bind:value={userAuthFilenameInput} placeholder="user.auth" /></label>
        </div>
        <div class="card-actions">
          <button type="button" onclick={enableUserAuth} disabled={savingUserAuthFilename}>
            {savingUserAuthFilename ? 'Enabling…' : 'Enable'}
          </button>
        </div>
      {:else}
        <p class="field-hint">Auth file: <code>{form.user_auth.filename}</code></p>

        {#if form.user_auth.accounts.length > 0}
          <div class="recipient-list">
            {#each form.user_auth.accounts as a (a.user)}
              <div class="recipient-row">
                <label class="checkbox-label">
                  <Switch checked={a.enabled} onchange={() => toggleConsoleUser(a)} />
                  {a.user}
                </label>
                <span class="mono">
                  <RevealSecret value={a.password} />
                </span>
                <label>
                  Role
                  <select value={a.role} onchange={(e) => setConsoleRole(a.user, e.currentTarget.value)}>
                    {#each ROLES as r (r.key)}<option value={r.key}>{r.label}</option>{/each}
                  </select>
                </label>
                <label>New password <input type="text" bind:value={consolePasswordInputs[a.user]} placeholder="(unchanged)" /></label>
                <button type="button" onclick={() => setConsolePassword(a.user)} disabled={!consolePasswordInputs[a.user] || settingConsolePasswordUser === a.user}>
                  {settingConsolePasswordUser === a.user ? 'Saving…' : 'Set password'}
                </button>
                <button type="button" class="remove-btn" onclick={() => removeConsoleUser(a.user)} disabled={removingConsoleUser === a.user}>
                  {removingConsoleUser === a.user ? 'Removing…' : 'Remove'}
                </button>
              </div>
            {/each}
          </div>
        {/if}

        <div class="recipient-row recipient-add">
          <label>New username <input type="text" bind:value={newConsoleUser} /></label>
          <label>New password <input type="text" bind:value={newConsolePassword} /></label>
          <label>
            Role
            <select bind:value={newConsoleRole}>
              {#each ROLES as r (r.key)}<option value={r.key}>{r.label}</option>{/each}
            </select>
          </label>
          <button type="button" onclick={addConsoleUser} disabled={addingConsoleUser}>{addingConsoleUser ? 'Adding…' : '+ Add account'}</button>
        </div>
        <span class="field-hint">The <code>admin_user</code> account in caster.yaml keeps working as-is and can't be removed here.</span>
      {/if}
    </div>
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
    margin-bottom: 1.25rem;
  }

  .card h3 {
    margin: 0 0 1rem;
    font-size: 0.95rem;
    color: var(--text);
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

  .mono {
    font-family: monospace;
    color: var(--text);
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

  .checkbox-label {
    display: inline-flex;
    flex-direction: row;
    align-items: center;
    gap: 0.35rem;
    font-size: 0.78rem;
    color: var(--text-muted);
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

  input {
    padding: 0.4rem 0.6rem;
    background: #12141c;
    border: 1px solid var(--border);
    border-radius: 5px;
    color: var(--text);
    font-size: 0.85rem;
    font-family: inherit;
  }

  input:focus {
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

  .error {
    color: #fca5a5;
    font-size: 0.9rem;
  }

  .loading {
    color: var(--text-dim);
    font-size: 0.9rem;
  }
</style>
