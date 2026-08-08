export function getCredentials() {
  return {
    user: sessionStorage.getItem('user'),
    password: sessionStorage.getItem('password'),
  };
}

export function setCredentials(user, password) {
  sessionStorage.setItem('user', user);
  sessionStorage.setItem('password', password);
}

export function clearCredentials() {
  sessionStorage.removeItem('user');
  sessionStorage.removeItem('password');
}

export function isLoggedIn() {
  return !!sessionStorage.getItem('user');
}

function authHeader() {
  const { user, password } = getCredentials();
  return { Authorization: `Basic ${btoa(`${user}:${password}`)}` };
}

export async function apiGet(endpoint) {
  const res = await fetch(`/adm/api/v1/${endpoint}`, { headers: authHeader(), cache: 'no-store' });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}

export async function apiPost(endpoint, extra = {}) {
  const body = new URLSearchParams(extra);
  const res = await fetch(`/adm/api/v1/${endpoint}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded', ...authHeader() },
    body,
  });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}
