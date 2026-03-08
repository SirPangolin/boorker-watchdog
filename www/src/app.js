// OAT CSS — Vite bundles these into the output
import '@knadh/oat/oat.min.css';
import '@someshkar/oat-chips/dist/chip.min.css';
import 'oat-animate/oat-animate/oat-animate.css';

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

const VERSION = '0.7.0';

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
        <a href="#dashboard" class="logo">${LOGO_SVG}</a>
        <span class="brand">Boorker Watchdog</span>
      </div>
      <nav class="header-nav">
        <a href="#dashboard" class="${hash === '#dashboard' ? 'active' : ''}">Dashboard</a>
        <a href="#settings" class="${hash === '#settings' ? 'active' : ''}">Settings</a>
      </nav>
      <div class="header-right">
        <button id="theme-toggle" class="btn btn-sm" title="Toggle theme">${themeIcon}</button>
        <button id="logout-btn" class="btn btn-sm" title="Logout">${LOGOUT_ICON}</button>
      </div>
    </header>
    <div id="motd-area"></div>
    <main class="container">
      ${pageHtml}
    </main>
    <footer class="app-footer">
      <span>Boorker Watchdog v${VERSION}</span>
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

    loadMotd();
  }
}

async function loadMotd() {
  try {
    const motds = await api('GET', '/system/motd');
    if (!Array.isArray(motds) || motds.length === 0) return;

    const motd = motds[0];
    const area = document.getElementById('motd-area');
    if (!area) return;

    const banner = document.createElement('div');
    banner.setAttribute('role', 'alert');
    banner.style.cssText = 'display:flex;align-items:center;justify-content:space-between;padding:0.75rem 1rem;background:var(--muted);border-bottom:1px solid var(--border);';

    const msg = document.createElement('span');
    // SECURITY: textContent for XSS safety — MOTD message is from API
    msg.textContent = motd.message;

    const dismiss = document.createElement('button');
    dismiss.className = 'btn btn-sm';
    dismiss.textContent = '\u00d7';
    dismiss.title = 'Dismiss';
    dismiss.addEventListener('click', () => banner.remove());

    banner.appendChild(msg);
    banner.appendChild(dismiss);
    area.appendChild(banner);
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
