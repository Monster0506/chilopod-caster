<script>
  import { clearCredentials, getCredentials } from '../lib/api.js';

  let { onLogout } = $props();

  const { user } = getCredentials();

  function logout() {
    clearCredentials();
    onLogout();
  }
</script>

<div class="dashboard">
  <header>
    <span class="brand">Millipede</span>
    <span class="user">{user}</span>
    <button onclick={logout}>Sign out</button>
  </header>
  <main>
    <p>Dashboard coming soon.</p>
  </main>
</div>

<style>
  .dashboard {
    min-height: 100vh;
    background: #0f1117;
    color: #e2e8f0;
  }

  header {
    display: flex;
    align-items: center;
    gap: 1rem;
    padding: 0.75rem 1.5rem;
    background: #1a1d27;
    border-bottom: 1px solid #2a2d3a;
  }

  .brand {
    font-weight: 600;
    font-size: 1rem;
    flex: 1;
  }

  .user {
    color: #64748b;
    font-size: 0.85rem;
  }

  button {
    padding: 0.35rem 0.75rem;
    background: transparent;
    border: 1px solid #2a2d3a;
    border-radius: 5px;
    color: #94a3b8;
    font-size: 0.85rem;
    cursor: pointer;
  }

  button:hover {
    border-color: #3b82f6;
    color: #e2e8f0;
  }

  main {
    padding: 2rem;
    color: #64748b;
  }
</style>
