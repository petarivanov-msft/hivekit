/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * hivekit_button.c — BOOT button handling for XIAO ESP32-C6
 *
 * API verified against espressif/button ^4.0.0 (esp-iot-solution):
 *   iot_button_new_gpio_device()  → button_gpio.h
 *   iot_button_register_cb()      → iot_button.h
 *   button_config_t               → button_types.h (long/short press times only)
 *   button_gpio_config_t          → button_gpio.h  (gpio_num, active_level)
 *   button_event_t                → iot_button.h   (BUTTON_SINGLE_CLICK etc)
 *   button_event_args_t           → iot_button.h   (long_press.press_time)
 *   button_cb_t                   → iot_button.h   (void *btn_handle, void *usr_data)
 */

#include "hivekit.h"

#include "esp_log.h"
#include "esp_zigbee.h"
#include "ezbee/bdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_check.h"

#if CONFIG_HIVEKIT_BUTTON_ENABLED
#include "iot_button.h"
#include "button_gpio.h"
#endif

static const char *TAG = "hivekit_button";
static void (*s_single_press_cb)(void) = NULL;

/* ── Default callbacks ─────────────────────────────────────────────────────── */

void hivekit_on_button_single_press(void)
{
    if (s_single_press_cb) {
        s_single_press_cb();
        return;
    }
    ESP_LOGI(TAG, "Single press: identify");
    hivekit_led_blink(3, 200);
}

void hivekit_on_button_long_press_3s(void)
{
    if (ezb_bdb_dev_joined()) {
        ESP_LOGI(TAG, "3s press: already joined");
        hivekit_led_blink(2, 100);
        return;
    }
    ESP_LOGI(TAG, "3s press: starting network steering");
    hivekit_led_set_pattern(HIVEKIT_LED_SLOW_BLINK);
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
    esp_zigbee_lock_release();
}

void hivekit_on_button_long_press_10s(void)
{
    ESP_LOGI(TAG, "10s press: factory reset");
    hivekit_led_set_pattern(HIVEKIT_LED_FAST_BLINK);
    esp_zigbee_lock_acquire(portMAX_DELAY);
    if (ezb_bdb_dev_joined()) {
        ezb_bdb_reset_via_local_action();
    }
    esp_zigbee_lock_release();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_zigbee_factory_reset();
}

void hivekit_set_button_single_press_cb(void (*cb)(void))
{
    s_single_press_cb = cb;
}

/* ── Button driver init ────────────────────────────────────────────────────── */

#if CONFIG_HIVEKIT_BUTTON_ENABLED

static void on_single_click(void *btn_handle, void *usr_data)
{
    (void)btn_handle; (void)usr_data;
    hivekit_on_button_single_press();
}

static void on_long_press_3s(void *btn_handle, void *usr_data)
{
    (void)btn_handle; (void)usr_data;
    hivekit_on_button_long_press_3s();
}

static void on_long_press_10s(void *btn_handle, void *usr_data)
{
    (void)btn_handle; (void)usr_data;
    hivekit_on_button_long_press_10s();
}

esp_err_t hivekit_button_init(void)
{
    /* button v4 API: button_config_t only has timing fields;
     * GPIO config is separate via button_gpio_config_t.
     * SOURCE: button_types.h, button_gpio.h */
    button_config_t btn_cfg = {
        .long_press_time  = CONFIG_HIVEKIT_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_HIVEKIT_BUTTON_SHORT_PRESS_TIME_MS,
    };
    button_gpio_config_t gpio_cfg = {
        .gpio_num     = CONFIG_HIVEKIT_BUTTON_GPIO,
        .active_level = 0, /* active LOW */
    };

    button_handle_t btn = NULL;
    ESP_RETURN_ON_ERROR(
        iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn),
        TAG, "Failed to create button handle");

    /* Single click */
    ESP_RETURN_ON_ERROR(
        iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, on_single_click, NULL),
        TAG, "Failed to register single-click callback");

    /* 3s long press (use BUTTON_LONG_PRESS_START with press_time arg) */
    button_event_args_t args_3s = {
        .long_press.press_time = CONFIG_HIVEKIT_BUTTON_LONG_PRESS_TIME_MS,
    };
    ESP_RETURN_ON_ERROR(
        iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, &args_3s, on_long_press_3s, NULL),
        TAG, "Failed to register 3s long-press callback");

    /* 10s very long press */
    button_event_args_t args_10s = {
        .long_press.press_time = 10000,
    };
    ESP_RETURN_ON_ERROR(
        iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, &args_10s, on_long_press_10s, NULL),
        TAG, "Failed to register 10s long-press callback");

    ESP_LOGI(TAG, "Button initialised on GPIO%d", CONFIG_HIVEKIT_BUTTON_GPIO);
    return ESP_OK;
}

#else /* CONFIG_HIVEKIT_BUTTON_ENABLED */

esp_err_t hivekit_button_init(void)
{
    ESP_LOGW(TAG, "Button support disabled via Kconfig");
    return ESP_OK;
}

#endif /* CONFIG_HIVEKIT_BUTTON_ENABLED */
