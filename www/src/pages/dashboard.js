import { api } from '../lib/api.js';

let pollTimer = null;
let lastLoadTime = null;
let refreshTimer = null;

// ── Icons ──────────────────────────────────────────────────────────

const ICONS = {
  refresh: `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M23 4v6h-6"/><path d="M1 20v-6h6"/>
    <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/>
  </svg>`,
  thermometer: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
    <path d="M14 4v10.54a4 4 0 1 1-4 0V4a2 2 0 0 1 4 0Z"/>
  </svg>`,
  droplet: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
    <path d="M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z"/>
  </svg>`,
  activity: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
    <path d="M22 12h-4l-3 9L9 3l-3 9H2"/>
  </svg>`,
  clock: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
    <circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/>
  </svg>`,
  wifi: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
    <path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/>
    <path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/>
  </svg>`,
  gauge: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
    <path d="M12 2a10 10 0 1 0 10 10"/><path d="M12 12l4-8"/>
  </svg>`,
};

// ── Stat Card Registry ─────────────────────────────────────────────
// Maps sensor reading keys and system stat keys to display config.
// Unknown reading keys get a generic card via fallback.

const READING_TYPES = {
  // Sensor readings (from /sensors → readings object)
  temperature: {
    label: 'Temperature',
    icon: ICONS.thermometer,
    iconColor: 'primary',
    format: (v) => `${Number(v).toFixed(1)}<span class="stat-unit">°C</span>`,
    meta: () => 'Normal range',
  },
  humidity: {
    label: 'Humidity',
    icon: ICONS.droplet,
    iconColor: 'success',
    format: (v) => `${Math.round(v)}<span class="stat-unit">%</span>`,
    meta: () => 'Normal range',
  },
  state: {
    label: 'Vibration',
    icon: ICONS.activity,
    iconColor: 'warning',
    format: (v) => capitalize(String(v)),
    meta: (sensor) => {
      const ago = timeAgo(sensor.last_update);
      return ago ? `Last active ${ago}` : '';
    },
  },

  // System stats (from /system/status)
  uptime_seconds: {
    label: 'Uptime',
    icon: ICONS.clock,
    iconColor: 'primary',
    format: (v) => formatUptime(v),
    meta: () => 'Since last reboot',
    system: true,
  },
  wifi_rssi: {
    label: 'WiFi Signal',
    icon: ICONS.wifi,
    iconColor: 'success',
    format: (v) => `${v}<span class="stat-unit">dBm</span>`,
    meta: (_, status) => status.wifi_ssid || '',
    system: true,
  },
};

// ── Section Registry ───────────────────────────────────────────────
// Body sections below the stat grid. Each section renders from API data.
// Future sections (Mesh Network, Alerts, etc.) are added here.

const SECTIONS = [
  {
    id: 'system-health',
    title: 'System Health',
    badge: (data) => data.status.device_name || null,
    link: { text: 'View all →', href: '#settings' },
    render: renderSystemHealth,
  },
  {
    id: 'recent-events',
    title: 'Recent Events',
    badge: () => 'Today',
    link: null, // future: { text: 'View all →', href: '#events' }
    render: renderRecentEvents,
  },
];

// ── Collect stat cards from API data ───────────────────────────────

function collectCards(status, sensors) {
  const cards = [];

  // System stat cards
  for (const [key, config] of Object.entries(READING_TYPES)) {
    if (!config.system) continue;
    if (status[key] == null) continue;
    cards.push({
      key,
      label: config.label,
      icon: config.icon,
      iconColor: config.iconColor,
      valueHtml: config.format(status[key]),
      meta: config.meta(null, status),
    });
  }

  // Sensor reading cards
  for (const sensor of sensors) {
    if (!sensor.readings) continue;
    for (const [readingKey, value] of Object.entries(sensor.readings)) {
      const config = READING_TYPES[readingKey];
      if (config && !config.system) {
        cards.push({
          key: `${sensor.id}-${readingKey}`,
          label: config.label,
          icon: config.icon,
          iconColor: config.iconColor,
          valueHtml: config.format(value),
          meta: config.meta(sensor, null),
        });
      } else if (!config) {
        cards.push({
          key: `${sensor.id}-${readingKey}`,
          label: capitalize(readingKey.replace(/_/g, ' ')),
          icon: ICONS.gauge,
          iconColor: 'primary',
          valueHtml: escapeHtml(String(value)),
          meta: sensor.id,
        });
      }
    }
  }

  return cards;
}

