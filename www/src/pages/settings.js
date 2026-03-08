import { api } from '../lib/api.js';

let otaPollTimer = null;
let deviceName = '';

export function render() {
  return `
    <!-- Section 1: Device Info -->
    <details name="settings" open>
      <summary>Device Info</summary>
      <div class="settings-body" id="device-info" aria-busy="true" data-spinner="small"></div>
    </details>

    <!-- Section 2: Device Actions -->
    <details name="settings">
      <summary>Device Actions</summary>
      <div class="settings-body">
        <div class="settings-row">
          <div>
            <strong>Reboot</strong>
            <small>Restart the device. This takes about 10 seconds.</small>
          </div>
          <button class="outline" commandfor="reboot-dialog" command="show-modal">Reboot</button>
        </div>
        <div class="settings-row">
          <div>
            <strong>Factory Reset</strong>
            <small>Erase all settings and restore defaults.</small>
          </div>
          <button data-variant="danger" commandfor="reset-dialog" command="show-modal">Factory Reset</button>
        </div>
      </div>
    </details>

    <!-- Section 3: Firmware Updates -->
    <details name="settings">
      <summary>Firmware Updates</summary>
      <div class="settings-body" id="ota-section">
        <div class="settings-row">
          <div>
            <strong>Current Version</strong>
            <small id="ota-current-version">--</small>
          </div>
          <div>
            <small id="ota-last-check"></small>
          </div>
        </div>
        <div id="ota-update-alert" class="settings-row" hidden>
          <div>
            <strong id="ota-update-version"></strong>
            <small id="ota-release-notes"></small>
          </div>
          <button id="ota-install-btn">Install Update</button>
        </div>
        <div class="settings-row">
          <div>
            <strong>Check for Updates</strong>
            <small>Query the update server for new firmware.</small>
          </div>
          <button id="ota-check-btn" class="outline">Check Now</button>
        </div>
        <div id="ota-progress" hidden>
          <progress id="ota-progress-bar" value="0" max="100"></progress>
          <small id="ota-progress-text">Preparing...</small>
        </div>
        <div class="settings-row">
          <div>
            <strong>Upload Firmware</strong>
            <small>Manually upload a .bin firmware file.</small>
          </div>
          <input type="file" id="ota-file-input" accept=".bin">
        </div>
      </div>
    </details>

    <!-- Section 4: WiFi -->
    <details name="settings">
      <summary>WiFi</summary>
      <div class="settings-body" id="wifi-info" aria-busy="true" data-spinner="small"></div>
    </details>

    <!-- Section 5: Security -->
    <details name="settings">
      <summary>Security</summary>
      <div class="settings-body">
        <div class="settings-row">
          <div>
            <strong>Change Password</strong>
            <small>Update the device admin password.</small>
          </div>
          <a href="#login" role="button" class="outline">Change Password</a>
        </div>
      </div>
    </details>

    <!-- Reboot Dialog -->
    <dialog id="reboot-dialog" closedby="any">
      <form method="dialog">
        <header>
          <h3>Reboot Device</h3>
          <p>The device will restart and be temporarily unavailable. Are you sure?</p>
        </header>
        <footer>
          <button type="button" class="outline" commandfor="reboot-dialog" command="close">Cancel</button>
          <button id="reboot-confirm-btn" value="confirm">Reboot Now</button>
        </footer>
      </form>
    </dialog>

    <!-- Factory Reset Dialog -->
    <dialog id="reset-dialog" closedby="any">
      <form method="dialog">
        <header>
          <h3>Factory Reset</h3>
          <p>This will erase all settings, WiFi credentials, and sensor data. This action cannot be undone.</p>
        </header>
        <div style="padding: 0 1rem;">
          <label for="reset-confirm-input">Type the device name to confirm:</label>
          <input type="text" id="reset-confirm-input" autocomplete="off" placeholder="Device name">
        </div>
        <footer>
          <button type="button" class="outline" commandfor="reset-dialog" command="close">Cancel</button>
          <button id="reset-confirm-btn" data-variant="danger" value="confirm" disabled>Factory Reset</button>
        </footer>
      </form>
    </dialog>
  `;
}

export function mount() {
  loadDeviceInfo();
  loadOtaStatus();
  loadWifiInfo();
  bindActions();
}

export function unmount() {
  if (otaPollTimer) {
    clearInterval(otaPollTimer);
    otaPollTimer = null;
  }
}

