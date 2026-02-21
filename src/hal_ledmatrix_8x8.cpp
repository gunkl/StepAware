#include "hal_ledmatrix_8x8.h"
#include "logger.h"
#include "debug_logger.h"
#include <Wire.h>

#if !MOCK_HARDWARE
#include <LittleFS.h>
#endif

// LED state constants for Adafruit_GFX
#ifndef LED_ON
#define LED_ON 1
#endif
#ifndef LED_OFF
#define LED_OFF 0
#endif

// Animation frame data
static const uint8_t ARROW_DOWN[] = {
    0b00011000,  //   ##
    0b00011000,  //   ##
    0b00011000,  //   ##
    0b00011000,  //   ##
    0b11111111,  // ########
    0b01111110,  //  ######
    0b00111100,  //   ####
    0b00011000   //    ##
};

static const uint8_t ARROW_DOWN_INVERSE[] = {
    0b11100111,  // ###  ###
    0b11100111,  // ###  ###
    0b11100111,  // ###  ###
    0b11100111,  // ###  ###
    0b00000000,  //
    0b10000001,  // #      #
    0b11000011,  // ##    ##
    0b11100111   // ###  ###
};

static const uint8_t BOOT_OK[] = {
    0b00000000,  //
    0b00111101,  //   #### # (Circle with checkmark)
    0b01000010,  //  #    #
    0b10000101,  // #    #  #
    0b10101001,  // # # #  #
    0b01010010,  //  # #  #
    0b00111100,  //   ####
    0b00000000   //
};

static const uint8_t WIFI_CONNECTED[] = {
    0b00000000,  //
    0b00111100,  //   ####   (WiFi signal bars)
    0b01000000,  //  #
    0b10011000,  // #  ##
    0b00100000,  //   #
    0b00000100,  //      #
    0b00001000,  //     #
    0b00000000   //
};

static const uint8_t WIFI_DISCONNECTED[] = {
    0b10000001,  // #      # (Broken WiFi)
    0b01111110,  //  ######
    0b00100000,  //   #
    0b00011001,  //    ##  #
    0b01100110,  //  ##  ##
    0b10000100,  // #    #
    0b01111000,  //  ####
    0b10000010   // #     #
};

static const uint8_t ERROR_X[] = {
    0b00000000,
    0b01100110,  // ##   ##
    0b00111100,  //  ####
    0b00011000,  //   ##
    0b00011000,  //   ##
    0b00111100,  //  ####
    0b01100110,  // ##   ##
    0b00000000
};

// Battery animation frames - draining from ~1/3 full to empty
static const uint8_t BATTERY_33[] = {
    0b00011000,  // Terminal at top
    0b01111110,  // ┌──────┐
    0b01000010,  // │      │
    0b01000010,  // │      │ (1/2 full)
    0b01111110,  // │██████│
    0b01111110,  // │██████│
    0b01111110,  // │██████│
    0b01111110   // └──────┘
};

static const uint8_t BATTERY_25[] = {
    0b00011000,  // Terminal at top
    0b01111110,  // ┌──────┐
    0b01000010,  // │      │
    0b01000010,  // │      │
    0b01000010,  // │      │ (1/3 full)
    0b01111110,  // │██████│
    0b01111110,  // │██████│
    0b01111110   // └──────┘
};

static const uint8_t BATTERY_10[] = {
    0b00011000,  // Terminal at top
    0b01111110,  // ┌──────┐
    0b01000010,  // │      │
    0b01000010,  // │      │
    0b01000010,  // │      │
    0b01000010,  // │      │ (10% full)
    0b01111110,  // │██████│
    0b01111110   // └──────┘
};

static const uint8_t BATTERY_EMPTY[] = {
    0b00011000,  // Terminal at top
    0b01111110,  // ┌──────┐
    0b01000010,  // │      │
    0b01000010,  // │      │
    0b01000010,  // │      │
    0b01000010,  // │      │
    0b01000010,  // │      │
    0b01111110   // └──────┘
};

// Battery bitmaps for post-warning status display (healthy levels)
static const uint8_t BATTERY_FULL[] = {
    0b00011000,  // Terminal at top
    0b01111110,  // ┌──────┐
    0b01111110,  // │██████│ (full)
    0b01111110,  // │██████│
    0b01111110,  // │██████│
    0b01111110,  // │██████│
    0b01111110,  // │██████│
    0b01111110   // └──────┘
};

