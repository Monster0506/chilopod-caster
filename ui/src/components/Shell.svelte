<script>
  import { router } from '../lib/router.js';
  import { getCredentials, clearCredentials, isAdmin } from '../lib/api.js';
  import Toast from '../lib/Toast.svelte';
  import Dashboard from '../pages/Dashboard.svelte';
  import Connections from '../pages/Connections.svelte';
  import Map from '../pages/Map.svelte';
  import Log from '../pages/Log.svelte';
  import Settings from '../pages/Settings.svelte';
  import Auth from '../pages/Auth.svelte';

  let { onLogout } = $props();

  const { user } = getCredentials();
  const admin = isAdmin();

  const pages = {
    dashboard:   Dashboard,
    connections: Connections,
    map:         Map,
    log:         Log,
    ...(admin ? { settings: Settings, auth: Auth } : {}),
  };

  const navItems = [
    { id: 'dashboard',   label: 'Dashboard' },
    { id: 'connections', label: 'Connections' },
    { id: 'map',         label: 'Map' },
    { id: 'log',         label: 'Log' },
    ...(admin ? [{ id: 'settings', label: 'Settings' }, { id: 'auth', label: 'Auth' }] : []),
  ];

  $effect(() => {
    if (!pages[$router]) router.go('dashboard');
  });

  function logout() {
    clearCredentials();
    onLogout();
  }
</script>

<div class="shell">
  <aside class="sidebar">
    <div class="brand">Chilopod</div>
    <nav>
      {#each navItems as item}
        <button
          class="nav-item"
          class:active={$router === item.id}
          onclick={() => router.go(item.id)}
        >
          {item.label}
        </button>
      {/each}
    </nav>
    <div class="sidebar-footer">
      <span class="sidebar-user">{user}</span>
      <button class="logout-btn" onclick={logout}>Sign out</button>
    </div>
  </aside>

  <main class="content">
    {#if pages[$router]}
      {@const Page = pages[$router]}
      <Page />
    {/if}
  </main>
</div>

<Toast />

<style>
  .shell {
    display: flex;
    min-height: 100vh;
    background: var(--bg);
    color: var(--text);
  }

  /* Sidebar */
  .sidebar {
    width: 200px;
    flex-shrink: 0;
    background: var(--surface);
    border-right: 1px solid var(--border);
    display: flex;
    flex-direction: column;
  }

  .brand {
    padding: 1.25rem 1rem;
    font-size: 1.1rem;
    font-weight: 700;
    color: var(--text);
    border-bottom: 1px solid var(--border);
    letter-spacing: 0.03em;
  }

  nav {
    flex: 1;
    display: flex;
    flex-direction: column;
    padding: 0.5rem 0;
    gap: 2px;
  }

  .nav-item {
    display: block;
    width: 100%;
    padding: 0.55rem 1rem;
    text-align: left;
    background: none;
    border: none;
    border-radius: 0;
    color: var(--text-muted);
    font-size: 0.9rem;
    cursor: pointer;
    transition: background 120ms, color 120ms;
  }

  .nav-item:hover {
    background: #22263a;
    color: var(--text);
  }

  .nav-item.active {
    background: #1e3a5f;
    color: var(--accent-2);
    font-weight: 500;
  }

  .sidebar-footer {
    padding: 0.75rem 1rem;
    border-top: 1px solid var(--border);
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .sidebar-user {
    font-size: 0.8rem;
    color: var(--text-dim);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .logout-btn {
    padding: 0.35rem 0.6rem;
    background: transparent;
    border: 1px solid var(--border);
    border-radius: 4px;
    color: var(--text-dim);
    font-size: 0.8rem;
    cursor: pointer;
    text-align: left;
    transition: border-color 120ms, color 120ms;
  }

  .logout-btn:hover {
    border-color: var(--bad);
    color: #fca5a5;
  }

  /* Content */
  .content {
    flex: 1;
    overflow: auto;
  }


</style>
