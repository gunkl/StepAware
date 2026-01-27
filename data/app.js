// StepAware Dashboard JavaScript
console.log('app.js: File loaded successfully');
console.log('app.js: Loading from:', window.location.href);

const API_BASE = '/api';
let refreshInterval = null;
let currentConfig = null;

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    console.log('StepAware Dashboard initializing...');

    // Add global error handler
    window.addEventListener('error', function(event) {
        console.error('Global error caught:', event.error);
        console.error('Error message:', event.message);
        console.error('Error filename:', event.filename);
        console.error('Error line:', event.lineno, 'column:', event.colno);
    });

    // Add unhandled promise rejection handler
    window.addEventListener('unhandledrejection', function(event) {
        console.error('Unhandled promise rejection:', event.reason);
        console.error('Promise:', event.promise);
    });

    // Load initial data
    refreshAll();

    // Set up auto-refresh every 2 seconds
    refreshInterval = setInterval(refreshStatus, 2000);

    // Setup range input value displays
    setupRangeInputs();

    console.log('Dashboard initialized');
    console.log('DOM elements check:');
    console.log('- warning-duration:', document.getElementById('warning-duration') !== null);
    console.log('- config-body:', document.getElementById('config-body') !== null);
    console.log('- Save button exists:', document.querySelector('button[onclick="saveConfig()"]') !== null);
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
        console.log('refreshConfig: Fetching config from', `${API_BASE}/config`);
        const response = await fetch(`${API_BASE}/config`);
        console.log('refreshConfig: Response status:', response.status, response.statusText);

        if (!response.ok) {
            const errorText = await response.text();
            console.error('refreshConfig: Error response:', errorText);
            throw new Error(`Failed to fetch config: ${response.status}`);
        }

        const responseText = await response.text();
        console.log('refreshConfig: Raw response text:', responseText);

        try {
            currentConfig = JSON.parse(responseText);
            console.log('refreshConfig: Parsed config:', currentConfig);
            populateConfigForm(currentConfig);
        } catch (parseError) {
            console.error('refreshConfig: JSON parse error:', parseError);
            console.error('refreshConfig: Response was not valid JSON');
            throw new Error('Invalid JSON response from server');
        }
    } catch (error) {
        console.error('refreshConfig: Exception:', error);
        console.error('refreshConfig: Error stack:', error.stack);
        showToast('error', `Failed to load configuration: ${error.message}`);
    }
}

// Populate configuration form
function populateConfigForm(config) {
    console.log('populateConfigForm: Starting with config:', config);

    try {
        // Motion
        setElementValue('warning-duration', config.motion?.warningDuration || 15000);
        setElementValue('pir-warmup', config.motion?.pirWarmup || 60000);

        // LED
        setElementValue('led-brightness-full', config.led?.brightnessFull || 255);
        setElementValue('led-brightness-medium', config.led?.brightnessMedium || 128);
        setElementValue('led-brightness-dim', config.led?.brightnessDim || 20);

        // Button
        setElementValue('button-debounce', config.button?.debounceMs || 50);
        setElementValue('button-long-press', config.button?.longPressMs || 1000);

        // Power
        setElementChecked('power-saving-enabled', config.power?.savingEnabled || false);
        setElementValue('deep-sleep-after', (config.power?.deepSleepAfterMs || 3600000) / 60000);

        // WiFi
        setElementValue('wifi-ssid', config.wifi?.ssid || '');
        setElementValue('wifi-password', config.wifi?.password || '');
        setElementChecked('wifi-enabled', config.wifi?.enabled || false);

        // Hardware - Sensor 0
        if (config.sensors && config.sensors[0]) {
            const sensor0 = config.sensors[0];
            setElementChecked('sensor0-enabled', sensor0.enabled || false);
            setElementValue('sensor0-name', sensor0.name || '');
            setElementValue('sensor0-threshold', sensor0.detectionThreshold || 1500);
            setElementValue('sensor0-debounce', sensor0.debounceMs || 60);
            setElementValue('sensor0-window-size', sensor0.sampleWindowSize || 5);
            setElementChecked('sensor0-direction-enabled', sensor0.enableDirectionDetection || false);
            setElementChecked('sensor0-invert-logic', sensor0.invertLogic || false);

            // Update response time display
            updateResponseTime();
        }

        // Update range displays
        updateRangeDisplays();

        console.log('populateConfigForm: Completed successfully');
    } catch (error) {
        console.error('populateConfigForm: Error:', error);
        throw error;
    }
}