static const uint8_t BATTERY_75[] = {
    0b00011000,  // Terminal at top
    0b01111110,  // ┌──────┐
    0b01000010,  // │      │
    0b01111110,  // │██████│ (75%)
    0b01111110,  // │██████│
    0b01111110,  // │██████│
    0b01111110,  // │██████│
    0b01111110   // └──────┘
};

static const uint8_t BATTERY_50[] = {
    0b00011000,  // Terminal at top
    0b01111110,  // ┌──────┐
    0b01000010,  // │      │
    0b01000010,  // │      │
    0b01111110,  // │██████│ (50%)
    0b01111110,  // │██████│
    0b01111110,  // │██████│
    0b01111110   // └──────┘
};

// Snake traversal lookup: index 0-63 → (x, y) coordinates
// Row 0: L→R, Row 1: R→L, Row 2: L→R, etc. (boustrophedon pattern)
static const struct { uint8_t x; uint8_t y; } SNAKE_PIXELS[64] = {
    {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},{6,0},{7,0}, // Row 0 L→R
    {7,1},{6,1},{5,1},{4,1},{3,1},{2,1},{1,1},{0,1}, // Row 1 R→L
    {0,2},{1,2},{2,2},{3,2},{4,2},{5,2},{6,2},{7,2}, // Row 2 L→R
    {7,3},{6,3},{5,3},{4,3},{3,3},{2,3},{1,3},{0,3}, // Row 3 R→L
    {0,4},{1,4},{2,4},{3,4},{4,4},{5,4},{6,4},{7,4}, // Row 4 L→R
    {7,5},{6,5},{5,5},{4,5},{3,5},{2,5},{1,5},{0,5}, // Row 5 R→L
    {0,6},{1,6},{2,6},{3,6},{4,6},{5,6},{6,6},{7,6}, // Row 6 L→R
    {7,7},{6,7},{5,7},{4,7},{3,7},{2,7},{1,7},{0,7}, // Row 7 R→L
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
    , m_snakeProgress(0)
    , m_customAnimationCount(0)
    , m_activeCustomAnimation(nullptr)
    , m_i2cTransactionCount(0)
    , m_i2cFailureCount(0)
    , m_errorRate(-1.0f)
    , m_lastErrorRateUpdate(0)
#if !MOCK_HARDWARE
    , m_matrix(nullptr)
#endif
{
    memset(m_currentFrame, 0, sizeof(m_currentFrame));
    memset(m_mockFrame, 0, sizeof(m_mockFrame));
    memset(m_customAnimations, 0, sizeof(m_customAnimations));
}

HAL_LEDMatrix_8x8::~HAL_LEDMatrix_8x8() {
    // Free custom animations
    clearCustomAnimations();

#if !MOCK_HARDWARE
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
        DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Initializing in MOCK mode (I2C addr: 0x%02X)", m_i2cAddress);
        m_initialized = true;
        return true;
    }

#if !MOCK_HARDWARE
    // Initialize I2C
    Wire.begin(m_sdaPin, m_sclPin, I2C_FREQUENCY);

    // Create matrix object
    m_matrix = new Adafruit_8x8matrix();
    if (!m_matrix) {
        DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Failed to allocate matrix object");
        return false;
    }

    // Initialize matrix
    bool beginSuccess = m_matrix->begin(m_i2cAddress);

    // Track initial I2C transaction
    m_i2cTransactionCount++;
    if (!beginSuccess) {
        m_i2cFailureCount++;
        DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Failed to initialize HT16K33 at address 0x%02X (check wiring/address)",
                  m_i2cAddress);
        delete m_matrix;
        m_matrix = nullptr;
        return false;
    }

    m_matrix->setRotation(m_rotation);
    m_matrix->setBrightness(m_brightness);
    m_matrix->clear();
    m_matrix->writeDisplay();

    m_initialized = true;
    DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Initialized (I2C addr: 0x%02X, SDA: %d, SCL: %d)",
             m_i2cAddress, m_sdaPin, m_sclPin);

    // Perform initial error rate update
    updateErrorRate();

    return true;
