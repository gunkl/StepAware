#include "stepaware_matrix.h"

#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace stepaware_matrix {

static const char *const TAG = "stepaware_matrix";

// HT16K33 commands (same sequence as Adafruit_LEDBackpack::begin()).
static const uint8_t HT16K33_CMD_OSCILLATOR_ON = 0x21;  // system setup: oscillator on
static const uint8_t HT16K33_CMD_STANDBY = 0x20;        // system setup: oscillator off
static const uint8_t HT16K33_CMD_DISPLAY_OFF = 0x80;    // display off
static const uint8_t HT16K33_CMD_DISPLAY_ON = 0x81;     // display on, blink off
static const uint8_t HT16K33_CMD_BRIGHTNESS = 0xE0;     // | 0..15
static const uint8_t HT16K33_RAM_ADDR = 0x00;
static const size_t HT16K33_RAM_SIZE = 16;

static const uint32_t MATRIX_FLASH_DURATION_MS = 200;  // config.h

// ============================================================================
// Bitmaps (verbatim from src/hal_ledmatrix_8x8.cpp)
// ============================================================================

static const uint8_t ARROW_DOWN[8] = {
    0b00011000, 0b00011000, 0b00011000, 0b00011000,
    0b11111111, 0b01111110, 0b00111100, 0b00011000,
};

static const uint8_t ARROW_DOWN_INVERSE[8] = {
    0b11100111, 0b11100111, 0b11100111, 0b11100111,
    0b00000000, 0b10000001, 0b11000011, 0b11100111,
};

static const uint8_t BOOT_OK[8] = {
    0b00000000, 0b00111101, 0b01000010, 0b10000101,
    0b10101001, 0b01010010, 0b00111100, 0b00000000,
};

static const uint8_t WIFI_CONNECTED[8] = {
    0b00000000, 0b00111100, 0b01000000, 0b10011000,
    0b00100000, 0b00000100, 0b00001000, 0b00000000,
};

static const uint8_t WIFI_DISCONNECTED[8] = {
    0b10000001, 0b01111110, 0b00100000, 0b00011001,
    0b01100110, 0b10000100, 0b01111000, 0b10000010,
};

static const uint8_t ERROR_X[8] = {
    0b00000000, 0b01100110, 0b00111100, 0b00011000,
    0b00011000, 0b00111100, 0b01100110, 0b00000000,
};

static const uint8_t BATTERY_33[8] = {
    0b00011000, 0b01111110, 0b01000010, 0b01000010,
    0b01111110, 0b01111110, 0b01111110, 0b01111110,
};

static const uint8_t BATTERY_25[8] = {
    0b00011000, 0b01111110, 0b01000010, 0b01000010,
    0b01000010, 0b01111110, 0b01111110, 0b01111110,
};

static const uint8_t BATTERY_10[8] = {
    0b00011000, 0b01111110, 0b01000010, 0b01000010,
    0b01000010, 0b01000010, 0b01111110, 0b01111110,
};

static const uint8_t BATTERY_EMPTY[8] = {
    0b00011000, 0b01111110, 0b01000010, 0b01000010,
    0b01000010, 0b01000010, 0b01000010, 0b01111110,
};

static const uint8_t BATTERY_FULL[8] = {
    0b00011000, 0b01111110, 0b01111110, 0b01111110,
    0b01111110, 0b01111110, 0b01111110, 0b01111110,
};

static const uint8_t BATTERY_75[8] = {
    0b00011000, 0b01111110, 0b01000010, 0b01111110,
    0b01111110, 0b01111110, 0b01111110, 0b01111110,
};

static const uint8_t BATTERY_50[8] = {
    0b00011000, 0b01111110, 0b01000010, 0b01000010,
    0b01111110, 0b01111110, 0b01111110, 0b01111110,
};

// ============================================================================
// Former LittleFS custom animations (data/animations/*.txt), frame for frame
// ============================================================================

