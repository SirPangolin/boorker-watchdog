/**
 * Unified banner system — mirrors firmware event_bus priority pattern.
 *
 * Banners are registered with a priority. Multiple can be active but only
 * the top MAX_VISIBLE by priority are rendered. When a higher-priority
 * banner clears, the next one surfaces automatically.
 *
 * Sources: firmware MOTD (info), app-level alerts (session, connection).
 * Uses OAT's role="alert" + data-variant for styling.
 *
 * Priority 0 = highest (critical errors), higher numbers = lower priority.
 */

/** @type {Map<string, {priority: number, opts: object}>} */
const registry = new Map();

const MAX_VISIBLE = 2;

// Priority constants matching firmware event_bus pattern
export const BANNER_PRIORITY = {
  SESSION_EXPIRED: 0,
  DEVICE_OFFLINE: 1,
  ALERT_CRITICAL: 2,
  ALERT_WARNING: 3,
  MOTD_CRITICAL: 4,
  MOTD_WARNING: 5,
  MOTD_INFO: 6,
};

function getContainer() {
  return document.getElementById('banner-area');
}

function render() {
  const container = getContainer();
  if (!container) return;

  // Clear current banners
  while (container.firstChild) container.removeChild(container.firstChild);

  // Sort by priority (lowest number = highest priority), take top N
  const sorted = [...registry.entries()]
    .sort((a, b) => a[1].priority - b[1].priority)
    .slice(0, MAX_VISIBLE);

  for (const [id, { opts }] of sorted) {
    container.appendChild(buildBannerEl(id, opts));
  }
}

function buildBannerEl(id, opts) {
  const banner = document.createElement('div');
  banner.setAttribute('role', 'alert');
  if (opts.variant) {
    banner.setAttribute('data-variant', opts.variant);
  }
  banner.className = 'alert-banner';
  banner.dataset.bannerId = id;

  if (opts.badge) {
    const chip = document.createElement('span');
    chip.className = `badge ${opts.variant || ''}`.trim();
    chip.textContent = opts.badge;
    banner.appendChild(chip);
  }

  // SECURITY: icon is a trusted internal SVG string, never user content
  if (opts.icon) {
    const icon = document.createElement('span');
    icon.className = 'alert-banner-icon';
    icon.innerHTML = opts.icon; // eslint-disable-line -- trusted static SVG
    banner.appendChild(icon);
  }

  const text = document.createElement('span');
  text.className = 'alert-banner-text';
  text.textContent = opts.message || '';
  banner.appendChild(text);

  if (opts.meta) {
    const meta = document.createElement('span');
    meta.className = 'alert-banner-meta';
    meta.textContent = opts.meta;
    banner.appendChild(meta);
  }

  if (opts.link) {
    const a = document.createElement('a');
    a.href = opts.link.href;
    if (opts.link.external) {
      a.target = '_blank';
      a.rel = 'noopener noreferrer';
    }
    a.textContent = opts.link.text;
    banner.appendChild(a);
  }

  if (opts.onAction && opts.actionText) {
    const btn = document.createElement('button');
    btn.className = 'outline small';
    btn.textContent = opts.actionText;
    btn.addEventListener('click', opts.onAction);
    banner.appendChild(btn);
  }

  if (opts.dismissable !== false) {
    const dismiss = document.createElement('button');
    dismiss.className = 'alert-banner-dismiss';
    dismiss.textContent = '\u00d7';
    dismiss.title = 'Dismiss';
    dismiss.addEventListener('click', () => dismissBanner(id));
    banner.appendChild(dismiss);
  }

  return banner;
}

/**
 * Show or update a banner.
 * @param {string} id - Unique ID (e.g., 'session-expired', 'motd-1')
 * @param {number} priority - Lower = higher priority (use BANNER_PRIORITY constants)
 * @param {object} opts
 * @param {string} opts.message - Banner text
 * @param {'error'|'warning'|'success'} [opts.variant] - OAT alert variant (omit for default/info)
 * @param {string} [opts.badge] - Chip badge text
 * @param {string} [opts.icon] - Trusted icon SVG string (not user content)
 * @param {string} [opts.meta] - Secondary text
 * @param {object} [opts.link] - { href, text, external }
 * @param {boolean} [opts.dismissable=true] - Show dismiss button
 * @param {function} [opts.onAction] - Action button callback
 * @param {string} [opts.actionText] - Action button label
 * @param {function} [opts.onDismiss] - Called on server-side dismiss (e.g., MOTD DELETE)
 */
export function showBanner(id, priority, opts = {}) {
  registry.set(id, { priority, opts });
  render();
}

/**
 * Dismiss a banner by id. Next-priority banner surfaces if one was hidden.
 */
export function dismissBanner(id) {
  const entry = registry.get(id);
  if (entry && entry.opts.onDismiss) {
    entry.opts.onDismiss(id);
  }
  registry.delete(id);
  render();
}

/**
 * Check if a banner is registered (may not be visible if lower priority).
 */
export function hasBanner(id) {
  return registry.has(id);
}

/**
 * Clear all banners.
 */
export function clearAllBanners() {
  registry.clear();
  render();
}
