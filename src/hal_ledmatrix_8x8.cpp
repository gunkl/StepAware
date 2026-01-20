#include "hal_ledmatrix_8x8.h"
#include "logger.h"
#include <Wire.h>

// LED state constants for Adafruit_GFX
#ifndef LED_ON
#define LED_ON 1
#endif
#ifndef LED_OFF
#define LED_OFF 0
#endif

// Animation frame data
static const uint8_t ARROW_UP[] = {
    0b00011000,
    0b00111100,
    0b01111110,
    0b11111111,
    0b00011000,
    0b00011000,
    0b00011000,
    0b00011000
};

static const uint8_t CHECKMARK[] = {
    0b00000000,
    0b00000001,
    0b00000011,
    0b10000110,
    0b11001100,
    0b01111000,
    0b00110000,
    0b00000000
};

static const uint8_t BATTERY_EMPTY[] = {
    0b00111100,
    0b01000010,
    0b01000010,
    0b01000010,
    0b01000010,
    0b01000010,
    0b01000010,
    0b00111100
};

HAL_LEDMatrix_8x8::HAL_LEDMatrix_8x8(uint8_t i2c_address, uint8_t sda_pin,
                                     uint8_t scl_pin, bool mock_mode)
    : m_i2cAddress(i2c_address)
    , m_sdaPin(sda_pin)
    , m_sclPin(scl_pin)
    , m_mockMode(mock_mode)
    , m_initialized(false)
    , m_brightness(MATRIX_BRIGHTNESS_DEFAULT)
    , m_rotation(MATRIX_ROTATION)
    , m_currentPattern(ANIM_NONE)
    , m_animationStartTime(0)
    , m_animationDuration(0)
    , m_lastFrameTime(0)
    , m_animationFrame(0)
#ifndef MOCK_HARDWARE
    , m_matrix(nullptr)
#endif
{
    memset(m_currentFrame, 0, sizeof(m_currentFrame));
    memset(m_mockFrame, 0, sizeof(m_mockFrame));
}

HAL_LEDMatrix_8x8::~HAL_LEDMatrix_8x8() {
#ifndef MOCK_HARDWARE
    if (m_matrix) {
        delete m_matrix;
        m_matrix = nullptr;
    }
#endif
}

bool HAL_LEDMatrix_8x8::begin() {
    if (m_initialized) {
        return true;
    }

    if (m_mockMode) {
        LOG_INFO("HAL_LEDMatrix_8x8: Initializing in MOCK mode (I2C addr: 0x%02X)", m_i2cAddress);
        m_initialized = true;
        return true;
    }

#ifndef MOCK_HARDWARE
    // Initialize I2C
    Wire.begin(m_sdaPin, m_sclPin, I2C_FREQUENCY);

    // Create matrix object
    m_matrix = new Adafruit_8x8matrix();
    if (!m_matrix) {
        LOG_ERROR("HAL_LEDMatrix_8x8: Failed to allocate matrix object");
        return false;
    }

    // Initialize matrix
    m_matrix->begin(m_i2cAddress);
    m_matrix->setRotation(m_rotation);
    m_matrix->setBrightness(m_brightness);
    m_matrix->clear();
    m_matrix->writeDisplay();

    m_initialized = true;
    LOG_INFO("HAL_LEDMatrix_8x8: Initialized (I2C addr: 0x%02X, SDA: %d, SCL: %d)",
             m_i2cAddress, m_sdaPin, m_sclPin);
    return true;
#else
    LOG_WARN("HAL_LEDMatrix_8x8: Compiled with MOCK_HARDWARE but mock_mode=false");
    return false;
#endif
}

void HAL_LEDMatrix_8x8::update() {
    if (!m_initialized) {
        return;
    }

    if (m_currentPattern != ANIM_NONE) {
        updateAnimation();
    }
}

void HAL_LEDMatrix_8x8::clear() {
    if (!m_initialized) {
        return;
    }

    memset(m_currentFrame, 0, sizeof(m_currentFrame));

    if (m_mockMode) {
        memset(m_mockFrame, 0, sizeof(m_mockFrame));
    } else {
#ifndef MOCK_HARDWARE
        if (m_matrix) {
            m_matrix->clear();
            m_matrix->writeDisplay();
        }
#endif
    }
}

