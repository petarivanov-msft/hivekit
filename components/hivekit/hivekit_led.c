/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * hivekit_led.c — LED state machine for HiveKit devices
 *
 * Uses espressif/led_indicator v0.9.0 via IDF Component Manager.
 * Reference: https://components.espressif.com/components/espressif/led_indicator
 *
 * LED patterns (see hivekit.h for enum):
 *   HIVEKIT_LED_OFF          → all off
 *   HIVEKIT_LED_SOLID        → steady on (booting/joining success)
 *   HIVEKIT_LED_SLOW_BLINK   → 1 Hz (searching for network)
 *   HIVEKIT_LED_FAST_BLINK   → 5 Hz (factory reset in progress)
 *   HIVEKIT_LED_SINGLE_FLASH → 100 ms flash (sending reading)
 *   HIVEKIT_LED_ERROR        → 3 short flashes, then off
 *
 * GPIO: CONFIG_HIVEKIT_LED_GPIO (default 15 for XIAO ESP32-C6 built-in LED).
 * Polarity: CONFIG_HIVEKIT_LED_ACTIVE_HIGH (default true).
 *
 * TODO (Phase 1): The led_indicator API should be verified against the actual
 * component version at build time. This scaffold uses the patterns from
 * led_indicator docs. If blink_type_t definition differs, adapt accordingly.
 */

#include "hivekit.h"
#include "esp_log.h"

#if CONFIG_HIVEKIT_LED_ENABLED
#include "led_indicator.h"

static const char *TAG = "hivekit_led";
static led_indicator_handle_t s_led_handle = NULL;

/* Blink pattern definitions (led_indicator blink_step_t format).
 * Each entry: {LED_STATE_ON/OFF, delay_ms}, terminated by {LED_STATE_STOP, 0}.
 *
 * TODO: verify blink_step_t field names against your led_indicator version */
static const blink_step_t s_slow_blink[] = {
    {LED_STATE_ON,  500},
    {LED_STATE_OFF, 500},
    {LED_STATE_LOOP, 0},
};

static const blink_step_t s_fast_blink[] = {
    {LED_STATE_ON,  100},
    {LED_STATE_OFF, 100},
    {LED_STATE_LOOP, 0},
};

static const blink_step_t s_single_flash[] = {
    {LED_STATE_ON,   100},
    {LED_STATE_OFF,    0},
    {LED_STATE_STOP,   0},
};

static const blink_step_t s_error_flash[] = {
    {LED_STATE_ON,  100},
    {LED_STATE_OFF, 100},
    {LED_STATE_ON,  100},
    {LED_STATE_OFF, 100},
    {LED_STATE_ON,  100},
    {LED_STATE_OFF,   0},
    {LED_STATE_STOP,  0},
};

/* Pattern index must match hivekit_led_pattern_t order */
static const blink_step_t *const s_patterns[] = {
    [HIVEKIT_LED_OFF]          = NULL,
    [HIVEKIT_LED_SOLID]        = NULL, /* handled as special case below */
    [HIVEKIT_LED_SLOW_BLINK]   = s_slow_blink,
    [HIVEKIT_LED_FAST_BLINK]   = s_fast_blink,
    [HIVEKIT_LED_SINGLE_FLASH] = s_single_flash,
    [HIVEKIT_LED_ERROR]        = s_error_flash,
};

esp_err_t hivekit_led_init(void)
{
    led_indicator_config_t cfg = {
        .mode               = LED_GPIO_MODE,
        .led_gpio_config    = {
            .gpio_num         = CONFIG_HIVEKIT_LED_GPIO,
            .is_active_level_high = CONFIG_HIVEKIT_LED_ACTIVE_HIGH,
        },
        .blink_lists        = (blink_step_t **)s_patterns,
        .blink_list_num     = sizeof(s_patterns) / sizeof(s_patterns[0]),
    };
    s_led_handle = led_indicator_create(&cfg);
    if (!s_led_handle) {
        ESP_LOGE(TAG, "Failed to create LED indicator");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "LED initialised on GPIO%d", CONFIG_HIVEKIT_LED_GPIO);
    return ESP_OK;
}

void hivekit_led_set_pattern(hivekit_led_pattern_t pattern)
{
    if (!s_led_handle) return;

    led_indicator_stop(s_led_handle, HIVEKIT_LED_SLOW_BLINK);
    led_indicator_stop(s_led_handle, HIVEKIT_LED_FAST_BLINK);

    switch (pattern) {
    case HIVEKIT_LED_OFF:
        led_indicator_set_on_off(s_led_handle, false);
        break;
    case HIVEKIT_LED_SOLID:
        led_indicator_set_on_off(s_led_handle, true);
        break;
    default:
        if (s_patterns[pattern]) {
            led_indicator_start(s_led_handle, pattern);
        }
        break;
    }
}

void hivekit_led_blink(uint8_t count, uint32_t on_ms)
{
    if (!s_led_handle || count == 0) return;
    /* For ad-hoc blink: toggle manually. In production use a dynamic blink list.
     * This is acceptable for identify (rare, non-critical). */
    for (uint8_t i = 0; i < count; i++) {
        led_indicator_set_on_off(s_led_handle, true);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_indicator_set_on_off(s_led_handle, false);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
    }
}

#else /* CONFIG_HIVEKIT_LED_ENABLED */

esp_err_t hivekit_led_init(void)        { return ESP_OK; }
void hivekit_led_set_pattern(hivekit_led_pattern_t p) { (void)p; }
void hivekit_led_blink(uint8_t c, uint32_t ms)  { (void)c; (void)ms; }

#endif /* CONFIG_HIVEKIT_LED_ENABLED */
