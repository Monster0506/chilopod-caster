<script>
  import Mountpoints from './Mountpoints.svelte';
  import Rovers from './Rovers.svelte';

  let tab = $state('mountpoints');
</script>

<div class="page">
  <div class="tabs">
    <button class="tab" class:active={tab === 'mountpoints'} onclick={() => (tab = 'mountpoints')}>
      Mountpoints
    </button>
    <button class="tab" class:active={tab === 'rovers'} onclick={() => (tab = 'rovers')}>
      Rovers
    </button>
  </div>

  {#if tab === 'mountpoints'}
    <Mountpoints />
  {:else}
    <Rovers />
  {/if}
</div>

<style>
  .page {
    padding: 2rem;
  }

  .tabs {
    display: flex;
    gap: 0.4rem;
    margin-bottom: 1.5rem;
    border-bottom: 1px solid var(--border);
  }

  .tab {
    padding: 0.5rem 1rem;
    background: none;
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    color: var(--text-dim);
    font-size: 0.9rem;
    cursor: pointer;
    transition: color 120ms, border-color 120ms;
  }

  .tab:hover:not(.active) {
    color: var(--text-muted);
  }

  .tab.active {
    color: var(--text);
    border-bottom-color: var(--accent);
  }
</style>