// ── Page Lifecycle ─────────────────────────────────────────────────

export function render() {
  return `
    <div class="page-header">
      <h1 class="page-title">Dashboard</h1>
      <div class="refresh-group">
        <span class="refresh-text" id="refresh-text"></span>
        <button class="outline small" id="refresh-btn">${ICONS.refresh} Refresh</button>
      </div>
    </div>
    <div class="stat-grid" id="stat-grid" aria-busy="true" data-spinner="small"></div>
    <div id="alert-area"></div>
    <div class="section-grid" id="section-grid"></div>
  `;
}

export function mount() {
  loadData();
  pollTimer = setInterval(loadData, 10000);

  const refreshBtn = document.getElementById('refresh-btn');
  if (refreshBtn) {
    refreshBtn.addEventListener('click', () => {
      refreshBtn.setAttribute('aria-busy', 'true');
      refreshBtn.disabled = true;
      loadData().finally(() => {
        refreshBtn.removeAttribute('aria-busy');
        refreshBtn.disabled = false;
      });
    });
  }

  updateRefreshText();
  refreshTimer = setInterval(updateRefreshText, 1000);
}

export function unmount() {
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
  if (refreshTimer) {
    clearInterval(refreshTimer);
    refreshTimer = null;
  }
}

function updateRefreshText() {
  const el = document.getElementById('refresh-text');
  if (!el || !lastLoadTime) return;
  const seconds = Math.round((Date.now() - lastLoadTime) / 1000);
  el.textContent = seconds < 5 ? 'Updated just now' : `Updated ${seconds}s ago`;
}

async function loadData() {
  const [status, sensors, events] = await Promise.all([
    api('GET', '/system/status').catch(() => null),
    api('GET', '/sensors').catch(() => []),
    api('GET', '/events').catch(() => []),
  ]);
  lastLoadTime = Date.now();
  updateRefreshText();
  if (!status) return; // Can't render without status
  const sensorList = Array.isArray(sensors) ? sensors : [];
  const eventList = Array.isArray(events) ? events : [];
  renderStatGrid(status, sensorList);
  renderSections({ status, sensors: sensorList, events: eventList });
}

// ── Stat Grid ──────────────────────────────────────────────────────

function renderStatGrid(status, sensors) {
  const grid = document.getElementById('stat-grid');
  if (!grid) return;

  const cards = collectCards(status, sensors);

  if (cards.length === 0) {
    grid.innerHTML = '<p style="padding:1rem;color:var(--muted-foreground)">No data available.</p>';
    grid.removeAttribute('aria-busy');
    grid.removeAttribute('data-spinner');
    return;
  }

  grid.innerHTML = cards.map(card => `
    <article class="card stat-card">
      <div class="stat-header">
        <span class="stat-label">${card.label}</span>
        <div class="stat-icon ${card.iconColor}">${card.icon}</div>
      </div>
      <div class="stat-value">${card.valueHtml}</div>
      ${card.meta ? `<div class="stat-meta">${escapeHtml(card.meta)}</div>` : ''}
    </article>
  `).join('');

  grid.removeAttribute('aria-busy');
  grid.removeAttribute('data-spinner');
}

// ── Section Grid ───────────────────────────────────────────────────

function renderSections(data) {
  const container = document.getElementById('section-grid');
  if (!container) return;

  container.innerHTML = SECTIONS.map(section => {
    const badgeText = section.badge(data);
    const badgeHtml = badgeText
      ? `<span class="chip outline small">${escapeHtml(badgeText)}</span>`
      : '';
    const linkHtml = section.link
      ? `<a href="${section.link.href}" class="section-footer">${section.link.text}</a>`
      : '';

    return `
      <article class="card section-card">
        <header class="section-header">
          <h3 class="section-title">${section.title}</h3>
          ${badgeHtml}
        </header>
        <div class="section-body" id="section-${section.id}">
          ${section.render(data)}
        </div>
        ${linkHtml}
      </article>
    `;
  }).join('');
}

