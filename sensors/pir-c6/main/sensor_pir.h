/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * sensor_pir.h — PIR motion sensor driver interface (GPIO interrupt)
 *
 * ⚠️  UNTESTED — community verification welcome.
 *     Flash and report results: https://github.com/petarivanov-msft/hivekit/issues
 *
 * Supported PIR modules:
 *   - AM312  (3.3V-compatible, recommended for direct ESP32-C6 GPIO)
 *   - HC-SR501 (5V supply required; OUT line is 3.3V-safe with many variants)
 *   See README.md for wiring and module selection guidance.
 *
 * GPIO: CONFIG_HIVEKIT_PIR_GPIO (default GPIO2)
 * Output polarity: active-high (configurable via CONFIG_HIVEKIT_PIR_ACTIVE_HIGH)
 *
 * Operating principle:
 *   PIR modules emit a digital pulse on their OUT pin when motion is detected.
 *   The AM312/HC-SR501 hold OUT HIGH for a hold-time (configurable on HC-SR501
 *   via trimmer pot, typically 2–300 s; fixed ~2 s on AM312). This driver watches
 *   BOTH edges so it can report the exact moment occupancy starts AND ends.
 *
 * Implementation pattern:
 *   ISR (IRAM_ATTR) → posts event to FreeRTOS queue → consumer task debounces
 *   and calls hivekit_report_pir(). No heap allocation, no logging in ISR.
 *
 * Reference:
 *   ESP-IDF GPIO ISR service:
 *   https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/gpio.html
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the PIR GPIO and install ISR handler.
 *
 * Configures CONFIG_HIVEKIT_PIR_GPIO as input with any-edge interrupt.
 * Installs the GPIO ISR service if not already installed (safe to call
 * multiple times globally — gpio_install_isr_service returns ESP_ERR_INVALID_STATE
 * if already installed, which this function treats as success).
 * Spawns the PIR consumer task which debounces events and calls
 * hivekit_report_pir().
 *
 * Must be called after the Zigbee stack is ready to accept reports.
 *
 * @return ESP_OK on success.
 *         ESP_ERR_INVALID_ARG if GPIO number is invalid.
 */
esp_err_t pir_init(void);

#ifdef __cplusplus
}
#endif