function escapeHtml(str) {
  const div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
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

function formatBytes(bytes, unit) {
  if (bytes == null) return 'N/A';
  if (unit === 'MB') return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
  return (bytes / 1024).toFixed(0) + ' KB';
}

/**
 * Build a settings row using safe DOM construction, then serialize to HTML.
 * All label/value strings are escaped via escapeHtml to prevent XSS.
 */
function settingsRow(label, value) {
  // SECURITY: Both label and value are escaped — safe for innerHTML assembly
  return `<div class="settings-row">
    <div><strong>${escapeHtml(label)}</strong></div>
    <div>${escapeHtml(value)}</div>
  </div>`;
}

async function loadDeviceInfo() {
  const el = document.getElementById('device-info');
  if (!el) return;

  try {
    const [info, status] = await Promise.all([
      api('GET', '/system/info'),
      api('GET', '/system/status'),
    ]);

    deviceName = status.device_name || '';

    // SECURITY: All values pass through escapeHtml via settingsRow
    const rows = [
      settingsRow('Device Name', status.device_name || 'Unknown'),
      settingsRow('Firmware', 'v' + (info.version || '?')),
      settingsRow('ESP-IDF', 'v' + (info.idf_version || '?')),
      settingsRow('Uptime', formatUptime(status.uptime_seconds)),
      settingsRow('Heap Free', formatBytes(status.heap_free, 'KB') + ' / ' + formatBytes(status.heap_total, 'KB')),
      settingsRow('PSRAM Free', formatBytes(status.psram_free, 'MB') + ' / ' + formatBytes(status.psram_total, 'MB')),
    ];

    // Safe: settingsRow escapes all dynamic values via escapeHtml
    el.innerHTML = rows.join('');  // nosemgrep: innerHTML-xss
    el.removeAttribute('aria-busy');
    el.removeAttribute('data-spinner');

    // Update footer version if element exists
    const footerVersion = document.getElementById('footer-version');
    if (footerVersion) {
      footerVersion.textContent = 'v' + info.version;
    }
  } catch (err) {
    el.textContent = 'Failed to load device info.';
    el.removeAttribute('aria-busy');
    el.removeAttribute('data-spinner');
    console.error('Device info load failed:', err);
  }
}

async function loadOtaStatus() {
  try {
    const ota = await api('GET', '/ota');
    renderOtaStatus(ota);
  } catch (err) {
    console.error('OTA status load failed:', err);
  }
}

function renderOtaStatus(ota) {
  // SECURITY: All values set via textContent — no innerHTML with API data
  const versionEl = document.getElementById('ota-current-version');
  const lastCheckEl = document.getElementById('ota-last-check');
  const updateAlert = document.getElementById('ota-update-alert');
  const updateVersion = document.getElementById('ota-update-version');
  const releaseNotes = document.getElementById('ota-release-notes');

  if (versionEl) {
    versionEl.textContent = 'v' + (ota.version || '?');
  }

  if (lastCheckEl && ota.last_check) {
    const d = new Date(ota.last_check * 1000);
    lastCheckEl.textContent = 'Last checked: ' + d.toLocaleString();
  }

  if (ota.update && updateAlert) {
    updateAlert.hidden = false;
    if (updateVersion) {
      updateVersion.textContent = 'Update available: v' + ota.update.version;
    }
    if (releaseNotes) {
      releaseNotes.textContent = ota.update.release_notes || '';
    }
  }

  if (ota.state === 'downloading' || ota.state === 'flashing') {
    showProgress(ota);
    startPolling();
  }
}

function showProgress(ota) {
  const progressEl = document.getElementById('ota-progress');
  const progressBar = document.getElementById('ota-progress-bar');
  const progressText = document.getElementById('ota-progress-text');

  if (progressEl) progressEl.hidden = false;
  if (progressBar && ota.progress != null) {
    progressBar.value = ota.progress;
  }
  if (progressText) {
    progressText.textContent = ota.state === 'downloading' ? 'Downloading...' : 'Flashing...';
  }
}

function startPolling() {
  if (otaPollTimer) return;
  otaPollTimer = setInterval(async () => {
    try {
      const ota = await api('GET', '/ota');
      renderOtaStatus(ota);

      if (ota.state !== 'downloading' && ota.state !== 'flashing') {
        clearInterval(otaPollTimer);
        otaPollTimer = null;

        const progressEl = document.getElementById('ota-progress');
        if (progressEl) progressEl.hidden = true;

        if (ota.state === 'idle' || ota.state === 'ready') {
          showToast('Firmware update complete. Reboot to apply.');
        }
      }
    } catch (err) {
      console.error('OTA poll failed:', err);
    }
  }, 2000);
}

async function loadWifiInfo() {
  const el = document.getElementById('wifi-info');
  if (!el) return;

  try {
    const status = await api('GET', '/system/status');

    // Build WiFi info using DOM methods for safety
    el.removeAttribute('aria-busy');
    el.removeAttribute('data-spinner');

    // Status row with badge (badge class is static, not user data)
    const statusRow = document.createElement('div');
    statusRow.className = 'settings-row';
    const statusLabel = document.createElement('div');
    statusLabel.innerHTML = '<strong>Status</strong>';
    const statusValue = document.createElement('div');
    const badge = document.createElement('span');
    badge.className = status.wifi_connected ? 'badge success' : 'badge danger';
    badge.textContent = status.wifi_connected ? 'Connected' : 'Disconnected';
    statusValue.appendChild(badge);
    statusRow.appendChild(statusLabel);
    statusRow.appendChild(statusValue);
    el.appendChild(statusRow);

    // Remaining rows via DOM
    appendInfoRow(el, 'Network', status.wifi_ssid || 'N/A');
    appendInfoRow(el, 'Signal', status.wifi_rssi != null ? status.wifi_rssi + ' dBm' : 'N/A');
    appendInfoRow(el, 'IP Address', status.ip_address || 'N/A');
  } catch (err) {
    el.textContent = 'Failed to load WiFi info.';
    el.removeAttribute('aria-busy');
    el.removeAttribute('data-spinner');
    console.error('WiFi info load failed:', err);
  }
}

/** Append a settings row using safe DOM methods (textContent only). */
function appendInfoRow(parent, label, value) {
  const row = document.createElement('div');
  row.className = 'settings-row';
  const labelDiv = document.createElement('div');
  const strong = document.createElement('strong');
  strong.textContent = label;
  labelDiv.appendChild(strong);
  const valueDiv = document.createElement('div');
  valueDiv.textContent = value;
  row.appendChild(labelDiv);
  row.appendChild(valueDiv);
  parent.appendChild(row);
}

function bindActions() {
  // Reboot confirm
  const rebootBtn = document.getElementById('reboot-confirm-btn');
  if (rebootBtn) {
    rebootBtn.addEventListener('click', async () => {
      try {
        await api('POST', '/system/reboot');
        showToast('Rebooting device...');
        const dialog = document.getElementById('reboot-dialog');
        if (dialog) dialog.close();
      } catch (err) {
        showToast('Reboot failed: ' + err.message);
      }
    });
  }

  // Factory reset - enable button when name matches
  const resetInput = document.getElementById('reset-confirm-input');
  const resetBtn = document.getElementById('reset-confirm-btn');
  if (resetInput && resetBtn) {
    resetInput.addEventListener('input', () => {
      resetBtn.disabled = resetInput.value !== deviceName;
    });

    resetBtn.addEventListener('click', async () => {
      try {
        await api('POST', '/system/factory-reset');
        showToast('Factory reset complete, rebooting...');
        const dialog = document.getElementById('reset-dialog');
        if (dialog) dialog.close();
      } catch (err) {
        showToast('Factory reset failed: ' + err.message);
      }
    });
  }

  // Reset dialog - clear input when closed
  const resetDialog = document.getElementById('reset-dialog');
  if (resetDialog) {
    resetDialog.addEventListener('close', () => {
      if (resetInput) resetInput.value = '';
      if (resetBtn) resetBtn.disabled = true;
    });
  }

  // OTA Check Now
  const checkBtn = document.getElementById('ota-check-btn');
  if (checkBtn) {
    checkBtn.addEventListener('click', async () => {
      checkBtn.setAttribute('aria-busy', 'true');
      try {
        const ota = await api('GET', '/ota?refresh=true');
        renderOtaStatus(ota);
        showToast('Update check complete.');
      } catch (err) {
        showToast('Update check failed: ' + err.message);
      } finally {
        checkBtn.removeAttribute('aria-busy');
      }
    });
  }

  // OTA Install Update
  const installBtn = document.getElementById('ota-install-btn');
  if (installBtn) {
    installBtn.addEventListener('click', async () => {
      installBtn.setAttribute('aria-busy', 'true');
      try {
        await api('PUT', '/ota');
        showToast('Update started...');
        startPolling();
      } catch (err) {
        showToast('Update failed: ' + err.message);
      } finally {
        installBtn.removeAttribute('aria-busy');
      }
    });
  }

  // OTA Upload firmware
  const fileInput = document.getElementById('ota-file-input');
  if (fileInput) {
    fileInput.addEventListener('change', async () => {
      const file = fileInput.files[0];
      if (!file) return;

      const progressEl = document.getElementById('ota-progress');
      const progressText = document.getElementById('ota-progress-text');
      if (progressEl) progressEl.hidden = false;
      if (progressText) progressText.textContent = 'Uploading firmware...';

      try {
        const buffer = await file.arrayBuffer();
        await fetch('/api/v1/ota', {
          method: 'POST',
          headers: { 'Content-Type': 'application/octet-stream' },
          body: buffer,
        });
        showToast('Firmware uploaded. Reboot to apply.');
      } catch (err) {
        showToast('Upload failed: ' + err.message);
      } finally {
        if (progressEl) progressEl.hidden = true;
        fileInput.value = '';
      }
    });
  }
}

function showToast(message) {
  if (typeof ot !== 'undefined' && ot.toast) {
    ot.toast(message);
  } else {
    console.log('[Toast]', message);
  }
}
