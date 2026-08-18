export function getCredentials() {
  return {
    user: sessionStorage.getItem('user'),
    password: sessionStorage.getItem('password'),
    role: sessionStorage.getItem('role') ?? 'admin',
  };
}

export function setCredentials(user, password, role = 'admin') {
  sessionStorage.setItem('user', user);
  sessionStorage.setItem('password', password);
  sessionStorage.setItem('role', role);
}

export function clearCredentials() {
  sessionStorage.removeItem('user');
  sessionStorage.removeItem('password');
  sessionStorage.removeItem('role');
}

export function isLoggedIn() {
  return !!sessionStorage.getItem('user');
}

export function isAdmin() {
  return getCredentials().role === 'admin';
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
