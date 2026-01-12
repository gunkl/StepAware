// StepAware Dashboard JavaScript

const API_BASE = '/api';
let refreshInterval = null;
let currentConfig = null;

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    console.log('StepAware Dashboard initializing...');

    // Load initial data
    refreshAll();

    // Set up auto-refresh every 2 seconds
    refreshInterval = setInterval(refreshStatus, 2000);

    // Setup range input value displays
    setupRangeInputs();

    console.log('Dashboard initialized');
});

// Refresh all data
function refreshAll() {
    refreshStatus();
    refreshConfig();
    refreshLogs();
    refreshVersion();
}

// Refresh system status
async function refreshStatus() {
    try {
        const response = await fetch(`${API_BASE}/status`);
        if (!response.ok) throw new Error('Failed to fetch status');

        const data = await response.json();
        updateStatusUI(data);
        updateConnectionStatus(true);
    } catch (error) {
        console.error('Error fetching status:', error);
        updateConnectionStatus(false);
    }
}

// Update status UI
function updateStatusUI(data) {
    // Update stats
    document.getElementById('uptime').textContent = formatUptime(data.uptime);
    document.getElementById('memory').textContent = formatMemory(data.freeHeap);
    document.getElementById('motion-events').textContent = data.motionEvents || 0;
    document.getElementById('mode-changes').textContent = data.modeChanges || 0;

    // Update mode
    document.getElementById('current-mode-name').textContent = data.modeName || 'UNKNOWN';
    updateModeButtons(data.mode);

    // Update warning status
    const warningEl = document.getElementById('warning-status');
    if (data.warningActive) {
        warningEl.style.display = 'flex';
    } else {
        warningEl.style.display = 'none';
    }
}

// Update connection status indicator
function updateConnectionStatus(connected) {
    const indicator = document.getElementById('connection-status');
    if (connected) {
        indicator.classList.add('connected');
        indicator.classList.remove('disconnected');
        indicator.title = 'Connected';
    } else {
        indicator.classList.add('disconnected');
        indicator.classList.remove('connected');
        indicator.title = 'Disconnected';
    }
}

// Update mode button active state
function updateModeButtons(mode) {
    document.querySelectorAll('.mode-btn').forEach(btn => {
        if (parseInt(btn.dataset.mode) === mode) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });
}

// Set operating mode
async function setMode(mode) {
    try {
        const response = await fetch(`${API_BASE}/mode`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode: mode })
        });

        if (!response.ok) throw new Error('Failed to set mode');

        const data = await response.json();
        showToast('success', `Mode changed to ${data.modeName}`);
        updateModeButtons(data.mode);
        document.getElementById('current-mode-name').textContent = data.modeName;
    } catch (error) {
        console.error('Error setting mode:', error);
        showToast('error', 'Failed to change mode');
    }
}

// Refresh configuration
async function refreshConfig() {
    try {
        const response = await fetch(`${API_BASE}/config`);
        if (!response.ok) throw new Error('Failed to fetch config');

        currentConfig = await response.json();
        populateConfigForm(currentConfig);
    } catch (error) {
        console.error('Error fetching config:', error);
        showToast('error', 'Failed to load configuration');
    }
}

// Populate configuration form
function populateConfigForm(config) {
    // Motion
    document.getElementById('warning-duration').value = config.motion?.warningDuration || 15000;
    document.getElementById('pir-warmup').value = config.motion?.pirWarmup || 60000;

    // LED
    document.getElementById('led-brightness-full').value = config.led?.brightnessFull || 255;
    document.getElementById('led-brightness-medium').value = config.led?.brightnessMedium || 128;
    document.getElementById('led-brightness-dim').value = config.led?.brightnessDim || 20;

    // Button
    document.getElementById('button-debounce').value = config.button?.debounceMs || 50;
    document.getElementById('button-long-press').value = config.button?.longPressMs || 1000;

    // Power
    document.getElementById('power-saving-enabled').checked = config.power?.savingEnabled || false;
    document.getElementById('deep-sleep-after').value = (config.power?.deepSleepAfterMs || 3600000) / 60000;

    // WiFi
    document.getElementById('wifi-ssid').value = config.wifi?.ssid || '';
    document.getElementById('wifi-password').value = config.wifi?.password || '';
    document.getElementById('wifi-enabled').checked = config.wifi?.enabled || false;

    // Update range displays
    updateRangeDisplays();
}

// Save configuration
async function saveConfig() {
    try {
        // Build config object
        const config = {
            motion: {
                warningDuration: parseInt(document.getElementById('warning-duration').value),
                pirWarmup: parseInt(document.getElementById('pir-warmup').value)
            },
            led: {
                brightnessFull: parseInt(document.getElementById('led-brightness-full').value),
                brightnessMedium: parseInt(document.getElementById('led-brightness-medium').value),
                brightnessDim: parseInt(document.getElementById('led-brightness-dim').value),
                blinkFastMs: currentConfig.led.blinkFastMs,
                blinkSlowMs: currentConfig.led.blinkSlowMs,
                blinkWarningMs: currentConfig.led.blinkWarningMs
            },
            button: {
                debounceMs: parseInt(document.getElementById('button-debounce').value),
                longPressMs: parseInt(document.getElementById('button-long-press').value)
            },
            battery: currentConfig.battery,
            light: currentConfig.light,
            wifi: {
                ssid: document.getElementById('wifi-ssid').value,
                password: document.getElementById('wifi-password').value,
                enabled: document.getElementById('wifi-enabled').checked
            },
            device: currentConfig.device,
            power: {
                savingEnabled: document.getElementById('power-saving-enabled').checked,
                deepSleepAfterMs: parseInt(document.getElementById('deep-sleep-after').value) * 60000
            },
            logging: currentConfig.logging,
            metadata: currentConfig.metadata
        };

        const response = await fetch(`${API_BASE}/config`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });

        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.error || 'Failed to save config');
        }

        currentConfig = await response.json();
        showToast('success', 'Configuration saved successfully');
    } catch (error) {
        console.error('Error saving config:', error);
        showToast('error', `Failed to save: ${error.message}`);
    }
}

