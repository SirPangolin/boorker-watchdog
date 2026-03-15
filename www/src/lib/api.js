const API_BASE = '/api/v1';

// Prevents request storms after session expiry (e.g., post-flash reboot)
let authRedirectPending = false;

export async function api(method, path, body) {
  if (authRedirectPending) {
    throw new ApiError('Session expired', 401, {});
  }

  const opts = {
    method,
    headers: {},
  };

  if (body !== undefined) {
    opts.headers['Content-Type'] = 'application/json';
    opts.body = JSON.stringify(body);
  }

  const res = await fetch(`${API_BASE}${path}`, opts);

  let data;
  try {
    data = await res.json();
  } catch {
    data = {};
  }

  if (res.status === 401) {
    if (!authRedirectPending) {
      authRedirectPending = true;
      // Use replaceState to avoid polluting history
      if (window.location.hash !== '#login') {
        window.location.replace('#login');
      }
    }
    throw new ApiError(data.message || 'Session expired', 401, data);
  }

  if (!res.ok) {
    throw new ApiError(data.message || data.error || 'Request failed', res.status, data);
  }

  return data;
}

/** Reset auth gate after successful login */
export function clearAuthRedirect() {
  authRedirectPending = false;
}

export class ApiError extends Error {
  constructor(message, status, data) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.data = data;
  }
}
