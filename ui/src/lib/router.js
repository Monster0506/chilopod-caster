import { writable } from 'svelte/store';

const VALID = ['dashboard', 'connections', 'sources', 'auth', 'settings'];

function current() {
  const h = location.hash.slice(1);
  return VALID.includes(h) ? h : 'dashboard';
}

function createRouter() {
  const { subscribe, set } = writable(current());

  window.addEventListener('hashchange', () => set(current()));

  return {
    subscribe,
    go(page) { location.hash = page; },
  };
}

export const router = createRouter();
