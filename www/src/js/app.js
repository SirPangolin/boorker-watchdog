// Boorker Web Interface - Main Application

const API_BASE = '/api/v1';

// State
let state = {
    authenticated: false,
    nodeName: 'boorker',
    status: null
};

// API helpers
async function api(endpoint, options = {}) {
    const resp = await fetch(API_BASE + endpoint, {
        ...options,
        headers: {
            'Content-Type': 'application/json',
            ...options.headers
        }
    });

    if (resp.status === 401) {
        window.location.href = '/login.html';
        throw new Error('Unauthorized');
    }

    return resp.json();
}

// Router
const routes = {
    '/': renderDashboard,
    '/terminal': renderTerminal,
    '/settings': renderSettings,
    '/update': renderUpdate
};

function navigate() {
    const hash = window.location.hash || '#/';
    const path = hash.slice(1);

    // Update nav active state
    document.querySelectorAll('.nav-item').forEach(item => {
        const href = item.getAttribute('href');
        item.classList.toggle('active', href === hash);
    });

    // Render route
    const render = routes[path];
    if (render) {
        render();
    } else {
        document.getElementById('content').innerHTML = '<div class="card"><h2>Page Not Found</h2></div>';
    }
}

// Dashboard
async function renderDashboard() {
    const content = document.getElementById('content');
    content.innerHTML = `
        <div class="stat-grid">
            <div class="card">
                <h2>Uptime</h2>
                <div class="stat-value" id="uptime">--</div>
            </div>
            <div class="card">
                <h2>Free Memory</h2>
                <div class="stat-value" id="heap">--</div>
            </div>
            <div class="card">
                <h2>Power</h2>
                <div class="stat-value" id="power">--</div>
            </div>
        </div>
        <div class="card">
            <h2>System Info</h2>
            <p>Node: <span id="info-node">--</span></p>
            <p>MAC: <span id="info-mac">--</span></p>
            <p>Cores: <span id="info-cores">--</span></p>
        </div>
    `;

    try {
        const status = await api('/system/status');
        const info = await api('/system/info');

        // Format uptime
        const hours = Math.floor(status.uptime / 3600);
        const mins = Math.floor((status.uptime % 3600) / 60);
        document.getElementById('uptime').textContent = `${hours}h ${mins}m`;

        // Format heap
        const heapKB = Math.round(status.heap_free / 1024);
        document.getElementById('heap').textContent = `${heapKB} KB`;

        // Power
        document.getElementById('power').textContent = status.power?.source || 'Unknown';

        // Info
        document.getElementById('info-node').textContent = status.node_name || '--';
        document.getElementById('info-mac').textContent = info.mac || '--';
        document.getElementById('info-cores').textContent = info.cores || '--';

        // Update header
        if (status.node_name) {
            document.getElementById('node-name').textContent = status.node_name;
        }
        document.getElementById('status-indicator').textContent = '● Connected';
        document.getElementById('status-indicator').style.color = 'var(--success)';

    } catch (err) {
        console.error('Failed to load status:', err);
        document.getElementById('status-indicator').textContent = '● Disconnected';
        document.getElementById('status-indicator').style.color = 'var(--danger)';
    }
}

// Terminal (placeholder)
function renderTerminal() {
    const content = document.getElementById('content');
    content.innerHTML = `
        <div class="card">
            <h2>Terminal</h2>
            <div class="terminal" id="terminal-output">
                <div>Welcome to Boorker Terminal</div>
                <div>Type 'help' for available commands</div>
                <div>&nbsp;</div>
            </div>
            <div class="terminal-input">
                <span>$</span>
                <input type="text" id="terminal-cmd" placeholder="Enter command...">
                <button id="terminal-send">Send</button>
            </div>
        </div>
    `;

    // Terminal will be implemented with WebSocket in future
    document.getElementById('terminal-send').addEventListener('click', () => {
        const output = document.getElementById('terminal-output');
        const cmd = document.getElementById('terminal-cmd').value;
        const div = document.createElement('div');
        div.textContent = '$ ' + cmd + ' (WebSocket not implemented)';
        output.appendChild(div);
        document.getElementById('terminal-cmd').value = '';
    });
}

// Settings (placeholder)
function renderSettings() {
    const content = document.getElementById('content');
    content.innerHTML = `
        <div class="card">
            <h2>WiFi Settings</h2>
            <p>Coming soon...</p>
        </div>
        <div class="card">
            <h2>Tailscale Settings</h2>
            <p>Coming soon...</p>
        </div>
        <div class="card">
            <h2>Security</h2>
            <button id="logout-btn">Logout</button>
        </div>
    `;

    document.getElementById('logout-btn').addEventListener('click', async () => {
        try {
            await api('/auth/logout', { method: 'POST' });
        } catch (e) {
            console.error('Logout failed:', e);
        }
        window.location.href = '/login.html';
    });
}

// Update (placeholder)
function renderUpdate() {
    const content = document.getElementById('content');
    content.innerHTML = `
        <div class="card">
            <h2>Firmware Update</h2>
            <p>Current version: <span id="fw-version">--</span></p>
            <p>OTA update functionality coming soon...</p>
        </div>
    `;
}

// Init
window.addEventListener('hashchange', navigate);
window.addEventListener('load', () => {
    navigate();

    // User menu
    const userMenu = document.getElementById('user-menu');
    if (userMenu) {
        userMenu.addEventListener('click', () => {
            window.location.hash = '#/settings';
        });
    }

    // Mobile menu toggle
    const menuToggle = document.getElementById('menu-toggle');
    const sidebar = document.getElementById('sidebar');
    if (menuToggle && sidebar) {
        menuToggle.addEventListener('click', () => {
            sidebar.classList.toggle('open');
        });
    }
});
