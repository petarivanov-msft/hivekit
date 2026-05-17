/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * main.c — HiveKit SCD40 sensor firmware entry point
 *
 * Target: Seeed XIAO ESP32-C6 + Adafruit SCD-41 (or SCD-40) breakout
 * I²C:    SDA=GPIO22, SCL=GPIO23
 * Button: BOOT GPIO9 (handled by hivekit_button.c)
 * LED:    GPIO15 (see Kconfig)
 *
 * Zigbee identity:
 *   manufacturerName = "HiveKit"
 *   modelIdentifier  = "hk-scd40-c6"
 *
 * This file is intentionally thin. All Zigbee boilerplate lives in
 * components/hivekit/. This file:
 *   1. Inits NVS
 *   2. Calls hivekit_init()
 *   3. Creates the SCD40 Zigbee device descriptor
 *   4. Starts the Zigbee stack
 *   5. Launches the sensor loop task
 *
 * API VERIFICATION (2026-05-17):
 *   nvs_flash_init()                    → nvs_flash.h
 *   nvs_flash_init_partition()          → nvs_flash.h
 *   esp_zigbee_init()                   → esp_zigbee.h
 *   esp_zigbee_start()                  → esp_zigbee.h
 *   esp_zigbee_launch_mainloop()        → esp_zigbee.h
 *   ESP_ZIGBEE_DEFAULT_CONFIG()         → esp_zigbee.h (macro wrapper)
 *   ESP_ZIGBEE_STORAGE_PARTITION_NAME   → esp_zigbee.h or sdkconfig default
 *
 * NOTE on ESP_ZIGBEE_DEFAULT_CONFIG():
 *   This macro (seen in Espressif examples) constructs a default
 *   esp_zigbee_config_t. If it's not found, construct manually:
 *     esp_zigbee_config_t config = {
 *       .device_config = {
 *         .device_type = EZB_NWK_DEVICE_TYPE_ED, // End Device
 *         .zed_config  = { .ed_timeout = ED_AGING_TIMEOUT_64MIN, .keep_alive = 4000 },
 *       },
 *       .platform_config = {
 *         .storage_partition_name = "zb_storage",
 *         .radio_config = { .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE },
 *       },
 *     };
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_zigbee.h"
#include "hivekit.h"
#include "sensor_scd40.h"

static const char *TAG = "hivekit_main";

#define HIVEKIT_MANUFACTURER "HiveKit"
#define HIVEKIT_MODEL        "hk-scd40-c6"
#define HIVEKIT_FW_VERSION   "1.0.0-phase1"

/* Sensor reading interval (ms). 300 000 = 5 minutes. */
#define SENSOR_INTERVAL_MS   (300 * 1000)

/* ── Sensor task ──────────────────────────────────────────────────────────── */

static void sensor_task(void *pvParameters)
{
    esp_err_t err;

    /* Init I²C + SCD40 sensor. May block while sensor warms up. */
    err = scd40_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SCD40 init failed: %s — sensor task exiting", esp_err_to_name(err));
        hivekit_led_set_pattern(HIVEKIT_LED_ERROR);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "SCD40 ready, entering measurement loop (interval=%dms)", SENSOR_INTERVAL_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));

        hivekit_scd40_reading_t reading;
        err = scd40_read_measurement(&reading);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SCD40 read failed: %s", esp_err_to_name(err));
            hivekit_led_set_pattern(HIVEKIT_LED_ERROR);
            continue;
        }

        ESP_LOGI(TAG, "SCD40: CO2=%.0f ppm  T=%.2f °C  RH=%.1f %%",
                 reading.co2_ppm, reading.temperature_c, reading.humidity_pct);

        hivekit_led_set_pattern(HIVEKIT_LED_SINGLE_FLASH);
        hivekit_report_scd40(&reading);
    }
}

/* ── Zigbee main task ─────────────────────────────────────────────────────── */

static void zigbee_main_task(void *pvParameters)
{
    /* SOURCE: esp_zigbee.h — ESP_ZIGBEE_DEFAULT_CONFIG() */
    esp_zigbee_config_t config = ESP_ZIGBEE_DEFAULT_CONFIG();

    ESP_ERROR_CHECK(esp_zigbee_init(&config));

    /* Init hivekit (registers signal handler, LED, etc.) */
    static const hivekit_config_t hk_cfg = {
        .manufacturer_name = HIVEKIT_MANUFACTURER,
        .model_identifier  = HIVEKIT_MODEL,
        .fw_version        = HIVEKIT_FW_VERSION,
    };
    ESP_ERROR_CHECK(hivekit_init(&hk_cfg));

    /* Build the Zigbee device (clusters, attributes) */
    ESP_ERROR_CHECK(hivekit_create_scd40_device());

    /* Start Zigbee stack — autostart=false so we control commissioning
     * via the signal handler (EZB_ZDO_SIGNAL_SKIP_STARTUP) */
    ESP_ERROR_CHECK(esp_zigbee_start(false));

    /* Hand off to the Zigbee main loop. This call does not return. */
    esp_zigbee_launch_mainloop();

    /* Unreachable */
    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

/* ── app_main ─────────────────────────────────────────────────────────────── */

void app_main(void)
{
    /* NVS is required for Zigbee NVS storage */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase — erasing and reinitialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Separate Zigbee NVS partition (zb_storage in partitions.csv).
     * SOURCE: esp_zigbee.h — ESP_ZIGBEE_STORAGE_PARTITION_NAME */
    ESP_ERROR_CHECK(nvs_flash_init_partition(ESP_ZIGBEE_STORAGE_PARTITION_NAME));

    ESP_LOGI(TAG, "HiveKit SCD40 firmware %s starting", HIVEKIT_FW_VERSION);

    /* Zigbee task: stack requires a dedicated task with sufficient stack */
    xTaskCreate(zigbee_main_task, "zigbee_main", 8192, NULL, 5, NULL);

    /* Sensor task: separate from zigbee to avoid blocking the stack */
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 4, NULL);
}