static const AnimationFrame ANIM_HEART_FRAMES[] = {
    {{0b00000000, 0b00100100, 0b00111100, 0b00011000,
      0b00000000, 0b00000000, 0b00000000, 0b00000000}, 100},  // small
    {{0b00000000, 0b01100110, 0b11111111, 0b01111110,
      0b00111100, 0b00011000, 0b00000000, 0b00000000}, 100},  // medium
    {{0b01100110, 0b11111111, 0b11111111, 0b11111111,
      0b01111110, 0b00111100, 0b00011000, 0b00000000}, 100},  // large
    {{0b00000000, 0b01100110, 0b11111111, 0b01111110,
      0b00111100, 0b00011000, 0b00000000, 0b00000000}, 100},  // medium
    {{0b00000000, 0b00100100, 0b00111100, 0b00011000,
      0b00000000, 0b00000000, 0b00000000, 0b00000000}, 100},  // small
};

static const AnimationFrame ANIM_SPINNER_FRAMES[] = {
    {{0b00000000, 0b00000000, 0b00000000, 0b11111111,
      0b00000000, 0b00000000, 0b00000000, 0b00000000}, 80},  // horizontal
    {{0b10000000, 0b01000000, 0b00100000, 0b00010000,
      0b00001000, 0b00000100, 0b00000010, 0b00000001}, 80},  // diagonal
    {{0b00011000, 0b00011000, 0b00011000, 0b00011000,
      0b00011000, 0b00011000, 0b00011000, 0b00011000}, 80},  // vertical
    {{0b00000001, 0b00000010, 0b00000100, 0b00001000,
      0b00010000, 0b00100000, 0b01000000, 0b10000000}, 80},  // other diagonal
    {{0b00000000, 0b00000000, 0b00000000, 0b11111111,
      0b00000000, 0b00000000, 0b00000000, 0b00000000}, 80},
    {{0b10000000, 0b01000000, 0b00100000, 0b00010000,
      0b00001000, 0b00000100, 0b00000010, 0b00000001}, 80},
    {{0b00011000, 0b00011000, 0b00011000, 0b00011000,
      0b00011000, 0b00011000, 0b00011000, 0b00011000}, 80},
    {{0b00000001, 0b00000010, 0b00000100, 0b00001000,
      0b00010000, 0b00100000, 0b01000000, 0b10000000}, 80},
};

static const AnimationFrame ANIM_WAVE_FRAMES[] = {
    {{0b00011000, 0b00111100, 0b01111110, 0b11111111,
      0b00000000, 0b00000000, 0b00000000, 0b00000000}, 120},
    {{0b00000000, 0b00011000, 0b00111100, 0b01111110,
      0b11111111, 0b00000000, 0b00000000, 0b00000000}, 120},
    {{0b00000000, 0b00000000, 0b00011000, 0b00111100,
      0b01111110, 0b11111111, 0b00000000, 0b00000000}, 120},
    {{0b00000000, 0b00000000, 0b00000000, 0b00011000,
      0b00111100, 0b01111110, 0b11111111, 0b00000000}, 120},
    {{0b00000000, 0b00000000, 0b00000000, 0b00000000,
      0b00011000, 0b00111100, 0b01111110, 0b11111111}, 120},
    {{0b00000000, 0b00000000, 0b00000000, 0b00000000,
      0b00000000, 0b00011000, 0b00111100, 0b01111110}, 120},
    {{0b00000000, 0b00000000, 0b00000000, 0b00000000,
      0b00000000, 0b00000000, 0b00011000, 0b00111100}, 120},
    {{0b00000000, 0b00000000, 0b00000000, 0b00000000,
      0b00000000, 0b00000000, 0b00000000, 0b00011000}, 120},
};

static const AnimationFrame ANIM_ALERT_FRAMES[] = {
    {{0b10000001, 0b01000010, 0b00100100, 0b00011000,
      0b00011000, 0b00100100, 0b01000010, 0b10000001}, 150},  // X
    {{0b01111110, 0b10111101, 0b11011011, 0b11100111,
      0b11100111, 0b11011011, 0b10111101, 0b01111110}, 150},  // inverted X
    {{0b11111111, 0b11111111, 0b11111111, 0b11111111,
      0b11111111, 0b11111111, 0b11111111, 0b11111111}, 100},  // all on
    {{0b00000000, 0b00000000, 0b00000000, 0b00000000,
      0b00000000, 0b00000000, 0b00000000, 0b00000000}, 100},  // all off
};

