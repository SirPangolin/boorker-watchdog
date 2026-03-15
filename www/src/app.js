// OAT CSS — Vite bundles these into the output
import '@knadh/oat/oat.min.css';
import '@someshkar/oat-chips/dist/chip.min.css';
import 'oat-animate/oat-animate/oat-animate.css';
import './style.css';

// OAT JS — side-effect imports so Vite includes them
import '@knadh/oat/oat.min.js';
import 'oat-animate/oat-animate/oat-animate.js';

import { initTheme, toggleTheme } from './lib/theme.js';
import { checkSession, logout } from './lib/auth.js';
import { api } from './lib/api.js';
import * as loginPage from './pages/login.js';
import * as dashboardPage from './pages/dashboard.js';
import * as settingsPage from './pages/settings.js';

initTheme();

const LOGO_SVG = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="32" height="32">
  <polygon points="8,20 14,6 20,20" fill="#64748b"/>
  <polygon points="28,20 34,6 40,20" fill="#64748b"/>
  <polygon points="24,8 38,18 38,32 24,42 10,32 10,18" fill="#e2e8f0"/>
  <polygon points="16,28 24,42 32,28" fill="#f8fafc"/>
  <circle cx="17" cy="24" r="4" fill="#0891b2"/>
  <path d="M17,20 A4,4 0 0,0 13,24 L17,24 Z" fill="#b45309"/>
  <circle cx="17" cy="24" r="2" fill="#18171d"/>
  <circle cx="31" cy="24" r="4" fill="#b45309"/>
  <circle cx="31" cy="24" r="2" fill="#18171d"/>
  <polygon points="21,36 24,40 27,36" fill="#18171d"/>
  <polygon points="22,14 24,8 26,14 26,28 22,28" fill="#ffffff" opacity="0.6"/>
</svg>`;

const SUN_ICON = `<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="12" cy="12" r="5"/>
  <line x1="12" y1="1" x2="12" y2="3"/>
  <line x1="12" y1="21" x2="12" y2="23"/>
  <line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/>
  <line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/>
  <line x1="1" y1="12" x2="3" y2="12"/>
  <line x1="21" y1="12" x2="23" y2="12"/>
  <line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/>
  <line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/>
</svg>`;

const MOON_ICON = `<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>
</svg>`;

const LOGOUT_ICON = `<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/>
  <polyline points="16 17 21 12 16 7"/>
  <line x1="21" y1="12" x2="9" y2="12"/>
</svg>`;

const MENU_ICON = `<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <line x1="3" y1="6" x2="21" y2="6"/>
  <line x1="3" y1="12" x2="21" y2="12"/>
  <line x1="3" y1="18" x2="21" y2="18"/>
