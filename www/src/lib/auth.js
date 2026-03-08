import { api, ApiError } from './api.js';

export async function checkSession() {
  try {
    const data = await api('GET', '/auth/status');
    return {
      authenticated: data.authenticated,
      claimed: data.password_changed,
    };
  } catch (err) {
    if (err instanceof ApiError && err.status === 401) {
      return { authenticated: false, claimed: true };
    }
    return { authenticated: false, claimed: true };
  }
}

export async function login(username, password) {
  return api('POST', '/auth/login', { username, password });
}

export async function logout() {
  return api('POST', '/auth/logout');
}

export async function changePassword(currentPassword, newPassword) {
  return api('PUT', '/auth/password', {
    current_password: currentPassword,
    new_password: newPassword,
  });
}
