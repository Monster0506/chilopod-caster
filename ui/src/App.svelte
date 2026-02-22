<script>
  import { isLoggedIn, clearCredentials } from './lib/api.js';
  import Login from './pages/Login.svelte';
  import Dashboard from './pages/Dashboard.svelte';

  let loggedIn = $state(isLoggedIn());
</script>

{#if loggedIn}
  <Dashboard onLogout={() => { clearCredentials(); loggedIn = false; }} />
{:else}
  <Login onLogin={() => { loggedIn = true; }} />
{/if}
