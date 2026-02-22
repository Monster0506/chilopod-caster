<script>
  import { router } from '../lib/router.js';
  import { getCredentials, clearCredentials } from '../lib/api.js';
  import Dashboard from '../pages/Dashboard.svelte';
  import Connections from '../pages/Connections.svelte';
  import Sources from '../pages/Sources.svelte';
  import Auth from '../pages/Auth.svelte';
  import Settings from '../pages/Settings.svelte';

  let { onLogout } = $props();

  const { user } = getCredentials();

  const pages = {
    dashboard:   Dashboard,
    connections: Connections,
    sources:     Sources,
    auth:        Auth,
    settings:    Settings,
  };

  const navItems = [
    { id: 'dashboard',   label: 'Dashboard' },
    { id: 'connections', label: 'Connections' },
    { id: 'sources',     label: 'Sources' },
    { id: 'auth',        label: 'Auth' },
    { id: 'settings',    label: 'Settings' },
  ];

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

<style>
  .shell {
    display: flex;
    min-height: 100vh;
    background: #0f1117;
    color: #e2e8f0;
  }

  /* Sidebar */
  .sidebar {
    width: 200px;
    flex-shrink: 0;
    background: #1a1d27;
    border-right: 1px solid #2a2d3a;
    display: flex;
    flex-direction: column;
  }

  .brand {
    padding: 1.25rem 1rem;
    font-size: 1.1rem;
    font-weight: 700;
    color: #e2e8f0;
    border-bottom: 1px solid #2a2d3a;
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
    color: #94a3b8;
    font-size: 0.9rem;
    cursor: pointer;
    transition: background 120ms, color 120ms;
  }

  .nav-item:hover {
    background: #22263a;
    color: #e2e8f0;
  }

  .nav-item.active {
    background: #1e3a5f;
    color: #93c5fd;
    font-weight: 500;
  }

  .sidebar-footer {
    padding: 0.75rem 1rem;
    border-top: 1px solid #2a2d3a;
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .sidebar-user {
    font-size: 0.8rem;
    color: #475569;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .logout-btn {
    padding: 0.35rem 0.6rem;
    background: transparent;
    border: 1px solid #2a2d3a;
    border-radius: 4px;
    color: #64748b;
    font-size: 0.8rem;
    cursor: pointer;
    text-align: left;
    transition: border-color 120ms, color 120ms;
  }

  .logout-btn:hover {
    border-color: #ef4444;
    color: #fca5a5;
  }

  /* Content */
  .content {
    flex: 1;
    overflow: auto;
  }


</style>
