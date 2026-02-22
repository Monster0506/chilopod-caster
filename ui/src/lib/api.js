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

export async function apiGet(endpoint) {
  const { user, password } = getCredentials();
  const params = new URLSearchParams({ user, password });
  const res = await fetch(`/adm/api/v1/${endpoint}?${params}`);
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}
