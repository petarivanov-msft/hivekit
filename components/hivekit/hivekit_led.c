/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * hivekit_led.c — LED state machine for HiveKit devices
 *
 * Uses espressif/led_indicator v0.9.x (API verified against esp-iot-solution).
 * API:
 *   led_indicator_new_gpio_device()  → led_indicator_gpio.h
 *   led_indicator_start()            → led_indicator.h
 *   led_indicator_stop()             → led_indicator.h
 *   led_indicator_set_on_off()       → led_indicator.h
 *   blink_step_t: {type, value, hold_time_ms}, with LED_BLINK_LOOP/LED_BLINK_STOP
 *   led_indicator_config_t: {blink_lists, blink_list_num}
 */

#include "hivekit.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_HIVEKIT_LED_ENABLED
#include "led_indicator.h"
#include "led_indicator_gpio.h"

static const char *TAG = "hivekit_led";
static led_indicator_handle_t s_led_handle = NULL;

/*
 * blink_step_t fields: {type, value, hold_time_ms}
 * type:          LED_BLINK_HOLD | LED_BLINK_LOOP | LED_BLINK_STOP
 * value:         LED_STATE_ON (255) | LED_STATE_OFF (0)  — or brightness 0-255
 * hold_time_ms:  duration; set 0 for LOOP/STOP
 */
static const blink_step_t s_slow_blink[] = {
    {LED_BLINK_HOLD, LED_STATE_ON,  500},
    {LED_BLINK_HOLD, LED_STATE_OFF, 500},
    {LED_BLINK_LOOP, 0, 0},
};

static const blink_step_t s_fast_blink[] = {
    {LED_BLINK_HOLD, LED_STATE_ON,  100},
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_LOOP, 0, 0},
};

static const blink_step_t s_single_flash[] = {
    {LED_BLINK_HOLD, LED_STATE_ON,  100},
    {LED_BLINK_HOLD, LED_STATE_OFF,   0},
    {LED_BLINK_STOP, 0, 0},
};

static const blink_step_t s_error_flash[] = {
    {LED_BLINK_HOLD, LED_STATE_ON,  100},
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_HOLD, LED_STATE_ON,  100},
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_HOLD, LED_STATE_ON,  100},
    {LED_BLINK_HOLD, LED_STATE_OFF,   0},
    {LED_BLINK_STOP, 0, 0},
};

/* Pattern index must match hivekit_led_pattern_t order */
static const blink_step_t *const s_patterns[] = {
    [HIVEKIT_LED_OFF]          = NULL,
    [HIVEKIT_LED_SOLID]        = NULL, /* handled via led_indicator_set_on_off */
    [HIVEKIT_LED_SLOW_BLINK]   = s_slow_blink,
    [HIVEKIT_LED_FAST_BLINK]   = s_fast_blink,
    [HIVEKIT_LED_SINGLE_FLASH] = s_single_flash,
    [HIVEKIT_LED_ERROR]        = s_error_flash,
};

esp_err_t hivekit_led_init(void)
{
    led_indicator_config_t led_cfg = {
        .blink_lists    = s_patterns,
        .blink_list_num = sizeof(s_patterns) / sizeof(s_patterns[0]),
    };
    led_indicator_gpio_config_t gpio_cfg = {
        .is_active_level_high = CONFIG_HIVEKIT_LED_ACTIVE_HIGH,
        .gpio_num             = CONFIG_HIVEKIT_LED_GPIO,
    };
    esp_err_t err = led_indicator_new_gpio_device(&led_cfg, &gpio_cfg, &s_led_handle);
    if (err != ESP_OK || !s_led_handle) {
        ESP_LOGE(TAG, "Failed to create LED indicator (err=0x%x)", err);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "LED initialised on GPIO%d", CONFIG_HIVEKIT_LED_GPIO);
    return ESP_OK;
}

void hivekit_led_set_pattern(hivekit_led_pattern_t pattern)
{
    if (!s_led_handle) return;

    /* Stop running blink patterns */
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
        if ((size_t)pattern < sizeof(s_patterns)/sizeof(s_patterns[0]) &&
            s_patterns[pattern] != NULL) {
            led_indicator_start(s_led_handle, pattern);
        }
        break;
    }
}

void hivekit_led_blink(uint8_t count, uint32_t on_ms)
{
    if (!s_led_handle || count == 0) return;
    for (uint8_t i = 0; i < count; i++) {
        led_indicator_set_on_off(s_led_handle, true);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_indicator_set_on_off(s_led_handle, false);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
    }
}

#else /* CONFIG_HIVEKIT_LED_ENABLED */

esp_err_t hivekit_led_init(void)                           { return ESP_OK; }
void hivekit_led_set_pattern(hivekit_led_pattern_t p)      { (void)p; }
void hivekit_led_blink(uint8_t c, uint32_t ms)             { (void)c; (void)ms; }

#endif /* CONFIG_HIVEKIT_LED_ENABLED */
