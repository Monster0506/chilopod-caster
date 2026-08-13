<script>
  import JsonTree from './JsonTree.svelte';

  let { value, label = null, depth = 0 } = $props();

  function isPlainObject(v) {
    return v !== null && typeof v === 'object' && !Array.isArray(v);
  }
</script>

{#if isPlainObject(value)}
  {@const entries = Object.entries(value)}
  <details class="json-node" open={depth < 2}>
    <summary class="json-summary">
      {#if label !== null}<span class="json-key">{label}</span>{/if}
      <span class="json-type">{'{'}{entries.length}{'}'}</span>
    </summary>
    <div class="json-children">
      {#each entries as [k, v] (k)}
        <JsonTree value={v} label={k} depth={depth + 1} />
      {/each}
    </div>
  </details>
{:else if Array.isArray(value)}
  <details class="json-node" open={depth < 2}>
    <summary class="json-summary">
      {#if label !== null}<span class="json-key">{label}</span>{/if}
      <span class="json-type">[{value.length}]</span>
    </summary>
    <div class="json-children">
      {#each value as v, i (i)}
        <JsonTree value={v} label={i} depth={depth + 1} />
      {/each}
    </div>
  </details>
{:else}
  <div class="json-leaf">
    {#if label !== null}<span class="json-key">{label}:</span>{/if}
    <span
      class="json-value"
      class:json-null={value === null}
      class:json-string={typeof value === 'string'}
      class:json-number={typeof value === 'number'}
      class:json-bool={typeof value === 'boolean'}
    >
      {value === null ? 'null' : typeof value === 'string' ? `"${value}"` : String(value)}
    </span>
  </div>
{/if}

<style>
  .json-node {
    margin: 0.1rem 0;
  }

  .json-summary {
    cursor: pointer;
    font-family: monospace;
    font-size: 0.82rem;
    list-style: none;
  }

  .json-summary::-webkit-details-marker {
    display: none;
  }

  .json-summary::before {
    content: '▸';
    display: inline-block;
    width: 1em;
    color: #475569;
  }

  .json-node[open] > .json-summary::before {
    content: '▾';
  }

  .json-key {
    color: #93c5fd;
    margin-right: 0.35rem;
  }

  .json-type {
    color: #475569;
  }

  .json-children {
    margin-left: 1.1rem;
    border-left: 1px solid #1e2130;
    padding-left: 0.6rem;
  }

  .json-leaf {
    font-family: monospace;
    font-size: 0.82rem;
    padding-left: 1em;
  }

  .json-value {
    color: #e2e8f0;
  }

  .json-value.json-string {
    color: #86efac;
  }

  .json-value.json-number {
    color: #fbbf24;
  }

  .json-value.json-bool {
    color: #d8b4fe;
  }

  .json-value.json-null {
    color: #64748b;
  }
</style>
