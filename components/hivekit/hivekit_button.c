/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * hivekit_button.c — BOOT button handling for XIAO ESP32-C6
 *
 * API verified against espressif/button v4.1.6 (esp-iot-solution v2.0):
 *   iot_button_create()        → iot_button.h (one-arg: button_config_t*)
 *   iot_button_register_cb()   → 4-arg: (handle, event, cb, usr_data)
 *   button_config_t            → {type, long_press_time, short_press_time, gpio_button_config}
 *   button_gpio_config_t       → {gpio_num, active_level}
 *   BUTTON_TYPE_GPIO, BUTTON_SINGLE_CLICK, BUTTON_LONG_PRESS_START
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
    /* button v4.1.6 (esp-iot-solution v2.0): single button_config_t struct.
     * SOURCE: iot_button.h — button_config_t, BUTTON_TYPE_GPIO */
    button_config_t btn_cfg = {
        .type             = BUTTON_TYPE_GPIO,
        .long_press_time  = CONFIG_HIVEKIT_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_HIVEKIT_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num     = CONFIG_HIVEKIT_BUTTON_GPIO,
            .active_level = 0, /* active LOW */
        },
    };

    button_handle_t btn = iot_button_create(&btn_cfg);
    if (!btn) {
        ESP_LOGE(TAG, "Failed to create button handle");
        return ESP_FAIL;
    }

    /* 4-arg register_cb: (handle, event, cb, usr_data) */
    ESP_RETURN_ON_ERROR(
        iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, on_single_click, NULL),
        TAG, "Failed to register single-click callback");

    /* 3s long press: register BUTTON_LONG_PRESS_START; the timing is set via
     * btn_cfg.long_press_time. We use BUTTON_LONG_PRESS_START for both 3s and
     * 10s detection via separate handle with different long_press_time. */
    ESP_RETURN_ON_ERROR(
        iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, on_long_press_3s, NULL),
        TAG, "Failed to register long-press callback");

    /* For 10s press: create a second button handle on the same GPIO with 10s threshold */
    button_config_t btn_10s_cfg = btn_cfg;
    btn_10s_cfg.long_press_time = 10000;
    button_handle_t btn_10s = iot_button_create(&btn_10s_cfg);
    if (btn_10s) {
        iot_button_register_cb(btn_10s, BUTTON_LONG_PRESS_START, on_long_press_10s, NULL);
    }

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