void HAL_LEDMatrix_8x8::setBrightness(uint8_t level) {
    if (level > 15) {
        level = 15;
    }

    m_brightness = level;

    if (!m_mockMode) {
#ifndef MOCK_HARDWARE
        if (m_matrix) {
            m_matrix->setBrightness(level);
            m_matrix->writeDisplay();
        }
#endif
    }

    LOG_DEBUG("HAL_LEDMatrix_8x8: Brightness set to %d", level);
}

void HAL_LEDMatrix_8x8::setRotation(uint8_t rotation) {
    if (rotation > 3) {
        rotation = 0;
    }

    m_rotation = rotation;

    if (!m_mockMode) {
#ifndef MOCK_HARDWARE
        if (m_matrix) {
            m_matrix->setRotation(rotation);
            m_matrix->writeDisplay();
        }
#endif
    }

    LOG_DEBUG("HAL_LEDMatrix_8x8: Rotation set to %d", rotation);
}

void HAL_LEDMatrix_8x8::startAnimation(AnimationPattern pattern, uint32_t duration_ms) {
    if (!m_initialized) {
        return;
    }

    m_currentPattern = pattern;
    m_animationStartTime = millis();
    m_animationDuration = duration_ms;
    m_animationFrame = 0;
    m_lastFrameTime = millis();

    const char* patternName = "UNKNOWN";
    switch (pattern) {
        case ANIM_NONE:           patternName = "NONE"; break;
        case ANIM_MOTION_ALERT:   patternName = "MOTION_ALERT"; break;
        case ANIM_BATTERY_LOW:    patternName = "BATTERY_LOW"; break;
        case ANIM_BOOT_STATUS:    patternName = "BOOT_STATUS"; break;
        case ANIM_WIFI_CONNECTED: patternName = "WIFI_CONNECTED"; break;
        case ANIM_CUSTOM:         patternName = "CUSTOM"; break;
    }

    LOG_INFO("HAL_LEDMatrix_8x8: Animation started: %s, duration: %u ms", patternName, duration_ms);
}

void HAL_LEDMatrix_8x8::stopAnimation() {
    if (m_currentPattern != ANIM_NONE) {
        LOG_DEBUG("HAL_LEDMatrix_8x8: Animation stopped");
        m_currentPattern = ANIM_NONE;
        clear();
    }
}

bool HAL_LEDMatrix_8x8::isAnimating() const {
    return m_currentPattern != ANIM_NONE;
}

HAL_LEDMatrix_8x8::AnimationPattern HAL_LEDMatrix_8x8::getPattern() const {
    return m_currentPattern;
}

void HAL_LEDMatrix_8x8::drawFrame(const uint8_t frame[8]) {
    if (!m_initialized) {
        return;
    }

    memcpy(m_currentFrame, frame, 8);

    if (m_mockMode) {
        memcpy(m_mockFrame, frame, 8);
    } else {
#ifndef MOCK_HARDWARE
        if (m_matrix) {
            m_matrix->clear();
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    if (frame[y] & (1 << x)) {
                        m_matrix->drawPixel(x, y, LED_ON);
                    }
                }
            }
            m_matrix->writeDisplay();
        }
#endif
    }
}

void HAL_LEDMatrix_8x8::setPixel(uint8_t x, uint8_t y, bool on) {
    if (!m_initialized || x >= 8 || y >= 8) {
        return;
    }

    if (on) {
        m_currentFrame[y] |= (1 << x);
    } else {
        m_currentFrame[y] &= ~(1 << x);
    }

    if (m_mockMode) {
        if (on) {
            m_mockFrame[y] |= (1 << x);
        } else {
            m_mockFrame[y] &= ~(1 << x);
        }
    } else {
#ifndef MOCK_HARDWARE
        if (m_matrix) {
            m_matrix->drawPixel(x, y, on ? LED_ON : LED_OFF);
            m_matrix->writeDisplay();
        }
#endif
    }
}

void HAL_LEDMatrix_8x8::drawBitmap(const uint8_t* bitmap) {
    drawFrame(bitmap);
}

void HAL_LEDMatrix_8x8::scrollText(const char* text, uint32_t speed_ms) {
    if (!m_initialized || !text) {
        return;
    }

    // This is a simplified implementation
    // For production, would need to use Adafruit_GFX text rendering
    LOG_DEBUG("HAL_LEDMatrix_8x8: Scrolling text: %s (speed: %u ms)", text, speed_ms);

    if (m_mockMode) {
        // Mock mode: just log
        return;
    }

#ifndef MOCK_HARDWARE
    if (m_matrix) {
        // Text scrolling would be implemented here using Adafruit_GFX
        // For Phase 1, we'll keep this simple
        m_matrix->clear();
        m_matrix->setTextSize(1);
        m_matrix->setTextWrap(false);
        m_matrix->setTextColor(LED_ON);
        m_matrix->setCursor(0, 0);
        m_matrix->print(text[0]); // Show first character for now
        m_matrix->writeDisplay();
    }
#endif
}

