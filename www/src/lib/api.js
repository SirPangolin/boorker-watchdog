const API_BASE = '/api/v1';

export async function api(method, path, body) {
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
    // Only redirect if we're not already on the login page
    if (window.location.hash !== '#login') {
      window.location.hash = '#login';
    }
    throw new ApiError(data.message || 'Session expired', 401, data);
  }

  if (!res.ok) {
    throw new ApiError(data.message || data.error || 'Request failed', res.status, data);
  }

  return data;
}

export class ApiError extends Error {
  constructor(message, status, data) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.data = data;
  }
}
