#include "recal_scheduler.h"
#include "debug_logger.h"

// Default time source — replaced in unit tests via s_timeFunc
static time_t defaultTimeFunc() {
    return time(nullptr);
}

time_t (*RecalScheduler::s_timeFunc)() = defaultTimeFunc;

RecalScheduler::RecalScheduler(HAL_PIR* sensor)
    : m_sensor(sensor)
    , m_windowStartHour(2)
    , m_windowEndHour(4)
    , m_quiescencePeriodMs(3600000)   // 1 hour
    , m_cooldownMs(7200000)           // 2 hours
    , m_hasRecalibrated(false)
    , m_lastRecalMs(0)
    , m_triggered(false)
{
}

void RecalScheduler::begin() {
    DEBUG_LOG_SYSTEM("RecalScheduler: Initialized (window %02d:00–%02d:00, quiescence %us, cooldown %us)",
        m_windowStartHour, m_windowEndHour,
        m_quiescencePeriodMs / 1000, m_cooldownMs / 1000);
}

void RecalScheduler::update(bool ntpSynced, uint32_t lastMotionMs) {
    m_triggered = false;

    // 1. NTP must be synced — no reliable time without it
    if (!ntpSynced) {
        return;
    }

    // 2. Sensor must not already be recalibrating
    if (m_sensor->isRecalibrating()) {
        return;
    }

    // 3. Check time window (local hour)
    time_t now = s_timeFunc();
    struct tm* local = localtime(&now);
    if (!local) {
        return;  // localtime failed — skip this cycle
    }
    int hour = local->tm_hour;
    if (hour < m_windowStartHour || hour >= m_windowEndHour) {
        return;  // Outside the recal window
    }

    // 4. Check quiescence: no motion for at least m_quiescencePeriodMs
    // lastMotionMs == 0 means no motion event has ever been recorded this boot,
    // which satisfies quiescence.
    if (lastMotionMs != 0) {
        uint32_t sinceMotion = millis() - lastMotionMs;
        if (sinceMotion < m_quiescencePeriodMs) {
            return;  // Motion too recent
        }
    }

    // 5. Check cooldown since last recal
    if (m_hasRecalibrated) {
        uint32_t sinceLast = millis() - m_lastRecalMs;
        if (sinceLast < m_cooldownMs) {
            return;  // Cooldown not yet elapsed
        }
    }

    // All conditions met — trigger recalibration
    if (m_sensor->recalibrate()) {
        m_hasRecalibrated = true;
        m_lastRecalMs = millis();
        m_triggered = true;
        DEBUG_LOG_SYSTEM("RecalScheduler: Triggered automatic recalibration at %02d:%02d",
            local->tm_hour, local->tm_min);
    }
}