void HAL_LEDMatrix_8x8::mockSetFrame(const uint8_t frame[8]) {
    if (m_mockMode) {
        memcpy(m_mockFrame, frame, 8);
    }
}

void HAL_LEDMatrix_8x8::updateAnimation() {
    if (!m_initialized || m_currentPattern == ANIM_NONE) {
        return;
    }

    // Check if animation duration expired
    if (m_animationDuration > 0) {
        uint32_t elapsed = millis() - m_animationStartTime;
        if (elapsed >= m_animationDuration) {
            stopAnimation();
            return;
        }
    }

    // Update animation based on pattern
    switch (m_currentPattern) {
        case ANIM_MOTION_ALERT:
            animateMotionAlert();
            break;
        case ANIM_BATTERY_LOW:
            animateBatteryLow(20); // Default 20% for now
            break;
        case ANIM_BOOT_STATUS:
            animateBootStatus("BOOT");
            break;
        case ANIM_WIFI_CONNECTED:
            drawBitmap(CHECKMARK);
            break;
        default:
            break;
    }
}

void HAL_LEDMatrix_8x8::animateMotionAlert() {
    uint32_t now = millis();
    uint32_t elapsed = now - m_lastFrameTime;

    // Phase 1: Flash twice (first 800ms)
    if (now - m_animationStartTime < 800) {
        if (elapsed >= MATRIX_FLASH_DURATION_MS) {
            m_lastFrameTime = now;
            m_animationFrame++;

            if (m_animationFrame % 2 == 0) {
                clear();
            } else {
                // Fill display
                uint8_t fullFrame[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                drawFrame(fullFrame);
            }
        }
    }
    // Phase 2: Scroll arrow up (800ms - 1600ms)
    else if (now - m_animationStartTime < 1600) {
        if (elapsed >= 100) { // 100ms per frame
            m_lastFrameTime = now;
            uint8_t scrollFrame = (now - m_animationStartTime - 800) / 100;

            // Shift arrow up based on scroll frame
            uint8_t shiftedArrow[8];
            for (int i = 0; i < 8; i++) {
                if (i + scrollFrame < 8) {
                    shiftedArrow[i] = ARROW_UP[i + scrollFrame];
                } else {
                    shiftedArrow[i] = 0;
                }
            }
            drawFrame(shiftedArrow);
        }
    }
    // Phase 3: Flash arrow twice (1600ms - 2400ms)
    else {
        if (elapsed >= MATRIX_FLASH_DURATION_MS) {
            m_lastFrameTime = now;

            if ((m_animationFrame % 2) == 0) {
                clear();
            } else {
                drawBitmap(ARROW_UP);
            }
            m_animationFrame++;
        }
    }
}

void HAL_LEDMatrix_8x8::animateBatteryLow(uint8_t percentage) {
    uint32_t now = millis();
    uint32_t elapsed = now - m_lastFrameTime;

    // Blink battery icon
    if (elapsed >= 500) {
        m_lastFrameTime = now;
        m_animationFrame++;

        if (m_animationFrame % 2 == 0) {
            clear();
        } else {
            drawBitmap(BATTERY_EMPTY);
        }
    }
}

void HAL_LEDMatrix_8x8::animateBootStatus(const char* status) {
    // Simple boot animation: show checkmark
    drawBitmap(CHECKMARK);
}

void HAL_LEDMatrix_8x8::drawArrow() {
    drawBitmap(ARROW_UP);
}

void HAL_LEDMatrix_8x8::flashDisplay(uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        uint8_t fullFrame[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        drawFrame(fullFrame);
        delay(MATRIX_FLASH_DURATION_MS);
        clear();
        delay(MATRIX_FLASH_DURATION_MS);
    }
}

void HAL_LEDMatrix_8x8::writeDisplay() {
    if (!m_mockMode) {
#ifndef MOCK_HARDWARE
        if (m_matrix) {
            m_matrix->writeDisplay();
        }
#endif
    }
}
