#ifndef STEPAWARE_HAL_LEDMATRIX_8X8_H
#define STEPAWARE_HAL_LEDMATRIX_8X8_H

#include <Arduino.h>
#if !MOCK_HARDWARE
    #include <Adafruit_LEDBackpack.h>
    #include <Adafruit_GFX.h>
#endif
#include "config.h"
#include "display_types.h"

/**
 * @brief Hardware Abstraction Layer for 8x8 LED Matrix
 *
 * Provides high-level control of Adafruit Mini 8x8 LED Matrix w/I2C Backpack (HT16K33).
 * Supports animations, scrolling text, and pixel-level control with mock mode for testing.
 *
 * Features:
 * - Pre-defined animations (motion alert, battery status, boot)
 * - Scrolling text display
 * - Direct pixel/frame buffer control
 * - Brightness control (0-15)
 * - Rotation support (0°, 90°, 180°, 270°)
 * - Mock mode for testing without hardware
 */
class HAL_LEDMatrix_8x8 {
public:
    /**
     * @brief Animation pattern enumeration
     */
    enum AnimationPattern {
        ANIM_NONE,              // No animation
        ANIM_MOTION_ALERT,      // Flash arrow + scroll down
        ANIM_BATTERY_LOW,       // Show battery draining animation
        ANIM_BOOT_STATUS,       // Boot-time status (circle-check for success)
        ANIM_ERROR,             // Error indicator (X icon)
        ANIM_WIFI_CONNECTED,    // WiFi signal bars
        ANIM_WIFI_DISCONNECTED, // Broken WiFi icon
        ANIM_CUSTOM             // For Phase 2
    };

    /**
     * @brief Constructor
     *
     * @param i2c_address I2C address (0x70-0x77)
     * @param sda_pin SDA GPIO pin
     * @param scl_pin SCL GPIO pin
     * @param mock_mode Enable mock mode for testing
     */
    HAL_LEDMatrix_8x8(uint8_t i2c_address = MATRIX_I2C_ADDRESS,
                      uint8_t sda_pin = I2C_SDA_PIN,
                      uint8_t scl_pin = I2C_SCL_PIN,
                      bool mock_mode = false);

    /**
     * @brief Destructor
     */
    ~HAL_LEDMatrix_8x8();

    /**
     * @brief Initialize the LED matrix
     *
     * Sets up I2C communication and configures the HT16K33 driver.
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Update animation state (call every loop)
     *
     * Updates animation frames and handles timing.
     */
    void update();

    /**
     * @brief Clear all pixels
     */
    void clear();

    /**
     * @brief Set brightness level
     *
     * @param level Brightness (0-15, where 15 is brightest)
     */
    void setBrightness(uint8_t level);

    /**
     * @brief Set display rotation
     *
     * @param rotation Rotation (0=0°, 1=90°, 2=180°, 3=270°)
     */
    void setRotation(uint8_t rotation);

    /**
     * @brief Start an animation
     *
     * @param pattern Animation pattern to play
     * @param duration_ms Animation duration (0 = loop indefinitely)
     */
    void startAnimation(AnimationPattern pattern, uint32_t duration_ms = 0);

    /**
     * @brief Stop current animation
     */
    void stopAnimation();

    /**
     * @brief Check if animation is running
     *
     * @return true if animating
     */
    bool isAnimating() const;

    /**
     * @brief Get current animation pattern
     *
     * @return AnimationPattern Current pattern
     */
    AnimationPattern getPattern() const;

    /**
     * @brief Draw a frame buffer to display
     *
     * @param frame 8-byte frame buffer (each byte = one row)
     */
    void drawFrame(const uint8_t frame[8]);

    /**
     * @brief Set individual pixel
     *
     * @param x X coordinate (0-7)
     * @param y Y coordinate (0-7)
     * @param on true = LED on, false = LED off
     */
    void setPixel(uint8_t x, uint8_t y, bool on);

    /**
     * @brief Draw bitmap (8x8)
     *
     * @param bitmap 8-byte bitmap
     */
    void drawBitmap(const uint8_t* bitmap);

    /**
     * @brief Scroll text across display
     *
     * @param text Text to scroll
     * @param speed_ms Delay between frames (ms)
     */
    void scrollText(const char* text, uint32_t speed_ms = MATRIX_SCROLL_SPEED_MS);

    /**
     * @brief Get brightness level
     *
     * @return uint8_t Current brightness (0-15)
     */
    uint8_t getBrightness() const { return m_brightness; }

    /**
     * @brief Check if matrix is initialized
     *
     * @return true if ready
     */
    bool isReady() const { return m_initialized; }

    /**
     * @brief Mock mode: Set frame buffer
     *
     * @param frame 8-byte frame buffer
     */
    void mockSetFrame(const uint8_t frame[8]);

    /**
     * @brief Mock mode: Get frame buffer
     *
     * @return uint8_t* Pointer to 8-byte frame buffer
     */
    uint8_t* mockGetFrame() { return m_mockFrame; }

    /**
     * @brief Get the raw current frame buffer (8 bytes, one per row, bit 7 = leftmost pixel)
     *
     * Useful for diagnostic logging before entering sleep — lets callers inspect exactly
     * which pixels are lit without modifying any display state.
     *
     * @return Pointer to internal 8-byte frame buffer (read-only)
     */
    const uint8_t* getCurrentFrame() const { return m_currentFrame; }

    // ========================================================================
    // Phase 2: Custom Animation Support (Stubs)
    // ========================================================================