#define STEPAWARE_FRAME_COUNT(table) static_cast<uint8_t>(sizeof(table) / sizeof((table)[0]))

// Snake-fill order: pixel index (0-63) -> {row, col}. Even rows L->R, odd rows R->L.
static const uint8_t SNAKE_ORDER[64][2] = {
    {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7},
    {1, 7}, {1, 6}, {1, 5}, {1, 4}, {1, 3}, {1, 2}, {1, 1}, {1, 0},
    {2, 0}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {2, 5}, {2, 6}, {2, 7},
    {3, 7}, {3, 6}, {3, 5}, {3, 4}, {3, 3}, {3, 2}, {3, 1}, {3, 0},
    {4, 0}, {4, 1}, {4, 2}, {4, 3}, {4, 4}, {4, 5}, {4, 6}, {4, 7},
    {5, 7}, {5, 6}, {5, 5}, {5, 4}, {5, 3}, {5, 2}, {5, 1}, {5, 0},
    {6, 0}, {6, 1}, {6, 2}, {6, 3}, {6, 4}, {6, 5}, {6, 6}, {6, 7},
    {7, 7}, {7, 6}, {7, 5}, {7, 4}, {7, 3}, {7, 2}, {7, 1}, {7, 0},
};

const char *animationTypeToString(AnimationType type) {
    switch (type) {
        case ANIM_NONE: return "NONE";
        case ANIM_MOTION_ALERT: return "MOTION_ALERT";
        case ANIM_BATTERY_LOW: return "BATTERY_LOW";
        case ANIM_BOOT_STATUS: return "BOOT_STATUS";
        case ANIM_ERROR: return "ERROR";
        case ANIM_WIFI_CONNECTED: return "WIFI_CONNECTED";
        case ANIM_WIFI_DISCONNECTED: return "WIFI_DISCONNECTED";
        case ANIM_HEART: return "HEART";
        case ANIM_SPINNER: return "SPINNER";
        case ANIM_WAVE: return "WAVE";
        case ANIM_ALERT: return "ALERT";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// ESPHome lifecycle
// ============================================================================

void StepAwareMatrix::setup() {
    if (!initHardware()) {
        // Do not block or mark failed: loop() retries every REINIT_INTERVAL_MS,
        // like the old HAL's periodic re-init.
        ESP_LOGW(TAG, "HT16K33 at 0x%02X not responding; will retry every %us",
                 this->address_, (unsigned) (REINIT_INTERVAL_MS / 1000));
        this->status_set_warning();
        m_lastReinitAttempt = millis();
    }
}

void StepAwareMatrix::loop() {
    if (m_standby) {
        if (millis() - m_standbySince >= STANDBY_RESUME_MS) {
            // Still running: the deep sleep this standby was for did not happen.
            m_standby = false;
            ESP_LOGW(TAG, "Still awake after standby: re-initialising the HT16K33");
            if (!initHardware()) {
                m_lastReinitAttempt = millis();
            }
        }
        return;
    }
    if (!m_ready) {
        uint32_t now = millis();
        if (now - m_lastReinitAttempt >= REINIT_INTERVAL_MS) {
            m_lastReinitAttempt = now;
            if (initHardware()) {
                ESP_LOGI(TAG, "HT16K33 recovered");
            } else {
                ESP_LOGD(TAG, "HT16K33 re-init failed, retrying in %us",
                         (unsigned) (REINIT_INTERVAL_MS / 1000));
            }
        }
        return;
    }

    if (m_pattern != ANIM_NONE) {
        updateAnimation();
    }
}

void StepAwareMatrix::dump_config() {
    ESP_LOGCONFIG(TAG, "StepAware 8x8 LED Matrix (HT16K33):");
    LOG_I2C_DEVICE(this);
    ESP_LOGCONFIG(TAG, "  Brightness: %u/15", m_brightness);
    ESP_LOGCONFIG(TAG, "  Rotation: %u deg", m_rotation * 90);
    ESP_LOGCONFIG(TAG, "  Ready: %s", YESNO(m_ready));
    if (!m_ready) {
        ESP_LOGE(TAG, "  Communication with HT16K33 failed (retrying)");
    }
}

// ============================================================================
// Hardware
// ============================================================================

bool StepAwareMatrix::initHardware() {
    // Oscillator on, then blank RAM BEFORE turning the display on so a stale frame
    // (HT16K33 keeps RAM while powered across ESP resets/OTA) never shows.
    if (!writeCommand(HT16K33_CMD_OSCILLATOR_ON)) {
        return false;
    }
    memset(m_frame, 0, sizeof(m_frame));
    if (!writeDisplay(true)) {
        return false;
    }
    if (!writeCommand(HT16K33_CMD_DISPLAY_ON) ||
        !writeCommand(HT16K33_CMD_BRIGHTNESS | m_brightness)) {
        return false;
    }
    m_ready = true;
    m_consecutiveFailures = 0;
    this->status_clear_warning();
    ESP_LOGD(TAG, "HT16K33 initialised, display RAM cleared");
    return true;
}

bool StepAwareMatrix::writeCommand(uint8_t cmd) {
    bool ok = this->write(&cmd, 1) == i2c::ERROR_OK;
    noteWriteResult(ok);
    return ok;
}

bool StepAwareMatrix::writeDisplay(bool force) {
    // Build the HT16K33 RAM image exactly as Adafruit_8x8matrix::drawPixel() +
    // Adafruit_LEDBackpack::writeDisplay() did: rotation transform, then pixel x of
    // row y goes to bit ((x + 7) % 8) of RAM byte 2*y (odd bytes unused = 0).
    uint8_t ram[HT16K33_RAM_SIZE];
    memset(ram, 0, sizeof(ram));
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (!(m_frame[y] & (0x80 >> x))) {
                continue;
            }
            int px = x;
            int py = y;
            int tmp;
            switch (m_rotation) {
                case 1: tmp = px; px = py; py = tmp; px = 7 - px; break;
                case 2: px = 7 - px; py = 7 - py; break;
                case 3: tmp = px; px = py; py = tmp; py = 7 - py; break;
                default: break;
            }
            ram[py * 2] |= static_cast<uint8_t>(1 << ((px + 7) % 8));
        }
    }

    if (!force && m_ramValid && memcmp(ram, m_ram, sizeof(ram)) == 0) {
        return true;  // unchanged; avoid redundant I2C traffic
    }

    bool ok = this->write_register(HT16K33_RAM_ADDR, ram, sizeof(ram)) == i2c::ERROR_OK;
    noteWriteResult(ok);
    if (ok) {
        memcpy(m_ram, ram, sizeof(ram));
        m_ramValid = true;
    } else {
        m_ramValid = false;
    }
    return ok;
}

