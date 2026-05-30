/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * main.c — HiveKit BME280 sensor firmware entry point
 *
 * ⚠️  UNTESTED — community verification welcome.
 *     Flash and report results: https://github.com/petarivanov-msft/hivekit/issues
 *
 * Target: Seeed XIAO ESP32-C6 + BME280 breakout board
 * I²C:    SDA=GPIO22, SCL=GPIO23  (same as SCD40 build)
 * I²C addr: 0x76 (SDO → GND) or 0x77 (SDO → 3.3V) — see sdkconfig.defaults
 * Button: BOOT GPIO9 (pairing / factory reset)
 * LED:    GPIO15
 *
 * Zigbee identity:
 *   manufacturerName = "HiveKit"
 *   modelIdentifier  = "hk-bme280-c6"
 *
 * Clusters exposed:
 *   0x0402 Temperature Measurement (int16, ×100 °C)
 *   0x0405 Relative Humidity       (uint16, ×100 %)
 *   0x0403 Pressure Measurement    (int16, hPa integer)
 *
 * Operating mode:
 *   BME280 is configured for FORCED mode per Bosch recommendation §3.5.1.
 *   One measurement every CONFIG_HIVEKIT_BME280_MEASUREMENT_INTERVAL_MS (default 30s).
 *   T×2, H×1, P×16, IIR filter=16.
 *   Source: BME280 datasheet §3.5.1 "Recommended modes of operation"
 *   URL: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "esp_zigbee.h"
#include "hivekit.h"
#include "hivekit_board.h"
#include "sensor_bme280.h"

/* ── Zigbee config helpers ────────────────────────────────────────────────── */

#ifndef ESP_ZIGBEE_STORAGE_PARTITION_NAME
#define ESP_ZIGBEE_STORAGE_PARTITION_NAME "zb_storage"
#endif

#define HIVEKIT_ZED_CONFIG() \
    { \
        .device_type         = EZB_NWK_DEVICE_TYPE_END_DEVICE, \
        .install_code_policy = false, \
        .zed_config = { \
            .ed_timeout = EZB_NWK_ED_TIMEOUT_64MIN, \
            .keep_alive = CONFIG_HIVEKIT_ZIGBEE_KEEP_ALIVE_MS, \
        }, \
    }

#define HIVEKIT_PLATFORM_CONFIG() \
    { \
        .storage_partition_name = ESP_ZIGBEE_STORAGE_PARTITION_NAME, \
        .radio_config = { \
            .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE, \
        }, \
    }

#define HIVEKIT_ZIGBEE_DEFAULT_CONFIG() \
    { \
        .device_config   = HIVEKIT_ZED_CONFIG(), \
        .platform_config = HIVEKIT_PLATFORM_CONFIG(), \
    }

static const char *TAG = "hivekit_main";

#define HIVEKIT_MANUFACTURER "HiveKit"
#define HIVEKIT_MODEL        "hk-bme280-c6"
#define HIVEKIT_FW_VERSION   "1.0.0-phase2-untested"

#ifndef CONFIG_HIVEKIT_BME280_MEASUREMENT_INTERVAL_MS
#define CONFIG_HIVEKIT_BME280_MEASUREMENT_INTERVAL_MS 30000
#endif

#define SENSOR_INTERVAL_MS CONFIG_HIVEKIT_BME280_MEASUREMENT_INTERVAL_MS

/* ── Sensor task ──────────────────────────────────────────────────────────── */

static void sensor_task(void *pvParameters)
{
    esp_err_t err;

    /* Initialise I²C + BME280: verify chip ID, load calibration, apply config */
    err = bme280_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BME280 init failed: %s — sensor task exiting", esp_err_to_name(err));
        hivekit_led_set_pattern(HIVEKIT_LED_ERROR);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "BME280 ready, entering measurement loop (interval=%dms)", SENSOR_INTERVAL_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));

        bme280_reading_t raw;
        err = bme280_read_forced(&raw);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "BME280 read failed: %s", esp_err_to_name(err));
            hivekit_led_set_pattern(HIVEKIT_LED_ERROR);
            continue;
        }

        /* Map local reading struct to hivekit shared type */
        hivekit_bme280_reading_t reading = {
            .temperature_c = raw.temperature_c,
            .humidity_pct  = raw.humidity_pct,
            .pressure_hpa  = raw.pressure_hpa,
        };

        ESP_LOGI(TAG, "BME280: T=%.2f°C  RH=%.1f%%  P=%.2f hPa",
                 reading.temperature_c, reading.humidity_pct, reading.pressure_hpa);

        hivekit_led_set_pattern(HIVEKIT_LED_SINGLE_FLASH);
        hivekit_report_bme280(&reading);
    }
}

/* ── Zigbee main task ─────────────────────────────────────────────────────── */

static void zigbee_main_task(void *pvParameters)
{
    esp_zigbee_config_t config = HIVEKIT_ZIGBEE_DEFAULT_CONFIG();

    ESP_ERROR_CHECK(esp_zigbee_init(&config));
    ESP_LOGI(TAG, "Keep-alive: %d ms", CONFIG_HIVEKIT_ZIGBEE_KEEP_ALIVE_MS);

    static const hivekit_config_t hk_cfg = {
        .manufacturer_name = HIVEKIT_MANUFACTURER,
        .model_identifier  = HIVEKIT_MODEL,
        .fw_version        = HIVEKIT_FW_VERSION,
    };
    ESP_ERROR_CHECK(hivekit_init(&hk_cfg));

    /* Build Zigbee device (Temperature + Humidity + Pressure clusters) */
    ESP_ERROR_CHECK(hivekit_create_bme280_device());

    /* Disable distributed security — Z2M uses centralized TC */
    ezb_aps_secur_enable_distributed_security(false);
    ezb_bdb_set_primary_channel_set(CONFIG_HIVEKIT_ZIGBEE_PRIMARY_CHANNEL_MASK);
    ESP_LOGI(TAG, "Primary channel mask: 0x%08x", CONFIG_HIVEKIT_ZIGBEE_PRIMARY_CHANNEL_MASK);

    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();

    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

/* ── app_main ─────────────────────────────────────────────────────────────── */

void app_main(void)
{
    /* Select on-PCB chip antenna (XIAO ESP32-C6) */
    ESP_ERROR_CHECK(hivekit_board_xiao_c6_init_antenna());

    /* Init NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase — erasing and reinitialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(nvs_flash_init_partition(ESP_ZIGBEE_STORAGE_PARTITION_NAME));

    ESP_LOGI(TAG, "HiveKit BME280 firmware %s starting", HIVEKIT_FW_VERSION);
    ESP_LOGI(TAG, "⚠️  UNTESTED — please report results at https://github.com/petarivanov-msft/hivekit/issues");

    xTaskCreate(zigbee_main_task, "zigbee_main", 8192, NULL, 5, NULL);
    xTaskCreate(sensor_task,      "sensor",       4096, NULL, 4, NULL);
}
