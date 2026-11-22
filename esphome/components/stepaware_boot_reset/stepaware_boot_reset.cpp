#include "stepaware_boot_reset.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

#include <driver/gpio.h>
#include <esp_system.h>
#include <nvs.h>

#include <cinttypes>
#include <cstdio>

namespace esphome::stepaware_boot_reset {

static const char *const TAG = "stepaware_boot_reset";

static constexpr uint32_t POLL_MS = 10;
static constexpr uint32_t DEBOUNCE_MS = 50;
static constexpr uint32_t PULSE_PERIOD_MS = 2000;  // HAL_LED PATTERN_PULSE: 2 s cycle
static constexpr uint32_t BLINK_FAST_MS = 250;     // LED_BLINK_FAST_MS
static constexpr uint32_t CONFIRM_BLINK_MS = 200;  // performWiFiReset(): 3 x 200 ms
static constexpr uint32_t CONFIRM_SOLID_MS = 2000; // performFactoryReset(): 2 s solid

// ESPHome's NVS namespace (esp32/preferences.cpp) and WiFiComponent::start() keys: saved
// credentials live under App.get_config_version_hash() when the YAML has a compiled network
// (88491487 otherwise); fast-connect data under key + 1.
static const char *const NVS_NAMESPACE = "esphome";
static constexpr uint32_t WIFI_KEY_NO_STA = 88491487UL;

void StepAwareBootReset::setup() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
        // A button wake must reach the core as a normal press; resets need a real boot.
        return;
    }
    m_checked = true;

    gpio_config_t conf{};
    conf.pin_bit_mask = 1ULL << m_buttonPin;
    conf.mode = GPIO_MODE_INPUT;
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&conf);
    delayMicroseconds(50);  // let the pull-up settle

    if (!isHeldStable(DEBOUNCE_MS)) {
        return;
    }
    m_wasHeld = true;

    ESP_LOGW(TAG, "Button held during boot - release to cancel");
    ESP_LOGW(TAG, "Hold %" PRIu32 " s for WiFi reset, %" PRIu32 " s for factory reset",
             m_wifiHoldMs / 1000, m_factoryHoldMs / 1000);
    prepareOutput();

    const uint32_t start = millis();
    uint32_t releasedSince = 0;
    Stage stage = STAGE_NONE;

    while (true) {
        const uint32_t now = millis();
        const uint32_t held = now - start;

        if (isPressed()) {
            releasedSince = 0;
        } else if (releasedSince == 0) {
            releasedSince = now | 1u;
        } else if (now - releasedSince >= DEBOUNCE_MS) {
            break;  // released (debounced)
        }

        if (held >= m_maxWaitMs) {
            ESP_LOGW(TAG, "Button still held after %" PRIu32 " s - stuck? Reset cancelled",
                     m_maxWaitMs / 1000);
            setLed(0.0f);
            return;
        }
        if (stage == STAGE_NONE && held >= m_wifiHoldMs) {
            stage = STAGE_WIFI_PENDING;
            ESP_LOGW(TAG, "*** WiFi reset pending *** release to confirm, keep holding for "
                          "factory reset");
        }
        if (stage == STAGE_WIFI_PENDING && held >= m_factoryHoldMs) {
            stage = STAGE_FACTORY_PENDING;
            ESP_LOGW(TAG, "*** FACTORY RESET pending *** release to erase ALL settings");
        }

        updateLed(stage, held);
        waitMs(POLL_MS);
    }

    ESP_LOGI(TAG, "Button released after %" PRIu32 " ms", millis() - start);
    switch (stage) {
        case STAGE_FACTORY_PENDING:
            performFactoryReset();
            break;
        case STAGE_WIFI_PENDING:
            performWifiReset();
            break;
        default:
            ESP_LOGI(TAG, "Reset cancelled (released too early)");
            setLed(0.0f);
            break;
    }
}

void StepAwareBootReset::dump_config() {
    ESP_LOGCONFIG(TAG, "StepAware boot reset:");
    ESP_LOGCONFIG(TAG, "  Button: GPIO%u (active low)", m_buttonPin);
    ESP_LOGCONFIG(TAG, "  WiFi reset hold: %" PRIu32 " ms, factory reset hold: %" PRIu32
                       " ms, max wait: %" PRIu32 " ms",
                  m_wifiHoldMs, m_factoryHoldMs, m_maxWaitMs);
    ESP_LOGCONFIG(TAG, "  Feedback LED: %s", m_output != nullptr ? "yes" : "no");
    ESP_LOGCONFIG(TAG, "  This boot: %s",
                  !m_checked ? "deep-sleep wake (not checked)"
                             : (m_wasHeld ? "button was held (cancelled)" : "button not held"));
}

