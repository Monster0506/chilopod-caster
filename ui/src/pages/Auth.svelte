<script>
  import { apiGet, apiPost } from '../lib/api.js';

  let entries = $state(null);
  let error = $state('');
  let autoRefresh = $state(true);

  let showForm = $state(false);
  let submitting = $state(false);
  let formMsg = $state('');
  const blankForm = { mountpoint: '', auth_user: '', auth_password: '' };
  let form = $state({ ...blankForm });

  let editingKey = $state(null);
  let editForm = $state({ auth_user: '', auth_password: '' });
  let saving = $state(false);
  let editMsg = $state('');

  let removing = $state(new Set());
  let removeMsg = $state('');

  let revealed = $state(new Set());

  async function fetchAll() {
    try {
      const res = await apiGet('auth');
      error = '';
      entries = res;
    } catch (e) {
      error = e.message;
    }
  }

  function toggleReveal(mountpoint) {
    const next = new Set(revealed);
    if (next.has(mountpoint)) next.delete(mountpoint);
    else next.add(mountpoint);
    revealed = next;
  }

  async function submitForm() {
    formMsg = '';
    if (!form.mountpoint || !form.auth_user || !form.auth_password) {
      formMsg = 'Mountpoint, user and password are required.';
      return;
    }
    submitting = true;
    try {
      const res = await apiPost('auth', { ...form });
      if (res.error) {
        formMsg = res.error;
      } else {
        formMsg = `Saved ${form.mountpoint}.`;
        form = { ...blankForm };
        showForm = false;
        await fetchAll();
      }
    } catch (e) {
      formMsg = `Failed: ${e.message}`;
    } finally {
      submitting = false;
    }
  }

  function startEdit(entry) {
    editingKey = entry.mountpoint;
    editForm = { auth_user: entry.user, auth_password: entry.password };
    editMsg = '';
  }

  function cancelEdit() {
    editingKey = null;
  }

  async function saveEdit(mountpoint) {
    if (!editForm.auth_user || !editForm.auth_password) {
      editMsg = 'User and password are required.';
      return;
    }
    saving = true;
    editMsg = '';
    try {
      const res = await apiPost('auth', { mountpoint, ...editForm });
      if (res.error) {
        editMsg = res.error;
      } else {
        editingKey = null;
        await fetchAll();
      }
    } catch (e) {
      editMsg = `Failed: ${e.message}`;
    } finally {
      saving = false;
    }
  }

  async function removeEntry(mountpoint) {
    if (!confirm(`Remove the auth entry for "${mountpoint}"? Anyone using it will no longer be able to authenticate.`)) return;
    removing = new Set([...removing, mountpoint]);
    removeMsg = '';
    try {
      const res = await apiPost('auth/remove', { mountpoint });
      removeMsg = res.result === 0
        ? `Removed ${mountpoint}.`
        : (res.error ?? 'Failed to remove entry.');
      await fetchAll();
    } catch (e) {
      removeMsg = `Failed: ${e.message}`;
    } finally {
      removing = new Set([...removing].filter((x) => x !== mountpoint));
    }
  }

  $effect(() => {
    fetchAll();
    if (!autoRefresh) return;
    const id = setInterval(fetchAll, 5000);
    return () => clearInterval(id);
  });
</script>

