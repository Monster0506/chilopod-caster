import { writable } from 'svelte/store';

export const toasts = writable([]);

let nextId = 0;

export function pushToast(text, kind = 'info', { sticky = false, timeout = 4000 } = {}) {
  const id = ++nextId;
  toasts.update((list) => [...list, { id, text, kind, sticky }]);
  if (!sticky) setTimeout(() => dismissToast(id), timeout);
  return id;
}

export function updateToast(id, text) {
  toasts.update((list) => list.map((t) => (t.id === id ? { ...t, text } : t)));
}

export function dismissToast(id) {
  toasts.update((list) => list.filter((t) => t.id !== id));
}
