import { api } from '../lib/api.js';

let pollTimer = null;

export function render() {
  return `
    <div class="stat-grid">
      <article class="card stat-card" aria-busy="true" data-spinner="small">
        <header><small>Temperature</small></header>
        <div class="stat-value" id="stat-temp">--</div>
      </article>
      <article class="card stat-card" aria-busy="true" data-spinner="small">
        <header><small>Humidity</small></header>
        <div class="stat-value" id="stat-humidity">--</div>
      </article>
      <article class="card stat-card" aria-busy="true" data-spinner="small">
        <header><small>Vibration</small></header>
        <div class="stat-value" id="stat-vibration">--</div>
      </article>
      <article class="card stat-card" aria-busy="true" data-spinner="small">
        <header><small>Uptime</small></header>
        <div class="stat-value" id="stat-uptime">--</div>
      </article>
    </div>
    <div id="alert-area"></div>
    <article class="card">
      <header><h3>Recent Events</h3></header>
      <div id="events-list" aria-busy="true" data-spinner="small"></div>
    </article>
  `;
}

export function mount() {
  loadData();
  pollTimer = setInterval(loadData, 10000);
}

export function unmount() {
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
}

async function loadData() {
  try {
    const [status, sensors, events] = await Promise.all([
      api('GET', '/system/status'),
      api('GET', '/sensors'),
      api('GET', '/events'),
    ]);
    renderStats(status, sensors);
    renderEvents(events);
  } catch (err) {
    console.error('Dashboard load failed:', err);
  }
}

function renderStats(status, sensors) {
  const tempSensor = sensors.find(s => s.readings && s.readings.temperature !== undefined);
  const humiditySensor = sensors.find(s => s.readings && s.readings.humidity !== undefined);
  const vibrationSensor = sensors.find(s => s.readings && s.readings.state !== undefined);

  updateCard('stat-temp', tempSensor ? `${tempSensor.readings.temperature.toFixed(1)}\u00B0C` : 'N/A');
  updateCard('stat-humidity', humiditySensor ? `${Math.round(humiditySensor.readings.humidity)}%` : 'N/A');
  updateCard('stat-vibration', vibrationSensor ? capitalize(vibrationSensor.readings.state) : 'N/A');
  updateCard('stat-uptime', formatUptime(status.uptime_seconds));
}

function renderEvents(events) {
  const el = document.getElementById('events-list');
  if (!el) return;

  const rows = events.slice(0, 5);

  if (rows.length === 0) {
    el.innerHTML = '<p>No recent events.</p>';
    el.removeAttribute('aria-busy');
    el.removeAttribute('data-spinner');
    return;
  }

  el.innerHTML = rows.map(ev => {
    const icon = typeIcon(ev.type);
    return `<div class="event-row">
      <span class="event-icon">${icon}</span>
      <span class="event-msg">${escapeHtml(ev.message)}</span>
      <span class="event-source">${escapeHtml(ev.source)}</span>
      <span class="event-time">${formatTime(ev.timestamp)}</span>
    </div>`;
  }).join('');

  el.removeAttribute('aria-busy');
  el.removeAttribute('data-spinner');
}

function updateCard(id, value) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = value;
  const card = el.closest('.stat-card');
  if (card) {
    card.removeAttribute('aria-busy');
    card.removeAttribute('data-spinner');
  }
}

function escapeHtml(str) {
  const div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
}

function capitalize(str) {
  if (!str) return '';
  return str.charAt(0).toUpperCase() + str.slice(1);
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

function typeIcon(type) {
  switch (type) {
    case 'alert': return '<span style="color:var(--danger)">&#9679;</span>';
    case 'warning': return '<span style="color:var(--warning)">&#9679;</span>';
    case 'info': return '<span style="color:var(--primary)">&#9679;</span>';
    default: return '<span style="color:var(--muted-foreground)">&#9679;</span>';
  }
}