<div class="page">
  <div class="header">
    <h2>Auth</h2>
    <div class="controls">
      <label class="toggle">
        <input type="checkbox" bind:checked={autoRefresh} />
        Auto-refresh
      </label>
      <button onclick={fetchAll}>Refresh</button>
      <button class="add-btn" onclick={() => (showForm = !showForm)}>
        {showForm ? 'Cancel' : '+ Add Entry'}
      </button>
    </div>
  </div>

  {#if showForm}
    <form class="add-form" onsubmit={(e) => { e.preventDefault(); submitForm(); }}>
      <div class="grid">
        <label>Mountpoint <input required bind:value={form.mountpoint} placeholder="MOUNT1" /></label>
        <label>User <input required bind:value={form.auth_user} placeholder="baseuser" /></label>
        <label>Password <input required bind:value={form.auth_password} placeholder="secret" /></label>
      </div>
      <div class="form-actions">
        <button type="submit" disabled={submitting}>{submitting ? 'Saving…' : 'Add entry'}</button>
        {#if formMsg}<span class="form-msg">{formMsg}</span>{/if}
      </div>
      <p class="hint">
        Adding an entry for a mountpoint that already has one updates it in place. The special
        mountpoint matching your own admin username is the credential you're logged in with --
        removing or changing it will lock you out.
      </p>
    </form>
  {/if}

  {#if removeMsg}
    <p class="remove-msg">{removeMsg}</p>
  {/if}

  {#if error}
    <p class="error">{error}</p>
  {:else if !entries}
    <p class="loading">Loading…</p>
  {:else}
    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>Mountpoint</th>
            <th>User</th>
            <th>Password</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          {#if entries.length === 0}
            <tr><td colspan="4" class="empty">No auth entries configured.</td></tr>
          {/if}
          {#each entries as entry (entry.mountpoint)}
            <tr>
              <td class="mono">{entry.mountpoint}</td>
              {#if editingKey === entry.mountpoint}
                <td><input class="edit-input" bind:value={editForm.auth_user} /></td>
                <td><input class="edit-input" bind:value={editForm.auth_password} /></td>
                <td class="actions">
                  <button class="save-btn" onclick={() => saveEdit(entry.mountpoint)} disabled={saving}>
                    {saving ? '…' : 'Save'}
                  </button>
                  <button class="cancel-btn" onclick={cancelEdit} disabled={saving}>Cancel</button>
                  {#if editMsg}<span class="row-msg">{editMsg}</span>{/if}
                </td>
              {:else}
                <td class="mono">{entry.user}</td>
                <td class="mono">
                  {revealed.has(entry.mountpoint) ? entry.password : '•'.repeat(Math.min(entry.password.length, 12))}
                  <button class="reveal-btn" onclick={() => toggleReveal(entry.mountpoint)}>
                    {revealed.has(entry.mountpoint) ? 'Hide' : 'Show'}
                  </button>
                </td>
                <td class="actions">
                  <button class="edit-btn" onclick={() => startEdit(entry)}>Edit</button>
                  <button
                    class="remove-btn"
                    onclick={() => removeEntry(entry.mountpoint)}
                    disabled={removing.has(entry.mountpoint)}
                  >
                    {removing.has(entry.mountpoint) ? '…' : 'Delete'}
                  </button>
                </td>
              {/if}
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
    <p class="count">{entries.length} entr{entries.length === 1 ? 'y' : 'ies'}</p>
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

  button:hover:not(:disabled) {
    background: #1e40af;
  }

  button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .add-btn {
    background: #1a3a1a;
    border-color: #22c55e;
    color: #86efac;
  }

  .add-btn:hover:not(:disabled) {
    background: #14532d;
  }

  .add-form {
    background: #1a1d27;
    border: 1px solid #2a2d3a;
    border-radius: 8px;
    padding: 1.25rem;
    margin-bottom: 1.5rem;
  }

  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
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
  }

  input:focus {
    outline: none;
    border-color: #2563eb;
  }

  .edit-input {
    width: 100%;
    padding: 0.3rem 0.5rem;
    font-family: monospace;
    font-size: 0.82rem;
  }

  .form-actions {
    display: flex;
    align-items: center;
    gap: 1rem;
    margin-top: 1rem;
  }

  .form-msg {
    font-size: 0.85rem;
    color: #94a3b8;
  }

  .hint {
    margin: 0.75rem 0 0;
    font-size: 0.78rem;
    color: #475569;
  }

  .remove-msg {
    margin: 0 0 1rem;
    font-size: 0.85rem;
    color: #64748b;
  }

  .actions {
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }

  .row-msg {
    font-size: 0.78rem;
    color: #fca5a5;
  }

  .edit-btn {
    padding: 0.25rem 0.6rem;
    background: transparent;
    border: 1px solid #1e3a5f;
    border-radius: 4px;
    color: #93c5fd;
    font-size: 0.78rem;
    cursor: pointer;
    transition: background 120ms;
  }

  .edit-btn:hover:not(:disabled) {
    background: #1e3a5f33;
  }

  .save-btn {
    padding: 0.25rem 0.6rem;
    background: #1a3a1a;
    border: 1px solid #22c55e;
    border-radius: 4px;
    color: #86efac;
    font-size: 0.78rem;
    cursor: pointer;
  }

  .save-btn:hover:not(:disabled) {
    background: #14532d;
  }

  .cancel-btn {
    padding: 0.25rem 0.6rem;
    background: transparent;
    border: 1px solid #2a2d3a;
    border-radius: 4px;
    color: #94a3b8;
    font-size: 0.78rem;
    cursor: pointer;
  }

  .cancel-btn:hover:not(:disabled) {
    background: #22263a;
  }

  .reveal-btn {
    padding: 0.1rem 0.4rem;
    margin-left: 0.4rem;
    background: transparent;
    border: 1px solid #2a2d3a;
    border-radius: 3px;
    color: #64748b;
    font-size: 0.7rem;
    cursor: pointer;
  }

  .reveal-btn:hover {
    background: #22263a;
    color: #94a3b8;
  }

  .remove-btn {
    padding: 0.25rem 0.6rem;
    background: transparent;
    border: 1px solid #7f1d1d;
    border-radius: 4px;
    color: #fca5a5;
    font-size: 0.78rem;
    cursor: pointer;
    transition: background 120ms;
  }

  .remove-btn:hover:not(:disabled) {
    background: #7f1d1d33;
  }

  .remove-btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
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

  .mono {
    font-family: monospace;
    font-size: 0.82rem;
  }

  .empty {
    text-align: center;
    color: #475569;
    padding: 2rem;
  }

  .count {
    margin: 0.75rem 0 0;
    font-size: 0.8rem;
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
