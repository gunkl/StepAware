#pragma once

// StepAware 8x8 LED matrix (Adafruit Mini 8x8 w/ HT16K33 I2C backpack).
// Port of HAL_LEDMatrix_8x8 (src/hal_ledmatrix_8x8.cpp) onto ESPHome's I2CDevice.
// Frames use the old firmware's format: one byte per row, bit 7 = leftmost pixel (x = 0).

#include <cstdint>
#include <utility>

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace stepaware_matrix {

/// Built-in animations. The first seven match HAL_LEDMatrix_8x8::AnimationPattern
/// (ANIM_CUSTOM dropped: no LittleFS in ESPHome). The last four are the formerly
/// loadable data/animations/*.txt files compiled in as static frame tables.
enum AnimationType : uint8_t {
    ANIM_NONE = 0,           // No animation
    ANIM_MOTION_ALERT,       // Flash arrow / inverse, scroll down, flash (2.4 s cycle)
    ANIM_BATTERY_LOW,        // Battery draining 33% -> empty, 400 ms per frame
    ANIM_BOOT_STATUS,        // Circle-check icon
    ANIM_ERROR,              // X icon
    ANIM_WIFI_CONNECTED,     // WiFi signal icon
    ANIM_WIFI_DISCONNECTED,  // Broken WiFi icon
    ANIM_HEART,              // data/animations/heart.txt   (5 frames, 100 ms)
    ANIM_SPINNER,            // data/animations/spinner.txt (8 frames, 80 ms)
    ANIM_WAVE,               // data/animations/wave.txt    (8 frames, 120 ms)
    ANIM_ALERT,              // data/animations/alert.txt   (4 frames, 150/150/100/100 ms)
};

/// One frame of a frame-table animation (custom animation file format).
struct AnimationFrame {
    uint8_t rows[8];   // bit 7 = leftmost pixel
    uint16_t delayMs;  // time this frame stays up
};

const char *animationTypeToString(AnimationType type);

class StepAwareMatrix : public Component, public i2c::I2CDevice {
public:
    // ESPHome overrides
    void setup() override;
    void loop() override;
    void dump_config() override;
    float get_setup_priority() const override { return setup_priority::DATA; }

    // Codegen setters (called before setup)
    void setInitialBrightness(uint8_t level) { m_brightness = level > 15 ? 15 : level; }
    void setInitialRotation(uint8_t rotation) { m_rotation = rotation & 0x03; }

    // Public API (same names as the old HAL)
    /// Start an animation. durationMs == 0 loops until stopAnimation().
    void startAnimation(AnimationType type, uint32_t durationMs = 0);
    /// Stop the current animation and blank the display (no-op if nothing is playing).
    void stopAnimation();
    /// Blank the display immediately (does not stop a running animation).
    void clear();
    /// Light the first `pixels` (0-64) pixels in SNAKE_ORDER. Direct draw, no animation state.
    void setSnakeProgress(uint8_t pixels);
    /// Brightness 0-15 (clamped), applied immediately. Notifies brightness listeners.
    void setBrightness(uint8_t level0to15);
    /// Called with the new level after every setBrightness() (HA number, actions, C++).
    template<typename F> void addOnBrightnessCallback(F &&callback) {
        m_brightnessCallbacks.add(std::forward<F>(callback));
    }
    /// Rotation in 90 degree steps (0-3), applied immediately.
    void setRotation(uint8_t rotation);
    /**
     * @brief Put the HT16K33 into standby before deep sleep (display off, oscillator off).
     *
     * A blank but running HT16K33 still draws its oscillator / driver current; standby
     * (system setup 0x20) is the chip's lowest-power state. Draws are dropped while in
     * standby. If the loop is still running STANDBY_RESUME_MS later the sleep did not
     * happen, so the display is re-initialised automatically (setup() does the same after
     * every boot).
     */
    void standby();
    bool isStandby() const { return m_standby; }

    bool isAnimating() const { return m_pattern != ANIM_NONE; }
    AnimationType getPattern() const { return m_pattern; }
    bool isReady() const { return m_ready; }
    uint8_t getBrightness() const { return m_brightness; }
    uint8_t getRotation() const { return m_rotation; }

    // Extra helpers carried over from the old HAL
    void drawFrame(const uint8_t frame[8]);
    void setPixel(uint8_t x, uint8_t y, bool on);
    void showBatteryBitmap(uint8_t percentage);
    void showWifiConnected();
    void showWifiDisconnected();
    /// Current logical frame (8 bytes, bit 7 = leftmost pixel). Read-only.
    const uint8_t *getCurrentFrame() const { return m_frame; }
    uint32_t getWriteFailureCount() const { return m_writeFailures; }

protected:
    static const uint32_t REINIT_INTERVAL_MS = 30000;
    static const uint8_t MAX_CONSECUTIVE_FAILURES = 5;
    static const uint32_t STANDBY_RESUME_MS = 1000;

    bool initHardware();
    bool writeCommand(uint8_t cmd);
    bool writeDisplay(bool force = false);
    void noteWriteResult(bool ok);

    void updateAnimation();
    void animateMotionAlert();
    void animateBatteryLow();
    void animateFrameTable(const AnimationFrame *frames, uint8_t count);

    bool m_ready{false};
    uint8_t m_brightness{15};
    uint8_t m_rotation{0};
    uint8_t m_frame[8]{};       // logical frame, MSB = leftmost pixel
    uint8_t m_ram[16]{};        // last HT16K33 RAM image written
    bool m_ramValid{false};

    AnimationType m_pattern{ANIM_NONE};
    uint32_t m_animStart{0};
    uint32_t m_animDuration{0};
    uint32_t m_lastFrameTime{0};
    uint8_t m_animFrame{0};
    uint16_t m_frameDelay{0};   // frame-table animations: delay of the frame on screen

    bool m_standby{false};
    uint32_t m_standbySince{0};

    uint32_t m_lastReinitAttempt{0};
    uint8_t m_consecutiveFailures{0};
    uint32_t m_writeFailures{0};

    CallbackManager<void(uint8_t)> m_brightnessCallbacks;
};

}  // namespace stepaware_matrix
}  // namespace esphome