#else
    DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Compiled with MOCK_HARDWARE=1 but mock_mode=false");
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
#if !MOCK_HARDWARE
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
#if !MOCK_HARDWARE
        if (m_matrix) {
            m_matrix->setBrightness(level);
            m_matrix->writeDisplay();
        }
#endif
    }

    DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Brightness set to %d", level);
}

void HAL_LEDMatrix_8x8::setRotation(uint8_t rotation) {
    if (rotation > 3) {
        rotation = 0;
    }

    m_rotation = rotation;

    if (!m_mockMode) {
#if !MOCK_HARDWARE
        if (m_matrix) {
            m_matrix->setRotation(rotation);
            m_matrix->writeDisplay();
        }
#endif
    }

    DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Rotation set to %d", rotation);
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
        case ANIM_BOOT_STATUS:       patternName = "BOOT_STATUS"; break;
        case ANIM_ERROR:             patternName = "ERROR"; break;
        case ANIM_WIFI_CONNECTED:    patternName = "WIFI_CONNECTED"; break;
        case ANIM_WIFI_DISCONNECTED: patternName = "WIFI_DISCONNECTED"; break;
        case ANIM_SNAKE_PROGRESS:    patternName = "SNAKE_PROGRESS"; break;
        case ANIM_CUSTOM:            patternName = "CUSTOM"; break;
    }

    DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Animation started: %s, duration: %u ms", patternName, duration_ms);
}

void HAL_LEDMatrix_8x8::stopAnimation() {
    if (m_currentPattern != ANIM_NONE) {
        DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Animation stopped");
        m_currentPattern = ANIM_NONE;
        clear();
    }
}

