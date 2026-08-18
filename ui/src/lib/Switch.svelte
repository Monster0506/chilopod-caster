<script>
  let { checked = false, onchange } = $props();
</script>

<span class="switch">
  <input type="checkbox" {checked} onchange={(e) => onchange?.(e.currentTarget.checked)} />
  <span class="switch-track"></span>
</span>

<style>
  .switch {
    position: relative;
    display: inline-block;
    flex-shrink: 0;
    width: 2.4rem;
    height: 1.35rem;
  }

  .switch input {
    position: absolute;
    inset: 0;
    margin: 0;
    opacity: 0;
    cursor: pointer;
  }

  .switch-track {
    position: absolute;
    inset: 0;
    background: #2a2f42;
    border: 1px solid #333a52;
    border-radius: 999px;
    transition: background 0.15s ease, border-color 0.15s ease;
  }

  .switch-track::before {
    content: '';
    position: absolute;
    top: 1px;
    left: 1px;
    width: 1.1rem;
    height: 1.1rem;
    background: #cbd5e1;
    border-radius: 50%;
    transition: transform 0.15s ease, background 0.15s ease;
  }

  .switch input:checked + .switch-track {
    background: var(--accent);
    border-color: var(--accent);
  }

  .switch input:checked + .switch-track::before {
    transform: translateX(1.05rem);
    background: #fff;
  }

  .switch input:disabled + .switch-track {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .switch input:focus-visible + .switch-track {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
  }
</style>
