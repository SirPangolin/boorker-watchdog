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

  if (res.status === 401) {
    window.location.hash = '#login';
    throw new ApiError('Session expired', 401);
  }

  const data = await res.json();

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
