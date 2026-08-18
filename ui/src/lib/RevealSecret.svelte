<script>
  let { value } = $props();
  let revealed = $state(false);
</script>

<button
  type="button"
  class="reveal-btn"
  onclick={() => (revealed = !revealed)}
  title={revealed ? 'Click to hide' : 'Click to reveal'}
>
  {revealed ? value : '••••••••'}
</button>

<style>
  .reveal-btn {
    padding: 0;
    background: none;
    border: none;
    color: inherit;
    font: inherit;
    font-family: var(--font-mono, monospace);
    cursor: pointer;
  }

  .reveal-btn:hover {
    color: var(--accent-2);
  }
</style>