void HAL_LEDMatrix_8x8::setSnakeProgress(uint8_t pixelCount) {
    if (!m_initialized) return;
    if (pixelCount > 64) pixelCount = 64;

    m_snakeProgress = pixelCount;
    m_currentPattern = ANIM_SNAKE_PROGRESS;

    // Build frame buffer from snake lookup
    uint8_t frame[8] = {0};
    for (uint8_t i = 0; i < pixelCount; i++) {
        uint8_t x = SNAKE_PIXELS[i].x;
        uint8_t y = SNAKE_PIXELS[i].y;
        frame[y] |= (1 << (7 - x));  // MSB-first format
    }

    drawFrame(frame);
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
#if !MOCK_HARDWARE
        if (m_matrix) {
            m_matrix->clear();
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    // Frame data is MSB-first: bit 7 = leftmost pixel (x=0)
                    if (frame[y] & (1 << (7 - x))) {
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

    // Frame buffer uses MSB-first format: bit 7 = leftmost pixel (x=0)
    uint8_t bitMask = (1 << (7 - x));

    if (on) {
        m_currentFrame[y] |= bitMask;
    } else {
        m_currentFrame[y] &= ~bitMask;
    }

    if (m_mockMode) {
        if (on) {
            m_mockFrame[y] |= bitMask;
        } else {
            m_mockFrame[y] &= ~bitMask;
        }
    } else {
#if !MOCK_HARDWARE
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
    DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Scrolling text: %s (speed: %u ms)", text, speed_ms);

    if (m_mockMode) {
        // Mock mode: just log
        return;
    }

#if !MOCK_HARDWARE
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

void HAL_LEDMatrix_8x8::showBatteryBitmap(uint8_t percentage) {
    if (!m_initialized) return;

    if (percentage >= 75) {
        drawBitmap(BATTERY_FULL);
    } else if (percentage >= 50) {
        drawBitmap(BATTERY_75);
    } else if (percentage >= 25) {
        drawBitmap(BATTERY_50);
    } else if (percentage >= 10) {
        drawBitmap(BATTERY_33);
    } else {
        drawBitmap(BATTERY_10);
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
        case ANIM_ERROR:
            drawBitmap(ERROR_X);
            break;
        case ANIM_WIFI_CONNECTED:
            drawBitmap(WIFI_CONNECTED);
            break;
        case ANIM_WIFI_DISCONNECTED:
            drawBitmap(WIFI_DISCONNECTED);
            break;
        case ANIM_SNAKE_PROGRESS:
            // Static display - no animation update needed
            // Progress is set explicitly via setSnakeProgress()
            break;
        case ANIM_CUSTOM:
            animateCustom();
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

            // Flash between arrow and inverse arrow
            if (m_animationFrame % 2 == 0) {
                drawBitmap(ARROW_DOWN);
            } else {
                drawBitmap(ARROW_DOWN_INVERSE);
            }
        }
    }
    // Phase 2: Scroll arrow down (800ms - 1600ms)
    else if (now - m_animationStartTime < 1600) {
        if (elapsed >= 100) { // 100ms per frame
            m_lastFrameTime = now;
            uint8_t scrollFrame = (now - m_animationStartTime - 800) / 100;

            // Shift arrow down based on scroll frame
            uint8_t shiftedArrow[8];
            for (int i = 0; i < 8; i++) {
                int sourceIdx = i - scrollFrame;
                if (sourceIdx >= 0 && sourceIdx < 8) {
                    shiftedArrow[i] = ARROW_DOWN[sourceIdx];
                } else {
                    shiftedArrow[i] = 0;
                }
            }
            drawFrame(shiftedArrow);
        }
    }
    // Phase 3: flash arrow/clear (1600–2400 ms), then loop back to Phase 1 when duration == 0
    else {
        uint32_t phaseElapsed = (now - m_animationStartTime) - 1600;

        // After 800 ms in Phase 3 (total cycle = 2400 ms), loop if running indefinitely
        if (m_animationDuration == 0 && phaseElapsed >= 800) {
            m_animationStartTime = now;
            m_animationFrame    = 0;
            m_lastFrameTime     = now;
            drawBitmap(ARROW_DOWN);   // first visible frame of Phase 1
            return;
        }

        if (elapsed >= MATRIX_FLASH_DURATION_MS) {
            m_lastFrameTime = now;
            if ((m_animationFrame % 2) == 0) {
                clear();
            } else {
                drawBitmap(ARROW_DOWN);
            }
            m_animationFrame++;
        }
    }
}

void HAL_LEDMatrix_8x8::animateBatteryLow(uint8_t percentage) {
    uint32_t now = millis();
    uint32_t elapsed = now - m_lastFrameTime;

    // Animate battery draining from 33% to empty
    // Frame duration: 400ms per frame = ~1.6s total animation
    if (elapsed >= 400) {
        m_lastFrameTime = now;

        // Cycle through 4 frames: 33%, 25%, 10%, empty
        uint8_t frameIndex = (m_animationFrame % 4);
        m_animationFrame++;

        switch (frameIndex) {
            case 0:
                drawBitmap(BATTERY_33);
                break;
            case 1:
                drawBitmap(BATTERY_25);
                break;
            case 2:
                drawBitmap(BATTERY_10);
                break;
            case 3:
                drawBitmap(BATTERY_EMPTY);
                break;
        }
    }
}

void HAL_LEDMatrix_8x8::animateBootStatus(const char* status) {
    // Show checkmark for success, X for error
    // Status strings like "ERROR", "FAIL", "FAILED" trigger error icon
    if (status && (strstr(status, "ERROR") || strstr(status, "FAIL"))) {
        drawBitmap(ERROR_X);
    } else {
        // Default: show OK icon for successful boot
        drawBitmap(BOOT_OK);
    }
}

void HAL_LEDMatrix_8x8::drawArrow() {
    drawBitmap(ARROW_DOWN);
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
#if !MOCK_HARDWARE
        if (m_matrix) {
            // Track I2C transaction
            m_i2cTransactionCount++;

            DEBUG_LOG_LED("LED Matrix: I2C transaction #%u (failures: %u, current error rate: %.1f%%)",
                      m_i2cTransactionCount, m_i2cFailureCount, m_errorRate);

            // Write to display (writeDisplay returns void, not bool)
            m_matrix->writeDisplay();

            // Track errors using Wire I2C status check
            // Note: Adafruit_LEDBackpack::writeDisplay() doesn't return a status,
            // so we verify I2C communication with a test transmission
            Wire.beginTransmission(m_i2cAddress);
            uint8_t error = Wire.endTransmission();
            if (error != 0) {
                m_i2cFailureCount++;
                DEBUG_LOG_LED("LED Matrix: I2C error detected (code: %u), failure count: %u",
                         error, m_i2cFailureCount);
            }

            // Update error rate periodically (every 10 transactions)
            if (m_i2cTransactionCount % 10 == 0) {
                DEBUG_LOG_LED("LED Matrix: Reached 10 transaction milestone, updating error rate...");
                updateErrorRate();
            }
        }
#endif
    }
}

// ============================================================================
// Phase 2: Custom Animation Support
// ============================================================================

bool HAL_LEDMatrix_8x8::loadCustomAnimation(const char* filepath) {
    // Check if we have room for more animations
    if (m_customAnimationCount >= MAX_CUSTOM_ANIMATIONS) {
        DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Cannot load animation, max limit reached (%d)", MAX_CUSTOM_ANIMATIONS);
        return false;
    }

#if !MOCK_HARDWARE
    // Open file from LittleFS
    File file = LittleFS.open(filepath, "r");
    if (!file) {
        DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Failed to open animation file: %s", filepath);
        return false;
    }

    // Allocate new custom animation
    HAL_LEDMatrix_8x8::CustomAnimation* anim = new HAL_LEDMatrix_8x8::CustomAnimation();
    if (!anim) {
        DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Failed to allocate memory for animation");
        file.close();
        return false;
    }

    // Initialize animation
    memset(anim, 0, sizeof(HAL_LEDMatrix_8x8::CustomAnimation));
    anim->loop = false;
    anim->frameCount = 0;

    // Parse file line by line
    String line;
    while (file.available()) {
        line = file.readStringUntil('\n');
        line.trim();

        // Skip empty lines and comments
        if (line.length() == 0 || line.startsWith("#")) {
            continue;
        }

        // Parse name
        if (line.startsWith("name=")) {
            String name = line.substring(5);
            name.trim();
            strncpy(anim->name, name.c_str(), sizeof(anim->name) - 1);
        }
        // Parse loop
        else if (line.startsWith("loop=")) {
            String value = line.substring(5);
            value.trim();
            anim->loop = (value == "true" || value == "1");
        }
        // Parse frame
        else if (line.startsWith("frame=")) {
            if (anim->frameCount >= 16) {
                DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Too many frames, max 16");
                continue;
            }

            String frameData = line.substring(6);
            frameData.trim();

            // Parse frame: 8 bytes + delay
            // Format: 11111111,10000001,10000001,10000001,10000001,10000001,10000001,11111111,100
            int commaCount = 0;
            int startIdx = 0;
            uint8_t byteIdx = 0;

            for (unsigned int i = 0; i <= frameData.length(); i++) {
                if (i == frameData.length() || frameData.charAt(i) == ',') {
                    String token = frameData.substring(startIdx, i);
                    token.trim();

                    if (byteIdx < 8) {
                        // Parse frame byte (binary string)
                        anim->frames[anim->frameCount][byteIdx] = (uint8_t)strtol(token.c_str(), NULL, 2);
                        byteIdx++;
                    } else {
                        // Parse delay (last value)
                        anim->frameDelays[anim->frameCount] = (uint16_t)token.toInt();
                    }

                    startIdx = i + 1;
                }
            }

            anim->frameCount++;
        }
    }

    file.close();

    // Validate animation
    if (anim->frameCount == 0 || strlen(anim->name) == 0) {
        DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Invalid animation file (no frames or name)");
        delete anim;
        return false;
    }

    // Store animation
    m_customAnimations[m_customAnimationCount++] = anim;

    DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Loaded custom animation '%s' (%d frames)",
             anim->name, anim->frameCount);

    return true;
#else
    // Mock mode: simulate successful load
    DEBUG_LOG_LED("HAL_LEDMatrix_8x8: MOCK - Would load animation from: %s", filepath);

    // Create mock animation
    HAL_LEDMatrix_8x8::CustomAnimation* anim = new HAL_LEDMatrix_8x8::CustomAnimation();
    if (!anim) {
        return false;
    }

    memset(anim, 0, sizeof(HAL_LEDMatrix_8x8::CustomAnimation));
    strncpy(anim->name, "MockAnimation", sizeof(anim->name) - 1);
    anim->loop = true;
    anim->frameCount = 2;

    // Simple test pattern
    anim->frames[0][0] = 0b11111111;
    anim->frameDelays[0] = 100;
    anim->frames[1][0] = 0b00000000;
    anim->frameDelays[1] = 100;

    m_customAnimations[m_customAnimationCount++] = anim;

    return true;
#endif
}

bool HAL_LEDMatrix_8x8::playCustomAnimation(const char* name, uint32_t duration_ms) {
    // Find animation by name
    HAL_LEDMatrix_8x8::CustomAnimation* anim = findCustomAnimation(name);
    if (!anim) {
        DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Custom animation not found: %s", name);
        return false;
    }

    // Set as active custom animation
    m_activeCustomAnimation = anim;

    // Start animation
    m_currentPattern = ANIM_CUSTOM;
    m_animationStartTime = millis();
    m_animationDuration = duration_ms;
    m_animationFrame = 0;
    m_lastFrameTime = millis();

    DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Playing custom animation '%s' (duration: %u ms)", name, duration_ms);

    return true;
}

uint8_t HAL_LEDMatrix_8x8::getCustomAnimationCount() const {
    return m_customAnimationCount;
}

void HAL_LEDMatrix_8x8::clearCustomAnimations() {
    // Free all custom animations
    for (uint8_t i = 0; i < m_customAnimationCount; i++) {
        if (m_customAnimations[i]) {
            delete m_customAnimations[i];
            m_customAnimations[i] = nullptr;
        }
    }

    m_customAnimationCount = 0;
    m_activeCustomAnimation = nullptr;

    DEBUG_LOG_LED("HAL_LEDMatrix_8x8: Cleared all custom animations");
}

HAL_LEDMatrix_8x8::CustomAnimation* HAL_LEDMatrix_8x8::findCustomAnimation(const char* name) {
    for (uint8_t i = 0; i < m_customAnimationCount; i++) {
        if (m_customAnimations[i] && strcmp(m_customAnimations[i]->name, name) == 0) {
            return m_customAnimations[i];
        }
    }
    return nullptr;
}

void HAL_LEDMatrix_8x8::animateCustom() {
    if (!m_activeCustomAnimation || m_activeCustomAnimation->frameCount == 0) {
        return;
    }

    uint32_t now = millis();
    uint8_t currentFrame = m_animationFrame % m_activeCustomAnimation->frameCount;
    uint16_t frameDelay = m_activeCustomAnimation->frameDelays[currentFrame];

    // Check if it's time to advance to next frame
    if (now - m_lastFrameTime >= frameDelay) {
        m_lastFrameTime = now;

        // Draw current frame
        drawFrame(m_activeCustomAnimation->frames[currentFrame]);

        // Advance to next frame
        m_animationFrame++;

        // Handle looping
        if (m_animationFrame >= m_activeCustomAnimation->frameCount) {
            if (m_activeCustomAnimation->loop && m_animationDuration == 0) {
                // Loop indefinitely
                m_animationFrame = 0;
            } else {
                // Animation complete (will be stopped by duration check in update())
            }
        }
    }
}

// ============================================================================
// Error Rate Monitoring
// ============================================================================

float HAL_LEDMatrix_8x8::getErrorRate() const {
    DEBUG_LOG_LED("LED Matrix: getErrorRate() called, returning %.1f%% "
              "(%u failures / %u transactions, last update: %u ms ago)",
              m_errorRate, m_i2cFailureCount, m_i2cTransactionCount,
              millis() - m_lastErrorRateUpdate);
    return m_errorRate;
}

void HAL_LEDMatrix_8x8::updateErrorRate() {
    if (m_mockMode) {
        // Mock mode - simulate perfect I2C communication
        m_errorRate = 0.0f;
        return;
    }

#if !MOCK_HARDWARE
    // Calculate error rate from tracked transactions
    if (m_i2cTransactionCount > 0) {
        m_errorRate = ((float)m_i2cFailureCount / (float)m_i2cTransactionCount) * 100.0f;

        uint32_t now = millis();
        // Log error rate once per minute
        if (now - m_lastErrorRateUpdate >= 60000) {
            DEBUG_LOG_LED("LED Matrix I2C: Error rate = %.1f%% (%u failures / %u transactions)",
                     m_errorRate, m_i2cFailureCount, m_i2cTransactionCount);
            m_lastErrorRateUpdate = now;

            // Reset counters periodically to prevent overflow and give recent error rate
            // Keep last 1000 transactions worth of data
            if (m_i2cTransactionCount > 1000) {
                // Scale down by factor of 10
                m_i2cTransactionCount /= 10;
                m_i2cFailureCount /= 10;
            }
        }
    } else {
        m_errorRate = -1.0f;  // No data yet
    }
#else
    m_errorRate = 0.0f;
#endif
}
