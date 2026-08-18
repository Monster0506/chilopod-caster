<script>
  import { toasts, dismissToast } from './toast.js';
</script>

{#if $toasts.length > 0}
  <div class="toast-stack">
    {#each $toasts as t (t.id)}
      <div class="toast toast-{t.kind}">
        <span>{t.text}</span>
        <button type="button" class="toast-close" onclick={() => dismissToast(t.id)} aria-label="Dismiss">&times;</button>
      </div>
    {/each}
  </div>
{/if}

<style>
  .toast-stack {
    position: fixed;
    left: 50%;
    bottom: 1.25rem;
    transform: translateX(-50%);
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 0.5rem;
    z-index: 200;
  }

  .toast {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.55rem 0.7rem 0.55rem 1rem;
    border-radius: 6px;
    font-size: 0.85rem;
    background: var(--surface-2);
    border: 1px solid var(--border);
    color: var(--text);
    box-shadow: 0 4px 16px rgba(0, 0, 0, 0.3);
  }

  .toast-warn {
    border-color: var(--warn);
    color: #fcd34d;
  }

  .toast-error {
    border-color: var(--bad);
    color: #fca5a5;
  }

  .toast-close {
    background: none;
    border: none;
    color: inherit;
    cursor: pointer;
    font-size: 1rem;
    line-height: 1;
    padding: 0;
    opacity: 0.7;
  }

  .toast-close:hover {
    opacity: 1;
  }
</style>