    /**
     * @brief Custom animation definition (Phase 2)
     *
     * Allows loading user-defined animations from configuration files.
     * Each animation consists of multiple frames with individual timing.
     */
    struct CustomAnimation {
        char name[32];              ///< Animation name
        uint8_t frames[16][8];      ///< Up to 16 frames (8 bytes each)
        uint16_t frameDelays[16];   ///< Delay after each frame (ms)
        uint8_t frameCount;         ///< Number of frames (1-16)
        bool loop;                  ///< Loop animation when complete
    };

    /**
     * @brief Load custom animation from file
     *
     * Loads a custom animation definition from a text file stored on LittleFS.
     * File format:
     *   name=MyAnimation
     *   loop=true
     *   frame=11111111,10000001,10000001,10000001,10000001,10000001,10000001,11111111,100
     *   frame=...
     *
     * Each frame consists of 8 binary bytes (one per row) plus a delay in milliseconds.
     * Maximum 16 frames per animation. Maximum 8 custom animations can be loaded.
     *
     * @param filepath Path to animation definition file (e.g., "/animations/heart.txt")
     * @return true if animation loaded successfully, false on error
     */
    bool loadCustomAnimation(const char* filepath);

    /**
     * @brief Play custom animation by name
     *
     * Plays a previously loaded custom animation. The animation must have been
     * loaded using loadCustomAnimation() first.
     *
     * @param name Animation name (matches name= field in animation file)
     * @param duration_ms Animation duration (0 = loop indefinitely if loop=true)
     * @return true if animation started successfully, false if animation not found
     */
    bool playCustomAnimation(const char* name, uint32_t duration_ms = 0);

    /**
     * @brief Get number of loaded custom animations
     *
     * Returns the count of currently loaded custom animations.
     *
     * @return uint8_t Number of custom animations loaded (0-8)
     */
    uint8_t getCustomAnimationCount() const;

    /**
     * @brief Clear all custom animations
     *
     * Frees memory used by all loaded custom animations. Use this to
     * clear animations before loading a different set.
     */
    void clearCustomAnimations();

    // ========================================================================
    // Error Rate Monitoring
    // ========================================================================

    /**
     * @brief Get I2C communication error rate as a percentage
     *
     * Returns the percentage of failed I2C transactions from the last check.
     * Error rate is calculated by tracking I2C write failures during normal operation.
     *
     * @return Error rate percentage (0.0 - 100.0), or -1.0 if no data available yet
     */
    float getErrorRate() const;

    /**
     * @brief Update error rate statistics
     *
     * Called internally during I2C operations to track failures.
     * Applications can read the error rate via getErrorRate().
     */
    void updateErrorRate();

    /**
     * @brief Get I2C transaction count
     *
     * Returns the number of I2C transactions performed since initialization.
     *
     * @return Transaction count
     */
    uint32_t getTransactionCount() const { return m_i2cTransactionCount; }

    /**
     * @brief Check if error rate data is available
     *
     * Error rate becomes available after first updateErrorRate() call,
     * which happens automatically after 10 transactions.
     *
     * @return true if error rate is valid, false if still showing -1.0
     */
    bool isErrorRateAvailable() const { return m_errorRate >= 0.0f; }

private:
    // Hardware
#if !MOCK_HARDWARE
    Adafruit_8x8matrix* m_matrix;
#endif
    uint8_t m_i2cAddress;
    uint8_t m_sdaPin;
    uint8_t m_sclPin;
    bool m_mockMode;
    bool m_initialized;

    // Display state
    uint8_t m_brightness;
    uint8_t m_rotation;
    uint8_t m_currentFrame[8];

    // Animation state
    AnimationPattern m_currentPattern;
    uint32_t m_animationStartTime;
    uint32_t m_animationDuration;
    uint32_t m_lastFrameTime;
    uint8_t m_animationFrame;

    // Custom animations (Phase 2)
    static const uint8_t MAX_CUSTOM_ANIMATIONS = 8;
    CustomAnimation* m_customAnimations[MAX_CUSTOM_ANIMATIONS];
    uint8_t m_customAnimationCount;
    CustomAnimation* m_activeCustomAnimation;

    // Error rate tracking
    uint32_t m_i2cTransactionCount;
    uint32_t m_i2cFailureCount;
    float m_errorRate;
    uint32_t m_lastErrorRateUpdate;

    // Mock mode
    uint8_t m_mockFrame[8];

    /**
     * @brief Update current animation
     */
    void updateAnimation();

    /**
     * @brief Motion alert animation
     */
    void animateMotionAlert();

    /**
     * @brief Battery low animation
     *
     * @param percentage Battery percentage (0-100)
     */
    void animateBatteryLow(uint8_t percentage);

    /**
     * @brief Boot status animation
     *
     * @param status Status text
     */
    void animateBootStatus(const char* status);

    /**
     * @brief Draw arrow symbol
     */
    void drawArrow();

    /**
     * @brief Flash display
     *
     * @param times Number of times to flash
     */
    void flashDisplay(uint8_t times);

    /**
     * @brief Write current frame to hardware
     */
    void writeDisplay();

    /**
     * @brief Animate custom animation (Phase 2)
     */
    void animateCustom();

    /**
     * @brief Find custom animation by name (Phase 2)
     *
     * @param name Animation name
     * @return CustomAnimation* Pointer to animation or nullptr if not found
     */
    CustomAnimation* findCustomAnimation(const char* name);
};

#endif // STEPAWARE_HAL_LEDMATRIX_8X8_H
