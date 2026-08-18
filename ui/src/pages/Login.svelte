<script>
  import { setCredentials } from '../lib/api.js';

  let { onLogin } = $props();

  let user = $state('admin');
  let password = $state('');
  let error = $state('');
  let loading = $state(false);

  async function login(e) {
    e.preventDefault();
    error = '';
    loading = true;
    try {
      const res = await fetch('/adm/api/v1/whoami', {
        headers: { Authorization: `Basic ${btoa(`${user}:${password}`)}` },
      });
      if (res.ok) {
        const { role } = await res.json();
        setCredentials(user, password, role);
        onLogin();
      } else if (res.status === 401) {
        error = 'Invalid credentials.';
      } else {
        error = `Server error: ${res.status}`;
      }
    } catch {
      error = 'Could not reach the caster.';
    } finally {
      loading = false;
    }
  }
</script>

<div class="login-wrap">
  <form class="login-box" onsubmit={login}>
    <h1>Chilopod</h1>
    <p class="subtitle">NTRIP Caster Administration</p>

    {#if error}
      <p class="error">{error}</p>
    {/if}

    <label>
      Username
      <input type="text" bind:value={user} autocomplete="username" required />
    </label>

    <label>
      Password
      <input type="password" bind:value={password} autocomplete="current-password" required />
    </label>

    <button type="submit" disabled={loading}>
      {loading ? 'Signing in…' : 'Sign in'}
    </button>
  </form>
</div>

<style>
  .login-wrap {
    min-height: 100vh;
    display: flex;
    align-items: center;
    justify-content: center;
    background: var(--bg);
  }

  .login-box {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 2.5rem 2rem;
    width: 100%;
    max-width: 360px;
    display: flex;
    flex-direction: column;
    gap: 1.2rem;
  }

  h1 {
    margin: 0;
    font-size: 1.6rem;
    color: var(--text);
    letter-spacing: 0.02em;
  }

  .subtitle {
    margin: -0.8rem 0 0;
    color: var(--text-dim);
    font-size: 0.85rem;
  }

  label {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    font-size: 0.85rem;
    color: var(--text-muted);
  }

  input {
    padding: 0.55rem 0.75rem;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 5px;
    color: var(--text);
    font-size: 0.95rem;
    outline: none;
    transition: border-color 150ms;
  }

  input:focus {
    border-color: var(--accent);
  }

  button {
    padding: 0.6rem;
    background: var(--accent);
    color: #fff;
    border: none;
    border-radius: 5px;
    font-size: 0.95rem;
    cursor: pointer;
    transition: background 150ms;
  }

  button:hover:not(:disabled) {
    background: var(--accent);
  }

  button:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }

  .error {
    margin: 0;
    padding: 0.5rem 0.75rem;
    background: #7f1d1d33;
    border: 1px solid #7f1d1d;
    border-radius: 5px;
    color: #fca5a5;
    font-size: 0.85rem;
  }
</style>