// Factory reset
async function factoryReset() {
    if (!confirm('Are you sure you want to reset to factory defaults? This cannot be undone.')) {
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/reset`, {
            method: 'POST'
        });

        if (!response.ok) throw new Error('Failed to reset');

        showToast('success', 'Configuration reset to factory defaults');
        setTimeout(() => {
            refreshConfig();
        }, 1000);
    } catch (error) {
        console.error('Error resetting config:', error);
        showToast('error', 'Failed to reset configuration');
    }
}

// Refresh logs
async function refreshLogs() {
    try {
        const response = await fetch(`${API_BASE}/logs`);
        if (!response.ok) throw new Error('Failed to fetch logs');

        const data = await response.json();
        updateLogsUI(data.logs);
    } catch (error) {
        console.error('Error fetching logs:', error);
    }
}

// Update logs UI
function updateLogsUI(logs) {
    const container = document.getElementById('logs-container');

    if (!logs || logs.length === 0) {
        container.innerHTML = '<p class="logs-empty">No logs available</p>';
        return;
    }

    container.innerHTML = logs.map(log => `
        <div class="log-entry">
            <span class="log-timestamp">${formatTimestamp(log.timestamp)}</span>
            <span class="log-level ${log.levelName}">${log.levelName}</span>
            <span class="log-message">${escapeHtml(log.message)}</span>
        </div>
    `).join('');

    // Auto-scroll to bottom
    container.scrollTop = container.scrollHeight;
}

// Refresh version info
async function refreshVersion() {
    try {
        const response = await fetch(`${API_BASE}/version`);
        if (!response.ok) throw new Error('Failed to fetch version');

        const data = await response.json();
        document.getElementById('firmware-version').textContent = data.version;
        document.getElementById('build-date').textContent = data.buildDate;
    } catch (error) {
        console.error('Error fetching version:', error);
    }
}

// Toggle configuration panel
function toggleConfig() {
    const body = document.getElementById('config-body');
    const icon = document.getElementById('config-toggle-icon');

    if (body.style.display === 'none') {
        body.style.display = 'block';
        icon.textContent = '▲';
    } else {
        body.style.display = 'none';
        icon.textContent = '▼';
    }
}

// Show configuration tab
function showConfigTab(tabName) {
    // Hide all tabs
    document.querySelectorAll('.config-tab').forEach(tab => {
        tab.style.display = 'none';
    });

    // Remove active class from all buttons
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.classList.remove('active');
    });

    // Show selected tab
    document.getElementById(`tab-${tabName}`).style.display = 'block';

    // Add active class to button
    event.target.classList.add('active');
}

// Setup range input value displays
function setupRangeInputs() {
    const ranges = [
        'led-brightness-full',
        'led-brightness-medium',
        'led-brightness-dim'
    ];

    ranges.forEach(id => {
        const input = document.getElementById(id);
        const display = document.getElementById(`${id}-value`);

        if (input && display) {
            input.addEventListener('input', function() {
                display.textContent = this.value;
            });
        }
    });
}

// Update all range displays
function updateRangeDisplays() {
    ['led-brightness-full', 'led-brightness-medium', 'led-brightness-dim'].forEach(id => {
        const input = document.getElementById(id);
        const display = document.getElementById(`${id}-value`);
        if (input && display) {
            display.textContent = input.value;
        }
    });
}

// Show toast notification
function showToast(type, message) {
    const container = document.getElementById('toast-container');

    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.innerHTML = `
        <span>${message}</span>
    `;

    container.appendChild(toast);

    // Remove after 3 seconds
    setTimeout(() => {
        toast.style.opacity = '0';
        setTimeout(() => toast.remove(), 300);
    }, 3000);
}

// Utility functions

function formatUptime(ms) {
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);

    if (days > 0) return `${days}d ${hours % 24}h`;
    if (hours > 0) return `${hours}h ${minutes % 60}m`;
    if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
    return `${seconds}s`;
}

function formatMemory(bytes) {
    if (bytes >= 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
    if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${bytes} B`;
}

function formatTimestamp(ms) {
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);

    const h = String(hours % 24).padStart(2, '0');
    const m = String(minutes % 60).padStart(2, '0');
    const s = String(seconds % 60).padStart(2, '0');
    const milli = String(ms % 1000).padStart(3, '0');

    return `${h}:${m}:${s}.${milli}`;
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// Cleanup on page unload
window.addEventListener('beforeunload', function() {
    if (refreshInterval) {
        clearInterval(refreshInterval);
    }
});