</svg>`;

const HELP_ICON = `<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="12" cy="12" r="10"/><path d="M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3"/><line x1="12" y1="17" x2="12.01" y2="17"/>
</svg>`;

// Transport icons for connection bar (14x14, compact)
const TRANSPORT_ICONS = {
  wifi: `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/>
    <path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><circle cx="12" cy="20" r="1" fill="currentColor"/>
  </svg>`,
  ap: `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M4.9 19.1C1 15.2 1 8.8 4.9 4.9"/><path d="M7.8 16.2c-2.3-2.3-2.3-6.1 0-8.5"/>
    <circle cx="12" cy="12" r="2" fill="currentColor"/><path d="M16.2 7.8c2.3 2.3 2.3 6.1 0 8.5"/><path d="M19.1 4.9C23 8.8 23 15.1 19.1 19"/>
  </svg>`,
  lora: `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M12 2v8"/><path d="M8 6l4-4 4 4"/><circle cx="12" cy="14" r="3"/>
    <path d="M6 18c0-3.3 2.7-6 6-6s6 2.7 6 6"/><line x1="12" y1="22" x2="12" y2="20"/>
  </svg>`,
  ble: `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M6.5 6.5l11 11L12 23V1l5.5 5.5-11 11"/>
  </svg>`,
  usb: `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <circle cx="10" cy="18" r="2"/><circle cx="20" cy="10" r="2"/><path d="M12 2v16"/>
    <path d="M12 8l8 2"/><path d="M8 2h8"/><line x1="12" y1="2" x2="12" y2="4"/>
  </svg>`,
  battery: `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <rect x="1" y="6" width="18" height="12" rx="2"/><line x1="23" y1="10" x2="23" y2="14"/>
  </svg>`,
  charging: `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <rect x="1" y="6" width="18" height="12" rx="2"/><line x1="23" y1="10" x2="23" y2="14"/>
    <polyline points="11 10 8 13 11 13 9 16"/>
  </svg>`,
};

const GITHUB_URL = 'https://github.com/SirPangolin/boorker-watchdog';

const routes = {
  '#login': { page: loginPage, auth: false },
  '#dashboard': { page: dashboardPage, auth: true },
  '#settings': { page: settingsPage, auth: true },
};

let currentPage = null;
export let session = null;

export function setSession(s) {
  session = s;
}
const app = document.getElementById('app');

/**
 * Render the app shell around page content.
 * SECURITY: All HTML here is static template strings only — no user input.
 * MOTD messages are rendered via textContent in loadMotd().
 */
function renderShell(pageHtml, showNav) {
  if (!showNav) {
    return pageHtml;
  }

  const hash = location.hash || '#dashboard';
  const themeIcon = document.documentElement.getAttribute('data-theme') === 'dark' ? SUN_ICON : MOON_ICON;

  return `
    <header class="app-header">
      <div class="header-left">
        <a href="#dashboard" class="logo">${LOGO_SVG}
          <div class="brand-text">
            <span class="brand-name">Boorker Watchdog</span>
            <span class="node-id" id="node-id"></span>
          </div>
        </a>
        <nav class="header-nav">
          <a href="#dashboard" class="${hash === '#dashboard' ? 'active' : ''}">Dashboard</a>
          <a href="#settings" class="${hash === '#settings' ? 'active' : ''}">Settings</a>
        </nav>
      </div>
      <div class="header-right">
        <div class="conn-bar" id="conn-bar">
          <span class="conn-item" id="conn-wifi" data-transport="wifi" data-tooltip="WiFi: checking...">${TRANSPORT_ICONS.wifi}</span>
          <span class="conn-item inactive" id="conn-ap" data-transport="ap" data-tooltip="WiFi AP: inactive">${TRANSPORT_ICONS.ap}</span>
          <span class="conn-item inactive" id="conn-lora" data-transport="lora" data-tooltip="LoRa: inactive">${TRANSPORT_ICONS.lora}</span>
          <span class="conn-item inactive" id="conn-ble" data-transport="ble" data-tooltip="BLE: inactive">${TRANSPORT_ICONS.ble}</span>
          <span class="conn-item inactive" id="conn-usb" data-transport="usb" data-tooltip="USB: inactive">${TRANSPORT_ICONS.usb}</span>
          <span class="conn-sep"></span>
          <span class="conn-item inactive" id="conn-power" data-transport="power" data-tooltip="Power: unknown">${TRANSPORT_ICONS.battery}</span>
        </div>
        <a href="${GITHUB_URL}/wiki" target="_blank" rel="noopener noreferrer" class="btn" data-tooltip="Documentation">${HELP_ICON}</a>
        <button id="theme-toggle" class="btn" data-tooltip="Toggle theme">${themeIcon}</button>
        <button id="logout-btn" class="btn" data-tooltip="Logout">${LOGOUT_ICON}</button>
        <button id="mobile-menu-btn" class="btn mobile-only" data-tooltip="Menu">${MENU_ICON}</button>
      </div>
    </header>
    <nav id="mobile-nav" class="mobile-nav" hidden>
      <a href="#dashboard" class="${hash === '#dashboard' ? 'active' : ''}">Dashboard</a>
      <a href="#settings" class="${hash === '#settings' ? 'active' : ''}">Settings</a>
    </nav>
    <div id="motd-area"></div>
    <main class="container">
      ${pageHtml}
    </main>
    <footer class="app-footer">
      <span id="footer-version">Boorker Watchdog</span>
      <span class="footer-sep">&middot;</span>
      <a href="${GITHUB_URL}" target="_blank" rel="noopener noreferrer">SirPangolin/boorker-watchdog</a>
      <span class="footer-sep">&middot;</span>
      <a href="${GITHUB_URL}/blob/main/LICENSE" target="_blank" rel="noopener noreferrer">MIT License</a>
      <span class="footer-sep">&middot;</span>
      <a href="${GITHUB_URL}/blob/main/CHANGELOG.md" target="_blank" rel="noopener noreferrer">Changelog</a>
      <span class="footer-sep">&middot;</span>
      <a href="${GITHUB_URL}/blob/main/ATTRIBUTIONS.md" target="_blank" rel="noopener noreferrer">Attributions</a>
    </footer>`;
}

export async function navigate() {
  let hash = location.hash || '#dashboard';
  let route = routes[hash];

  // Always unmount before any redirect to prevent stale timers
  if (currentPage) {
    try { currentPage.unmount(); } catch (_) { /* ignore */ }
    currentPage = null;
  }

  // Unknown route -> dashboard
  if (!route) {
    location.hash = '#dashboard';
    return;
  }

  // Auth guard: unauthenticated users must go to #login
  if (route.auth && (!session || !session.authenticated)) {
    location.hash = '#login';
    return;
  }

  // Authenticated users should not see #login
  if (!route.auth && session && session.authenticated) {
    location.hash = '#dashboard';
    return;
  }

  const page = route.page;
  const showNav = route.auth;

  // SECURITY NOTE: renderShell and page.render() return only static template
  // strings. No user-supplied data is interpolated into this HTML.
  app.innerHTML = renderShell(page.render(session), showNav);
  currentPage = page;
  page.mount(session);

  // Wire shell event listeners for authenticated pages
  if (showNav) {
    const themeBtn = document.getElementById('theme-toggle');
    if (themeBtn) {
      themeBtn.addEventListener('click', () => {
        toggleTheme();
        // Update icon after toggle
        const icon = document.documentElement.getAttribute('data-theme') === 'dark' ? SUN_ICON : MOON_ICON;
        themeBtn.innerHTML = icon;
      });
    }

    const logoutBtn = document.getElementById('logout-btn');
    if (logoutBtn) {
      logoutBtn.addEventListener('click', async () => {
        try {
          await logout();
        } catch (_) { /* ignore */ }
        session = { authenticated: false, claimed: true };
        location.hash = '#login';
      });
    }

    const menuBtn = document.getElementById('mobile-menu-btn');
    const mobileNav = document.getElementById('mobile-nav');
    if (menuBtn && mobileNav) {
      menuBtn.addEventListener('click', () => {
        mobileNav.hidden = !mobileNav.hidden;
      });
      // Close mobile nav when a link is clicked
      mobileNav.addEventListener('click', (e) => {
        if (e.target.tagName === 'A') mobileNav.hidden = true;
      });
    }

    loadMotd();
    loadShellStatus();
  }
}

async function loadShellStatus() {
  const nodeIdEl = document.getElementById('node-id');
  const wifiEl = document.getElementById('conn-wifi');

  try {
    const status = await api('GET', '/system/status');

    // Populate device mDNS name with reachability dot
    if (nodeIdEl && status.device_name) {
      nodeIdEl.innerHTML = '';
      const dot = document.createElement('span');
      dot.className = 'status-dot online';
      const text = document.createTextNode(` ${status.device_name}.local`);
      nodeIdEl.appendChild(dot);
      nodeIdEl.appendChild(text);
    }

    // WiFi transport
    if (wifiEl) {
      if (status.wifi_connected) {
        wifiEl.classList.remove('inactive');
        wifiEl.classList.add('active');
        const rssi = status.wifi_rssi || 0;
        const strength = rssi > -50 ? 'strong' : rssi > -70 ? 'fair' : 'weak';
        wifiEl.dataset.tooltip = `WiFi: ${status.wifi_ssid || 'connected'} (${rssi} dBm)`;
        wifiEl.dataset.strength = strength;
      } else {
        wifiEl.classList.add('inactive');
        wifiEl.classList.remove('active');
        wifiEl.dataset.tooltip = 'WiFi: disconnected';
        delete wifiEl.dataset.strength;
      }
    }

    // Update footer version from firmware
    const footerEl = document.getElementById('footer-version');
    if (footerEl && status.firmware_version) {
      footerEl.textContent = `Boorker Watchdog v${status.firmware_version}`;
    }

    // Future: update other transports from status data when available
    // updateTransport('conn-ap', status.ap_active, ...);
    // updateTransport('conn-lora', status.lora_connected, ...);
    // updateTransport('conn-ble', status.ble_active, ...);
    // updateTransport('conn-usb', status.usb_connected, ...);
    // updateTransport('conn-power', status.power_source, status.battery_pct, ...);

  } catch (_) {
    // Device unreachable — mark WiFi unknown, show offline dot
    if (nodeIdEl) {
      const dot = nodeIdEl.querySelector('.status-dot');
      if (dot) dot.className = 'status-dot offline';
    }
    if (wifiEl) {
      wifiEl.classList.add('inactive');
      wifiEl.classList.remove('active');
      wifiEl.dataset.tooltip = 'WiFi: unreachable';
      delete wifiEl.dataset.strength;
    }
  }
}

// Priority → CSS modifier class
const MOTD_PRIORITY_CLASS = {
  warning: 'warning',
  critical: 'critical',
};

// Info icon SVG (circled i)
const INFO_ICON = `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/>
</svg>`;

async function loadMotd() {
  try {
    const motds = await api('GET', '/system/motd');
    if (!Array.isArray(motds) || motds.length === 0) return;

    const area = document.getElementById('motd-area');
    if (!area) return;

    for (const motd of motds) {
      const priorityClass = MOTD_PRIORITY_CLASS[motd.priority] || '';

      const banner = document.createElement('div');
      banner.setAttribute('role', 'alert');
      banner.className = `motd-banner${priorityClass ? ` ${priorityClass}` : ''}`;

      // Icon
      const icon = document.createElement('span');
      icon.className = 'motd-icon';
      icon.innerHTML = INFO_ICON;

      // SECURITY: textContent for XSS safety — MOTD message is from API
      const msg = document.createElement('span');
      msg.className = 'motd-message';
      msg.textContent = motd.message;

      banner.appendChild(icon);
      banner.appendChild(msg);

      // Source-based action link (future: use motd.url if present)
      if (motd.url && /^https?:\/\//.test(motd.url)) {
        const link = document.createElement('a');
        link.href = motd.url;
        link.target = '_blank';
        link.rel = 'noopener noreferrer';
        link.textContent = motd.url_label || 'Learn more';
        banner.appendChild(link);
      } else if (motd.source === 'ota') {
        const link = document.createElement('a');
        link.href = '#settings';
        link.textContent = 'View OTA settings';
        banner.appendChild(link);
      }

      // Dismiss button — calls firmware DELETE endpoint
      const dismiss = document.createElement('button');
      dismiss.className = 'motd-dismiss';
      dismiss.textContent = '\u00d7';
      dismiss.title = 'Dismiss';
      dismiss.addEventListener('click', async () => {
        banner.remove();
        try {
          await api('DELETE', '/system/motd', { id: motd.id });
        } catch (_) {
          // Dismiss is best-effort — banner is already removed from UI
        }
      });

      banner.appendChild(dismiss);
      area.appendChild(banner);
    }
  } catch (_) {
    // MOTD is non-critical
  }
}

async function init() {
  session = await checkSession();
  app.removeAttribute('aria-busy');
  app.removeAttribute('data-spinner');
  window.addEventListener('hashchange', navigate);
  navigate();
}

init();
