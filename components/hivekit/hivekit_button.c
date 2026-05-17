/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * hivekit_button.c — BOOT button handling for XIAO ESP32-C6
 *
 * Hardware: BOOT button = GPIO9, active LOW, internal pull-up.
 *
 * Behaviour:
 *   < 1s (single press)  → identify (LED 3× blink)
 *   3s long press        → pairing mode (network steering)
 *   10s very long press  → factory reset
 *
 * API VERIFICATION (2026-05-17):
 *   ezb_bdb_dev_joined()                    → ezbee/bdb.h
 *   ezb_bdb_start_top_level_commissioning() → ezbee/bdb.h
 *   EZB_BDB_MODE_NETWORK_STEERING           → ezbee/bdb.h
 *   ezb_bdb_reset_via_local_action()        → ezbee/bdb.h
 *   esp_zigbee_factory_reset()              → esp_zigbee.h
 *   esp_zigbee_lock_acquire()               → esp_zigbee.h
 *   esp_zigbee_lock_release()               → esp_zigbee.h
 *   button component:
 *     iot_button_create()                   → espressif/button ^4.0.0
 *     iot_button_register_cb()              → espressif/button ^4.0.0
 *     BUTTON_SINGLE_CLICK, BUTTON_LONG_PRESS_HOLD → button_types.h
 *
 * NOTE: button component callback types may differ between versions.
 * Verify button_config_t and event types against your exact version:
 *   idf_component.yml says: espressif/button: "^4.0.0"
 *   Docs: https://components.espressif.com/components/espressif/button
 */

#include "hivekit.h"

#include "esp_log.h"
#include "esp_zigbee.h"
#include "ezbee/bdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

/* TODO (Phase 1): include button component header when confirmed available.
 * Expected: #include "iot_button.h"
 * Conditional compile guards the stub below. */
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
    /* TODO (Phase 1): send Identify cluster command to coordinator */
}

void hivekit_on_button_long_press_3s(void)
{
    /* SOURCE: bdb.h — ezb_bdb_dev_joined(), ezb_bdb_start_top_level_commissioning() */
    if (ezb_bdb_dev_joined()) {
        ESP_LOGI(TAG, "3s press: already joined, flashing to indicate");
        hivekit_led_blink(2, 100);
        return;
    }
    ESP_LOGI(TAG, "3s press: starting network steering");
    hivekit_led_set_pattern(HIVEKIT_LED_SLOW_BLINK);
    esp_zigbee_lock_acquire(portMAX_DELAY);
    /* SOURCE: bdb.h — EZB_BDB_MODE_NETWORK_STEERING */
    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
    esp_zigbee_lock_release();
}

void hivekit_on_button_long_press_10s(void)
{
    ESP_LOGI(TAG, "10s press: factory reset");
    hivekit_led_set_pattern(HIVEKIT_LED_FAST_BLINK);

    esp_zigbee_lock_acquire(portMAX_DELAY);
    /* SOURCE: bdb.h — ezb_bdb_dev_joined(), ezb_bdb_reset_via_local_action() */
    if (ezb_bdb_dev_joined()) {
        /* Proper Zigbee leave + clear NVS. Triggers ZB_ZDO_SIGNAL_LEAVE
         * with EZB_NWK_LEAVE_TYPE_RESET.
         * SOURCE: bdb.h — ezb_bdb_reset_via_local_action() */
        ezb_bdb_reset_via_local_action();
    }
    esp_zigbee_lock_release();

    /* Give stack time to flush then hard-reset.
     * esp_zigbee_factory_reset() is declared __noreturn.
     * SOURCE: esp_zigbee.h — esp_zigbee_factory_reset() */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_zigbee_factory_reset();
}

void hivekit_set_button_single_press_cb(void (*cb)(void))
{
    s_single_press_cb = cb;
}

/* ── Button driver init ────────────────────────────────────────────────────── */

#if CONFIG_HIVEKIT_BUTTON_ENABLED
static void button_event_cb(void *arg, void *usr_data)
{
    /* TODO (Phase 1): adapt to actual iot_button callback signature
     * In button v4.x the callback receives (void *button_handle, void *usr_data)
     * and you query the event with iot_button_get_event(handle).
     * Verify at build time. */
    int event = (int)(intptr_t)usr_data;
    switch (event) {
    case 0: hivekit_on_button_single_press();    break;
    case 1: hivekit_on_button_long_press_3s();   break;
    case 2: hivekit_on_button_long_press_10s();  break;
    default: break;
    }
}

esp_err_t hivekit_button_init(void)
{
    /* XIAO ESP32-C6 BOOT button: GPIO9, active LOW, pull-up enabled.
     * SOURCE: https://wiki.seeedstudio.com/XIAO_ESP32C6_Getting_Started/
     *         button-led-spec.md */
    button_config_t btn_cfg = {
        .type                 = BUTTON_TYPE_GPIO,
        .long_press_time      = CONFIG_HIVEKIT_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time     = CONFIG_HIVEKIT_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config   = {
            .gpio_num     = CONFIG_HIVEKIT_BUTTON_GPIO,
            .active_level = 0, /* active LOW */
        },
    };

    button_handle_t btn = iot_button_create(&btn_cfg);
    ESP_RETURN_ON_FALSE(btn, ESP_FAIL, TAG, "Failed to create button handle");

    /* TODO (Phase 1): register events for 3s and 10s holds.
     * iot_button in v4 supports BUTTON_LONG_PRESS_HOLD with time config.
     * Exact API: verify against https://components.espressif.com/components/espressif/button changelog */
    iot_button_register_cb(btn, BUTTON_SINGLE_CLICK,  button_event_cb, (void *)0);
    /* iot_button_register_cb(btn, BUTTON_LONG_PRESS_HOLD, ...) — TODO fill in timing */

    ESP_LOGI(TAG, "Button initialised on GPIO%d", CONFIG_HIVEKIT_BUTTON_GPIO);
    return ESP_OK;
}
#else
esp_err_t hivekit_button_init(void)
{
    ESP_LOGW(TAG, "Button support disabled via Kconfig");
    return ESP_OK;
}
#endif /* CONFIG_HIVEKIT_BUTTON_ENABLED */