// ── Section: System Health ─────────────────────────────────────────

function renderSystemHealth(data) {
  const s = data.status;
  const heapPct = s.heap_total ? Math.round((s.heap_free / s.heap_total) * 100) : 0;
  const psramPct = s.psram_total ? Math.round((s.psram_free / s.psram_total) * 100) : 0;

  const rows = [
    {
      label: 'Heap Memory',
      value: `${formatBytes(s.heap_free)} / ${formatBytes(s.heap_total)}`,
      pct: heapPct,
    },
    {
      label: 'PSRAM',
      value: `${formatBytes(s.psram_free)} / ${formatBytes(s.psram_total)}`,
      pct: psramPct,
    },
    {
      label: 'WiFi',
      value: s.wifi_connected
        ? `${s.wifi_rssi} dBm`
        : 'Disconnected',
      sub: s.wifi_ssid || null,
    },
    {
      label: 'IP Address',
      value: s.ip_address || 'N/A',
      sub: s.mac_address || null,
    },
  ];

  return rows.map(row => {
    let detail = '';
    if (row.pct != null) {
      detail = `
        <div class="health-bar-track">
          <div class="health-bar-fill ${row.pct < 20 ? 'danger' : row.pct < 50 ? 'warning' : ''}" style="width:${100 - row.pct}%"></div>
        </div>`;
    }
    return `
      <div class="health-row">
        <div class="health-label">
          <strong>${row.label}</strong>
          ${row.sub ? `<small>${escapeHtml(row.sub)}</small>` : ''}
        </div>
        <div class="health-value">${escapeHtml(row.value)}</div>
      </div>
      ${detail}`;
  }).join('');
}

// ── Section: Recent Events ─────────────────────────────────────────

function renderRecentEvents(data) {
  const events = data.events;
  if (!Array.isArray(events) || events.length === 0) {
    return '<p style="color:var(--muted-foreground);padding:0.5rem 0">No recent events.</p>';
  }

  return events.slice(0, 5).map(ev => `
    <div class="event-item">
      <span class="event-time">${formatTime(ev.timestamp)}</span>
      <span class="event-dot ${ev.type === 'alert' ? 'alert' : ev.type === 'warning' ? 'warning' : 'info'}"></span>
      <div class="event-content">
        <div class="event-text">${escapeHtml(ev.message)}</div>
        <div class="event-source">${escapeHtml(ev.source)}</div>
      </div>
    </div>
  `).join('');
}

// ── Helpers ─────────────────────────────────────────────────────────

function escapeHtml(str) {
  const div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
}

function capitalize(str) {
  if (!str) return '';
  return str.charAt(0).toUpperCase() + str.slice(1);
}

function formatBytes(bytes) {
  if (bytes == null) return 'N/A';
  if (bytes < 1024) return `${bytes}B`;
  const kb = bytes / 1024;
  if (kb < 1024) return `${Math.round(kb)}KB`;
  const mb = kb / 1024;
  return `${mb.toFixed(1)}MB`;
}

function timeAgo(unixTs) {
  if (!unixTs) return null;
  const diff = Math.floor(Date.now() / 1000) - unixTs;
  if (diff < 60) return 'just now';
  if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
  if (diff < 86400) {
    const h = Math.floor(diff / 3600);
    const m = Math.floor((diff % 3600) / 60);
    return `${h}h ${m}m ago`;
  }
  return `${Math.floor(diff / 86400)}d ago`;
}

function formatUptime(seconds) {
  if (seconds == null) return 'N/A';
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  if (days > 0) return `${days}d ${hours}h`;
  const mins = Math.floor((seconds % 3600) / 60);
  if (hours > 0) return `${hours}h ${mins}m`;
  return `${mins}m`;
}

function formatTime(ts) {
  if (!ts) return '';
  const d = new Date(ts * 1000);
  const hh = String(d.getHours()).padStart(2, '0');
  const mm = String(d.getMinutes()).padStart(2, '0');
  return `${hh}:${mm}`;
}
