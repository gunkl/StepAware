#include "ntp_manager.h"
#include "logger.h"

NTPManager::NTPManager()
    : m_enabled(false)
    , m_tzOffsetHours(-8)
    , m_synced(false)
    , m_syncPending(false)
    , m_wifiConnected(false)
    , m_lastCheckMs(0)
    , m_lastSyncEpoch(0)
    , m_syncInitiatedMs(0)
{
    strlcpy(m_server, "pool.ntp.org", sizeof(m_server));
}

void NTPManager::begin(bool enabled, const char* server, int8_t tzOffsetHours) {
    m_enabled = enabled;
    strlcpy(m_server, server, sizeof(m_server));
    m_tzOffsetHours = tzOffsetHours;

    // Configure timezone immediately so localtime() works if RTC has valid time,
    // even before configTime()/SNTP runs.  POSIX TZ sign is inverted vs UTC offset.
    char tz[16];
    snprintf(tz, sizeof(tz), "UTC%d", -m_tzOffsetHours);
    setenv("TZ", tz, 1);
    tzset();

    Serial.printf("[NTP] Initialized: %s, server=%s, tz=%+d\n",
                  m_enabled ? "enabled" : "disabled",
                  m_server,
                  m_tzOffsetHours);
}

void NTPManager::onWiFiConnected() {
    m_wifiConnected = true;

    if (!m_enabled) {
        return;
    }

    if (!m_synced && !m_syncPending) {
        Serial.println("[NTP] WiFi connected, initiating first sync...");
        initiateSync();
    }
}

void NTPManager::initiateSync() {
#if !MOCK_HARDWARE
    int32_t gmtOffsetSec = (int32_t)m_tzOffsetHours * 3600;
    configTime(gmtOffsetSec, 0, m_server);
#endif
    m_syncPending = true;
    m_syncInitiatedMs = millis();
    Serial.printf("[NTP] Sync initiated to %s (offset: %+d hours)\n", m_server, m_tzOffsetHours);
}

void NTPManager::update() {
    if (!m_enabled) {
        return;
    }

    // Check if a pending sync has completed or timed out
    if (m_syncPending) {
#if !MOCK_HARDWARE
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) {
            // Sync successful
            m_synced = true;
            m_syncPending = false;
            m_lastSyncEpoch = time(NULL);
            LOG_INFO("NTP synced successfully to %s", m_server);
        } else if (millis() - m_syncInitiatedMs >= SYNC_TIMEOUT_MS) {
            // Timeout — sync failed
            m_syncPending = false;
            LOG_ERROR("NTP sync failed - server not reachable (%s)", m_server);
        }
#else
        // In mock/native builds, sync never completes
        if (millis() - m_syncInitiatedMs >= SYNC_TIMEOUT_MS) {
            m_syncPending = false;
        }
#endif
    }

    // Hourly check
    uint32_t now = millis();
    if (now - m_lastCheckMs >= CHECK_INTERVAL_MS) {
        m_lastCheckMs = now;

        if (!m_wifiConnected) {
            return;  // Nothing we can do without WiFi
        }

        if (!m_synced && !m_syncPending) {
            // Time still not synced — retry
            Serial.println("[NTP] Hourly check: time not synced, retrying...");
            initiateSync();
        } else if (m_synced && !m_syncPending) {
            // Check if it's time for a daily resync
            time_t currentEpoch = time(NULL);
            if (currentEpoch > 0 && (currentEpoch - m_lastSyncEpoch) >= SYNC_INTERVAL_SEC) {
                Serial.println("[NTP] Daily resync triggered");
                initiateSync();
            }

            // Sanity check: if time has become invalid, force resync
            if (currentEpoch <= 946684800) {  // Before Jan 1 2000
                Serial.println("[NTP] WARNING: Time became invalid, forcing resync");
                m_synced = false;
                initiateSync();
            }
        }
    }
}