void StepAwareMatrix::noteWriteResult(bool ok) {
    if (ok) {
        if (m_consecutiveFailures > 0) {
            m_consecutiveFailures = 0;
            this->status_clear_warning();
        }
        return;
    }
    m_writeFailures++;
    m_consecutiveFailures++;
    this->status_set_warning();
    ESP_LOGW(TAG, "I2C write failed (total failures: %u)", (unsigned) m_writeFailures);
    if (m_ready && m_consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
        // Device probably lost power / bus stuck: fall back to periodic re-init.
        ESP_LOGE(TAG, "%u consecutive I2C failures, marking matrix not ready",
                 m_consecutiveFailures);
        m_ready = false;
        m_ramValid = false;
        m_lastReinitAttempt = millis();
    }
}

// ============================================================================
// Public API
// ============================================================================

void StepAwareMatrix::clear() {
    if (!m_ready) {
        return;
    }
    memset(m_frame, 0, sizeof(m_frame));
    writeDisplay();
}

void StepAwareMatrix::standby() {
    m_pattern = ANIM_NONE;
    memset(m_frame, 0, sizeof(m_frame));
    bool ok = true;
    if (m_ready) {
        writeDisplay();  // blank RAM so nothing stale shows on the next oscillator-on
        ok = writeCommand(HT16K33_CMD_DISPLAY_OFF);
    }
    // Sent even if the matrix was marked not ready: it may have recovered on its own.
    uint8_t cmd = HT16K33_CMD_STANDBY;
    ok = (this->write(&cmd, 1) == i2c::ERROR_OK) && ok;
    m_ready = false;
    m_ramValid = false;
    m_standby = true;
    m_standbySince = millis();
    ESP_LOGD(TAG, "HT16K33 standby%s", ok ? "" : " (I2C write failed)");
}

