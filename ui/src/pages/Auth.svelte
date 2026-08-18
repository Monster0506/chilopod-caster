<script>
  import { apiGet, apiPost } from '../lib/api.js';

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

  let revealedRoverUsers = $state(new Set());
  function toggleRoverReveal(user) {
    const next = new Set(revealedRoverUsers);
    if (next.has(user)) next.delete(user);
    else next.add(user);
    revealedRoverUsers = next;
  }

  let revealedSmtpHosts = $state(new Set());
  function toggleSmtpReveal(host) {
    const next = new Set(revealedSmtpHosts);
    if (next.has(host)) next.delete(host);
    else next.add(host);
    revealedSmtpHosts = next;
  }

  let roverAuthFilenameInput = $state('');
  let savingRoverAuthFilename = $state(false);
  let newRoverUser = $state('');
  let newRoverPassword = $state('');
  let addingRoverAccount = $state(false);
  let removingRoverUser = $state(null);
  let roverPasswordInputs = $state({});
  let settingRoverPasswordUser = $state(null);
  let roverAuthMsg = $state('');

  async function enableRoverAuth() {
    if (!roverAuthFilenameInput) { roverAuthMsg = 'Filename is required.'; return; }
    savingRoverAuthFilename = true;
    roverAuthMsg = '';
    try {
      const res = await apiPost('settings', { rover_auth_filename: roverAuthFilenameInput });
      if (res.error) {
        roverAuthMsg = res.error;
      } else {
        roverAuthMsg = 'Enabled. Add at least one account below - until then, every rover request is rejected.';
        await fetchSettings();
      }
    } catch (e) {
      roverAuthMsg = `Failed: ${e.message}`;
    } finally {
      savingRoverAuthFilename = false;
    }
  }

  async function addRoverAccount() {
    if (!newRoverUser || !newRoverPassword) { roverAuthMsg = 'Username and password are both required.'; return; }
    addingRoverAccount = true;
    roverAuthMsg = '';
    try {
      const res = await apiPost('settings', { 'rover_auth.add.user': newRoverUser, 'rover_auth.add.password': newRoverPassword });
      if (res.error) {
        roverAuthMsg = res.error;
      } else {
        newRoverUser = '';
        newRoverPassword = '';
        await fetchSettings();
      }
    } catch (e) {
      roverAuthMsg = `Failed: ${e.message}`;
    } finally {
      addingRoverAccount = false;
    }
  }

  async function removeRoverAccount(user) {
    removingRoverUser = user;
    roverAuthMsg = '';
    try {
      const res = await apiPost('settings', { 'rover_auth.remove': user });
      if (res.error) {
        roverAuthMsg = res.error;
      } else {
        await fetchSettings();
      }
    } catch (e) {
      roverAuthMsg = `Failed: ${e.message}`;
    } finally {
      removingRoverUser = null;
    }
  }

  async function toggleRoverAccount(account) {
    roverAuthMsg = '';
    const next = !account.enabled;
    try {
      const res = await apiPost('settings', { 'rover_auth.set_enabled.user': account.user, 'rover_auth.set_enabled.value': next ? 'Y' : 'N' });
      if (res.error) {
        roverAuthMsg = res.error;
      } else {
        await fetchSettings();
      }
    } catch (e) {
      roverAuthMsg = `Failed: ${e.message}`;
    }
  }

  async function setRoverPassword(user) {
    const value = roverPasswordInputs[user];
    if (!value) return;
    settingRoverPasswordUser = user;
    roverAuthMsg = '';
    try {
      const res = await apiPost('settings', { 'rover_auth.set_password.user': user, 'rover_auth.set_password.value': value });
      if (res.error) {
        roverAuthMsg = res.error;
      } else {
        roverPasswordInputs = { ...roverPasswordInputs, [user]: '' };
        roverAuthMsg = `Password updated for ${user}.`;
      }
    } catch (e) {
      roverAuthMsg = `Failed: ${e.message}`;
    } finally {
      settingRoverPasswordUser = null;
    }
  }

  let newSmtpAuthHost = $state('');
  let newSmtpAuthUser = $state('');
  let newSmtpAuthPassword = $state('');
  let savingSmtpAuth = $state(false);
  let removingSmtpAuthHost = $state(null);
  let smtpAuthMsg = $state('');

  async function setSmtpAuthCredential() {
    if (!newSmtpAuthHost || !newSmtpAuthUser || !newSmtpAuthPassword) {
      smtpAuthMsg = 'Host, username and password are all required.';
      return;
    }
    savingSmtpAuth = true;
    smtpAuthMsg = '';
    try {
      const res = await apiPost('settings', {
        'alarms.smtp_auth.set.host': newSmtpAuthHost,
        'alarms.smtp_auth.set.user': newSmtpAuthUser,
        'alarms.smtp_auth.set.password': newSmtpAuthPassword,
      });
      if (res.error) {
        smtpAuthMsg = res.error;
      } else {
        newSmtpAuthHost = '';
        newSmtpAuthUser = '';
        newSmtpAuthPassword = '';
        await fetchSettings();
      }
    } catch (e) {
      smtpAuthMsg = `Failed: ${e.message}`;
    } finally {
      savingSmtpAuth = false;
    }
  }

  async function removeSmtpAuthCredential(host) {
    removingSmtpAuthHost = host;
    smtpAuthMsg = '';
    try {
      const res = await apiPost('settings', { 'alarms.smtp_auth.remove': host });
      if (res.error) {
        smtpAuthMsg = res.error;
      } else {
        await fetchSettings();
      }
    } catch (e) {
      smtpAuthMsg = `Failed: ${e.message}`;
    } finally {
      removingSmtpAuthHost = null;
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
          {#if roverAuthMsg}<span class="msg">{roverAuthMsg}</span>{/if}
        </div>
      {:else}
        <p class="field-hint">Auth file: <code>{form.rover_auth.filename}</code></p>

        {#if form.rover_auth.accounts.length > 0}
          <div class="recipient-list">
            {#each form.rover_auth.accounts as a (a.user)}
              <div class="recipient-row">
                <label class="checkbox-label">
                  <input type="checkbox" checked={a.enabled} onchange={() => toggleRoverAccount(a)} />
                  {a.user}
                </label>
                <span class="mono">
                  <button type="button" class="reveal-btn" onclick={() => toggleRoverReveal(a.user)} title={revealedRoverUsers.has(a.user) ? 'Click to hide' : 'Click to reveal'}>
                    {revealedRoverUsers.has(a.user) ? a.password : '••••••••'}
                  </button>
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
        {#if roverAuthMsg}<div class="msg">{roverAuthMsg}</div>{/if}
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
                  <button type="button" class="reveal-btn" onclick={() => toggleSmtpReveal(c.host)} title={revealedSmtpHosts.has(c.host) ? 'Click to hide' : 'Click to reveal'}>
                    {revealedSmtpHosts.has(c.host) ? c.password : '••••••••'}
                  </button>
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
        {#if smtpAuthMsg}<div class="msg">{smtpAuthMsg}</div>{/if}
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

  .mono {
    font-family: monospace;
    color: #e2e8f0;
  }

  .reveal-btn {
    padding: 0;
    background: none;
    border: none;
    color: inherit;
    font: inherit;
    font-family: monospace;
    cursor: pointer;
  }

  .reveal-btn:hover {
    color: #93c5fd;
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

  input {
    padding: 0.4rem 0.6rem;
    background: #12141c;
    border: 1px solid #2a2d3a;
    border-radius: 5px;
    color: #e2e8f0;
    font-size: 0.85rem;
    font-family: inherit;
  }

  input:focus {
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
