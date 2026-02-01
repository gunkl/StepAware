#ifndef STEPAWARE_NTP_MANAGER_H
#define STEPAWARE_NTP_MANAGER_H

#include <Arduino.h>
#include <time.h>

/**
 * @brief NTP Time Synchronization Manager for StepAware
 *
 * Manages periodic NTP clock synchronization using ESP32's built-in SNTP client.
 * No external library required — uses configTime() and standard time.h functions.
 *
 * Sync schedule:
 * - Initial sync triggered when WiFi first connects (if NTP is enabled)
 * - Hourly check: retries if time is not yet valid, or initiates daily resync
 * - Daily resync: ~24 hours after last successful sync
 * - Invalid time detection: if time() returns an invalid value, triggers resync
 *
 * DNS: Hostname-based NTP servers (e.g., "pool.ntp.org") are resolved using
 * the DNS servers obtained from WiFi DHCP automatically by ESP32's lwIP stack.
 * IP addresses (e.g., "216.239.35.0") work directly without DNS.
 */
class NTPManager {
public:
    /**
     * @brief Construct NTP Manager
     */
    NTPManager();

    /**
     * @brief Initialize NTP Manager with configuration
     *
     * Does NOT initiate a sync — call onWiFiConnected() when WiFi is available.
     *
     * @param enabled Whether NTP sync is enabled
     * @param server NTP server hostname or IP address
     * @param tzOffsetHours UTC timezone offset in whole hours (e.g., -8 for PST)
     */
    void begin(bool enabled, const char* server, int8_t tzOffsetHours);

    /**
     * @brief Update NTP state machine (call every loop iteration)
     *
     * Handles sync completion detection, hourly checks, and daily resync scheduling.
     */
    void update();

    /**
     * @brief Notify NTP Manager that WiFi is connected
     *
     * Triggers an initial sync attempt if NTP is enabled and time has not yet been synced.
     */
    void onWiFiConnected();

    /**
     * @brief Check if time has been successfully synced at least once
     *
     * @return true if NTP sync has completed successfully
     */
    bool isTimeSynced() const { return m_synced; }

private:
    bool m_enabled;                  ///< NTP sync enabled
    char m_server[64];               ///< NTP server hostname or IP
    int8_t m_tzOffsetHours;          ///< UTC offset in whole hours
    bool m_synced;                   ///< Has time been synced at least once this boot?
    bool m_syncPending;              ///< Sync has been initiated, waiting for completion
    bool m_wifiConnected;            ///< WiFi is currently connected
    uint32_t m_lastCheckMs;          ///< millis() of last hourly check
    time_t m_lastSyncEpoch;          ///< Epoch time of last successful sync (0 if never)
    uint32_t m_syncInitiatedMs;      ///< millis() when current sync was initiated (for timeout)

    static constexpr uint32_t CHECK_INTERVAL_MS  = 3600000;  // 1 hour between checks
    static constexpr uint32_t SYNC_INTERVAL_SEC  = 86400;    // 24 hours between resyncs
    static constexpr uint32_t SYNC_TIMEOUT_MS    = 30000;    // 30s timeout for sync attempt

    /**
     * @brief Initiate an NTP sync via configTime()
     */
    void initiateSync();
};

#endif // STEPAWARE_NTP_MANAGER_H
