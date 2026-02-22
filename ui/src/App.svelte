<script>
  import { isLoggedIn, clearCredentials } from './lib/api.js';
  import Login from './pages/Login.svelte';
  import Shell from './components/Shell.svelte';

  let loggedIn = $state(isLoggedIn());
</script>

{#if loggedIn}
  <Shell onLogout={() => { loggedIn = false; }} />
{:else}
  <Login onLogin={() => { loggedIn = true; }} />
{/if}
