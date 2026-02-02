/**
 * ulp_pir_monitor.c — ULP RISC-V PIR motion detector for ESP32-C3 deep sleep.
 *
 * Compiled separately by PlatformIO ULP build step (board_build.ulp_sources).
 * Entry point at address 0; linked into main firmware as a binary blob
 * exposed via linker symbols ulp_pir_monitor_start[] and ulp_pir_monitor_size.
 *
 * Behaviour
 * ---------
 * Poll GPIO1 (PIR_SENSOR_PIN) every ~11 ms.  When the pin reads HIGH
 * (motion detected), wake the main CPU.  The main CPU sees wakeup cause
 * ESP_SLEEP_WAKEUP_ULP and routes to STATE_MOTION_ALERT.
 *
 * No shared flag in RTC memory is needed: ulp_riscv_wake_main_core() is
 * sufficient to produce the ULP wakeup cause on the main core side.
 *
 * GPIO access
 * -----------
 * The ULP RISC-V core can read the GPIO input-status register directly.
 * On ESP32-C3 this register is at a fixed address in the GPIO peripheral.
 * We use the SDK-provided GPIO_IN_REG macro when available; otherwise fall
 * back to the hard-coded address (peripheral base 0x3F400000 + offset 0x04).
 *
 * Delay
 * -----
 * ulp_riscv_delay() busy-waits for N ticks of the RTC slow clock (~136 kHz,
 * period ~7.35 µs/tick).  1500 ticks ≈ 11 ms — fast enough to catch any
 * PIR output pulse (AM312 hold time ≥ 1 s) while adding only ~20–30 µA of
 * average current to the ~120 µA quiescent deep-sleep budget.
 */

#include <stdint.h>
#include "ulp_riscv.h"

/*
 * GPIO_IN register address.
 * Prefer the SDK-provided macro; hard-code as fallback.
 * ESP32-C3: GPIO peripheral base = 0x3F400000, IN register offset = 0x04.
 */
#ifndef GPIO_IN_REG
#define GPIO_IN_REG  0x3F400004U
#endif

/* Bit mask for GPIO1 (PIR_SENSOR_PIN on StepAware hardware) */
#define PIR_GPIO_MASK   (1U << 1)

/* Poll interval in RTC slow-clock ticks (~11 ms at 136 kHz) */
#define POLL_DELAY_TICKS  1500U

static inline uint32_t gpio_in_read(void)
{
    return *(volatile uint32_t *)GPIO_IN_REG;
}

int main(void)
{
    while (1)
    {
        if (gpio_in_read() & PIR_GPIO_MASK)
        {
            /* Motion detected — wake the main CPU.
             * esp_sleep_get_wakeup_cause() will return ESP_SLEEP_WAKEUP_ULP. */
            ulp_riscv_wake_main_core();

            /* Spin here until the main core boots and halts the ULP.
             * Prevents re-triggering a second wake during main-core boot. */
            while (1) {}
        }

        ulp_riscv_delay(POLL_DELAY_TICKS);
    }

    return 0;  /* never reached */
}