void StepAwareMatrix::setBrightness(uint8_t level0to15) {
    if (level0to15 > 15) {
        level0to15 = 15;
    }
    m_brightness = level0to15;
    if (m_ready) {
        writeCommand(HT16K33_CMD_BRIGHTNESS | m_brightness);
    }
    ESP_LOGD(TAG, "Brightness set to %u", m_brightness);
    m_brightnessCallbacks.call(m_brightness);
}

void StepAwareMatrix::setRotation(uint8_t rotation) {
    if (rotation > 3) {
        rotation = 0;
    }
    m_rotation = rotation;
    if (m_ready) {
        writeDisplay();
    }
    ESP_LOGD(TAG, "Rotation set to %u", m_rotation);
}

void StepAwareMatrix::startAnimation(AnimationType type, uint32_t durationMs) {
    if (!m_ready) {
        return;
    }
    if (type == ANIM_NONE) {
        stopAnimation();
        return;
    }
    uint32_t now = millis();
    m_pattern = type;
    m_animStart = now;
    m_animDuration = durationMs;
    m_animFrame = 0;
    m_frameDelay = 0;  // frame tables: first frame immediately
    m_lastFrameTime = now;
    ESP_LOGD(TAG, "Animation started: %s, duration: %u ms", animationTypeToString(type),
             (unsigned) durationMs);
}

void StepAwareMatrix::stopAnimation() {
    if (m_pattern != ANIM_NONE) {
        ESP_LOGD(TAG, "Animation stopped");
        m_pattern = ANIM_NONE;
        clear();
    }
}

void StepAwareMatrix::drawFrame(const uint8_t frame[8]) {
    if (!m_ready) {
        return;
    }
    memcpy(m_frame, frame, sizeof(m_frame));
    writeDisplay();
}

void StepAwareMatrix::setPixel(uint8_t x, uint8_t y, bool on) {
    if (!m_ready || x >= 8 || y >= 8) {
        return;
    }
    uint8_t mask = static_cast<uint8_t>(0x80 >> x);
    if (on) {
        m_frame[y] |= mask;
    } else {
        m_frame[y] &= static_cast<uint8_t>(~mask);
    }
    writeDisplay();
}

void StepAwareMatrix::setSnakeProgress(uint8_t pixels) {
    if (!m_ready) {
        return;
    }
    if (pixels > 64) {
        pixels = 64;
    }
    uint8_t frame[8] = {0};
    for (uint8_t i = 0; i < pixels; i++) {
        frame[SNAKE_ORDER[i][0]] |= static_cast<uint8_t>(1 << (7 - SNAKE_ORDER[i][1]));
    }
    drawFrame(frame);
    if (pixels % 16 == 0) {
        ESP_LOGD(TAG, "Snake progress: %u/64 pixels", pixels);
    }
}

void StepAwareMatrix::showBatteryBitmap(uint8_t percentage) {
    if (percentage > 75) {
        drawFrame(BATTERY_FULL);
    } else if (percentage > 50) {
        drawFrame(BATTERY_75);
    } else if (percentage > 25) {
        drawFrame(BATTERY_50);
    } else {
        drawFrame(BATTERY_33);
    }
}

void StepAwareMatrix::showWifiConnected() { drawFrame(WIFI_CONNECTED); }

void StepAwareMatrix::showWifiDisconnected() { drawFrame(WIFI_DISCONNECTED); }

// ============================================================================
// Animations (non-blocking, driven from loop())
// ============================================================================