// Helper function to safely set element value
function setElementValue(id, value) {
    const element = document.getElementById(id);
    if (!element) {
        console.error(`setElementValue: Element '${id}' not found in DOM`);
        return;
    }
    console.log(`setElementValue: ${id} = ${value}`);
    element.value = value;
}

// Helper function to safely set checkbox state
function setElementChecked(id, checked) {
    const element = document.getElementById(id);
    if (!element) {
        console.error(`setElementChecked: Element '${id}' not found in DOM`);
        return;
    }
    console.log(`setElementChecked: ${id}.checked = ${checked}`);
    element.checked = checked;
}

// Save configuration
async function saveConfig() {
    console.log('saveConfig: Function called');

    // Check if currentConfig is loaded
    if (!currentConfig) {
        console.error('saveConfig: currentConfig is null - config not loaded yet');
        showToast('error', 'Configuration not loaded. Please refresh the page.');
        return;
    }

    try {
        console.log('saveConfig: Starting save operation...');
        console.log('saveConfig: currentConfig =', currentConfig);

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
            metadata: currentConfig.metadata,
            sensors: currentConfig.sensors || []
        };

        // Update sensor 0 config if hardware tab values exist
        if (document.getElementById('sensor0-enabled')) {
            if (!config.sensors[0]) {
                config.sensors[0] = { ...currentConfig.sensors[0] };
            }
            config.sensors[0].enabled = document.getElementById('sensor0-enabled').checked;
            config.sensors[0].name = document.getElementById('sensor0-name').value;
            config.sensors[0].detectionThreshold = parseInt(document.getElementById('sensor0-threshold').value);
            config.sensors[0].debounceMs = parseInt(document.getElementById('sensor0-debounce').value);
            config.sensors[0].sampleWindowSize = parseInt(document.getElementById('sensor0-window-size').value);
            config.sensors[0].enableDirectionDetection = document.getElementById('sensor0-direction-enabled').checked;
            config.sensors[0].invertLogic = document.getElementById('sensor0-invert-logic').checked;
        }

        console.log('saveConfig: Built config object:', config);
        console.log('saveConfig: Sending POST to', `${API_BASE}/config`);

        const response = await fetch(`${API_BASE}/config`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });

        console.log('saveConfig: Response received, status:', response.status, response.statusText);

        if (!response.ok) {
            const errorText = await response.text();
            console.error('saveConfig: Error response text:', errorText);
            try {
                const error = JSON.parse(errorText);
                throw new Error(error.error || 'Failed to save config');
            } catch (parseError) {
                throw new Error(`Server returned ${response.status}: ${errorText}`);
            }
        }

        const responseData = await response.json();
        console.log('saveConfig: Response data:', responseData);

        currentConfig = responseData;
        console.log('saveConfig: Successfully saved, updated currentConfig');
        showToast('success', 'Configuration saved successfully');
    } catch (error) {
        console.error('saveConfig: Exception caught:', error);
        console.error('saveConfig: Error stack:', error.stack);
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
        'led-brightness-dim',
        'sensor0-window-size'
    ];

    ranges.forEach(id => {
        const input = document.getElementById(id);
        const display = document.getElementById(`${id}-value`);

        if (input && display) {
            input.addEventListener('input', function() {
                display.textContent = this.value;
                // Update response time when window size changes
                if (id === 'sensor0-window-size') {
                    updateResponseTime();
                }
            });
        }
    });

    // Also update response time when debounce changes
    const debounceInput = document.getElementById('sensor0-debounce');
    if (debounceInput) {
        debounceInput.addEventListener('input', updateResponseTime);
    }
}

// Update all range displays
function updateRangeDisplays() {
    ['led-brightness-full', 'led-brightness-medium', 'led-brightness-dim', 'sensor0-window-size'].forEach(id => {
        const input = document.getElementById(id);
        const display = document.getElementById(`${id}-value`);
        if (input && display) {
            display.textContent = input.value;
        }
    });
}

// Update response time calculation
function updateResponseTime() {
    const windowSizeInput = document.getElementById('sensor0-window-size');
    const debounceInput = document.getElementById('sensor0-debounce');
    const responseTimeDisplay = document.getElementById('sensor0-response-time');

    if (windowSizeInput && debounceInput && responseTimeDisplay) {
        const windowSize = parseInt(windowSizeInput.value);
        const sampleRate = parseInt(debounceInput.value);
        const responseTime = windowSize * sampleRate;
        responseTimeDisplay.textContent = `${responseTime}ms`;
    }
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