bool StepAwareBootReset::isPressed() const {
    return gpio_get_level(static_cast<gpio_num_t>(m_buttonPin)) == 0;
}

bool StepAwareBootReset::isHeldStable(uint32_t windowMs) const {
    const uint32_t start = millis();
    while (millis() - start < windowMs) {
        if (!isPressed()) {
            return false;
        }
        delay(1);
    }
    return isPressed();
}

void StepAwareBootReset::prepareOutput() {
    if (m_outputComponent == nullptr) {
        return;
    }
    uint8_t state = m_outputComponent->get_component_state() & COMPONENT_STATE_MASK;
    if (state == COMPONENT_STATE_CONSTRUCTION) {
        // Runs the output's setup() now and moves it to SETUP; Application::setup() then
        // skips setup for it (its next call() goes straight to loop()).
        m_outputComponent->call();
    }
}

void StepAwareBootReset::setLed(float level) {
    if (m_output != nullptr) {
        m_output->set_level(level);
    }
}

void StepAwareBootReset::updateLed(Stage stage, uint32_t heldMs) {
    switch (stage) {
        case STAGE_NONE: {
            // Triangle "breathing", like HAL_LED PATTERN_PULSE.
            uint32_t phase = heldMs % PULSE_PERIOD_MS;
            uint32_t half = PULSE_PERIOD_MS / 2;
            float level = phase < half ? static_cast<float>(phase) / half
                                       : static_cast<float>(PULSE_PERIOD_MS - phase) / half;
            setLed(level);
            break;
        }
        case STAGE_WIFI_PENDING:
            setLed(((heldMs - m_wifiHoldMs) / BLINK_FAST_MS) % 2 == 0 ? 1.0f : 0.0f);
            break;
        case STAGE_FACTORY_PENDING:
            setLed(1.0f);
            break;
    }
}

void StepAwareBootReset::blinkConfirm(uint8_t count, uint32_t onMs, uint32_t offMs) {
    for (uint8_t i = 0; i < count; i++) {
        setLed(1.0f);
        waitMs(onMs);
        setLed(0.0f);
        waitMs(offMs);
    }
}

void StepAwareBootReset::waitMs(uint32_t ms) {
    const uint32_t start = millis();
    while (millis() - start < ms) {
        App.feed_wdt();
        delay(ms < POLL_MS ? ms : POLL_MS);
    }
    App.feed_wdt();
}

void StepAwareBootReset::performWifiReset() {
    ESP_LOGW(TAG, "WiFi credential reset: erasing runtime-saved WiFi settings");

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    } else {
        const uint32_t keys[] = {App.get_config_version_hash(), WIFI_KEY_NO_STA};
        unsigned erased = 0;
        for (uint32_t base : keys) {
            for (uint32_t key : {base, base + 1}) {
                char keyStr[12];
                snprintf(keyStr, sizeof(keyStr), "%" PRIu32, key);
                if (nvs_erase_key(handle, keyStr) == ESP_OK) {
                    erased++;
                }
            }
        }
        err = nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGW(TAG, "Erased %u WiFi preference entr%s (commit: %s)", erased,
                 erased == 1 ? "y" : "ies", esp_err_to_name(err));
    }
    ESP_LOGW(TAG, "WiFi will use the compiled credentials / fallback AP after reboot");

    blinkConfirm(3, CONFIRM_BLINK_MS, CONFIRM_BLINK_MS);
    rebootNow();
}

void StepAwareBootReset::performFactoryReset() {
    ESP_LOGW(TAG, "FACTORY RESET: erasing all ESPHome preferences (NVS)");
    setLed(1.0f);
    // Same as the factory_reset component: erases the whole NVS partition; the handle is
    // invalidated so nothing can be saved again before the reboot.
    global_preferences->reset();
    waitMs(CONFIRM_SOLID_MS);
    setLed(0.0f);
    rebootNow();
}

void StepAwareBootReset::rebootNow() {
    ESP_LOGW(TAG, "Rebooting");
    waitMs(200);  // let the log drain
    // Not App.safe_reboot(): most components have not been set up yet, so their
    // shutdown/teardown hooks must not run.
    esp_restart();
}

}  // namespace esphome::stepaware_boot_reset