void StepAwareMatrix::updateAnimation() {
    if (m_animDuration > 0 && millis() - m_animStart >= m_animDuration) {
        stopAnimation();
        return;
    }

    switch (m_pattern) {
        case ANIM_MOTION_ALERT: animateMotionAlert(); break;
        case ANIM_BATTERY_LOW: animateBatteryLow(); break;
        case ANIM_BOOT_STATUS: drawFrame(BOOT_OK); break;
        case ANIM_ERROR: drawFrame(ERROR_X); break;
        case ANIM_WIFI_CONNECTED: drawFrame(WIFI_CONNECTED); break;
        case ANIM_WIFI_DISCONNECTED: drawFrame(WIFI_DISCONNECTED); break;
        case ANIM_HEART:
            animateFrameTable(ANIM_HEART_FRAMES, STEPAWARE_FRAME_COUNT(ANIM_HEART_FRAMES));
            break;
        case ANIM_SPINNER:
            animateFrameTable(ANIM_SPINNER_FRAMES, STEPAWARE_FRAME_COUNT(ANIM_SPINNER_FRAMES));
            break;
        case ANIM_WAVE:
            animateFrameTable(ANIM_WAVE_FRAMES, STEPAWARE_FRAME_COUNT(ANIM_WAVE_FRAMES));
            break;
        case ANIM_ALERT:
            animateFrameTable(ANIM_ALERT_FRAMES, STEPAWARE_FRAME_COUNT(ANIM_ALERT_FRAMES));
            break;
        default: break;
    }
}

void StepAwareMatrix::animateMotionAlert() {
    uint32_t now = millis();
    uint32_t sinceFrame = now - m_lastFrameTime;
    uint32_t sinceStart = now - m_animStart;

    if (sinceStart < 800) {
        // Phase 1: alternate arrow / inverse arrow every 200 ms
        if (sinceFrame >= MATRIX_FLASH_DURATION_MS) {
            m_lastFrameTime = now;
            m_animFrame++;
            drawFrame((m_animFrame % 2 == 0) ? ARROW_DOWN : ARROW_DOWN_INVERSE);
        }
    } else if (sinceStart < 1600) {
        // Phase 2: scroll arrow down one row every 100 ms
        if (sinceFrame >= 100) {
            m_lastFrameTime = now;
            uint8_t scrollFrame = static_cast<uint8_t>((sinceStart - 800) / 100);
            uint8_t shifted[8];
            for (int i = 0; i < 8; i++) {
                int src = i - scrollFrame;
                shifted[i] = (src >= 0 && src < 8) ? ARROW_DOWN[src] : 0;
            }
            drawFrame(shifted);
        }
    } else {
        // Phase 3: flash arrow / blank; loop back to phase 1 after 2400 ms when indefinite
        uint32_t phaseElapsed = sinceStart - 1600;
        if (m_animDuration == 0 && phaseElapsed >= 800) {
            m_animStart = now;
            m_animFrame = 0;
            m_lastFrameTime = now;
            drawFrame(ARROW_DOWN);
            return;
        }
        if (sinceFrame >= MATRIX_FLASH_DURATION_MS) {
            m_lastFrameTime = now;
            if (m_animFrame % 2 == 0) {
                clear();
            } else {
                drawFrame(ARROW_DOWN);
            }
            m_animFrame++;
        }
    }
}

void StepAwareMatrix::animateFrameTable(const AnimationFrame *frames, uint8_t count) {
    // HAL_LEDMatrix_8x8::animateCustom() playback: each frame stays up for its own delay
    // and the table cycles (all four files are loop=true; with a duration it keeps
    // cycling until the duration expires). Difference: the first frame is drawn at once
    // instead of after its delay.
    uint32_t now = millis();
    if (now - m_lastFrameTime < m_frameDelay) {
        return;
    }
    uint8_t index = static_cast<uint8_t>(m_animFrame % count);
    m_lastFrameTime = now;
    drawFrame(frames[index].rows);
    m_frameDelay = frames[index].delayMs;
    m_animFrame = static_cast<uint8_t>((index + 1) % count);
}

void StepAwareMatrix::animateBatteryLow() {
    uint32_t now = millis();
    if (now - m_lastFrameTime < 400) {
        return;
    }
    m_lastFrameTime = now;
    static const uint8_t *const FRAMES[4] = {BATTERY_33, BATTERY_25, BATTERY_10, BATTERY_EMPTY};
    drawFrame(FRAMES[m_animFrame % 4]);
    m_animFrame++;
}

}  // namespace stepaware_matrix
}  // namespace esphome
