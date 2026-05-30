/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * main.c — HiveKit SHT40 sensor firmware entry point
 *
 * ⚠️  UNTESTED — community verification welcome.
 *     Flash and report results: https://github.com/petarivanov-msft/hivekit/issues
 *
 * Target: Seeed XIAO ESP32-C6 + SHT40 breakout board
 * I²C:    SDA=GPIO22, SCL=GPIO23  (same as SCD40/BME280 builds)
 * I²C addr: 0x44 (default, SHT40-AD1B) — see Kconfig.projbuild
 * Button: BOOT GPIO9 (pairing / factory reset)
 * LED:    GPIO15
 *
 * Zigbee identity:
 *   manufacturerName = "HiveKit"
 *   modelIdentifier  = "hk-sht40-c6"
 *
 * Clusters exposed:
 *   0x0402 Temperature Measurement (int16, ×100 °C)
 *   0x0405 Relative Humidity       (uint16, ×100 %)
 *
 * Operating mode:
 *   SHT40 is polled every CONFIG_HIVEKIT_SHT40_MEASUREMENT_INTERVAL_MS (default 30s).
 *   High-precision measurement command (0xFD), no heater.
 *   Source: SHT40 datasheet §2.4, Table 4
 *   URL: https://sensirion.com/media/documents/33C09C07/626C2DBA/Datasheet_SHT4x.pdf
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
#include "sensor_sht40.h"

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
#define HIVEKIT_MODEL        "hk-sht40-c6"
#define HIVEKIT_FW_VERSION   "1.0.0-phase2-untested"

#ifndef CONFIG_HIVEKIT_SHT40_MEASUREMENT_INTERVAL_MS
#define CONFIG_HIVEKIT_SHT40_MEASUREMENT_INTERVAL_MS 30000
#endif

#define SENSOR_INTERVAL_MS CONFIG_HIVEKIT_SHT40_MEASUREMENT_INTERVAL_MS

/* ── Sensor task ──────────────────────────────────────────────────────────── */

static void sensor_task(void *pvParameters)
{
    esp_err_t err;

    /* Initialise I²C + SHT40: soft reset, verify sensor presence */
    err = sht40_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SHT40 init failed: %s — sensor task exiting", esp_err_to_name(err));
        hivekit_led_set_pattern(HIVEKIT_LED_ERROR);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "SHT40 ready, entering measurement loop (interval=%dms)", SENSOR_INTERVAL_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));

        sht40_reading_t raw;
        err = sht40_read_high_precision(&raw);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SHT40 read failed: %s", esp_err_to_name(err));
            hivekit_led_set_pattern(HIVEKIT_LED_ERROR);
            continue;
        }

        /* Map local reading struct to hivekit shared type */
        hivekit_sht40_reading_t reading = {
            .temperature_c = raw.temperature_c,
            .humidity_pct  = raw.humidity_pct,
        };

        ESP_LOGI(TAG, "SHT40: T=%.2f°C  RH=%.1f%%",
                 reading.temperature_c, reading.humidity_pct);

        hivekit_led_set_pattern(HIVEKIT_LED_SINGLE_FLASH);
        hivekit_report_sht40(&reading);
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

    /* Build Zigbee device (Temperature + Humidity clusters) */
    ESP_ERROR_CHECK(hivekit_create_sht40_device());

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

    ESP_LOGI(TAG, "HiveKit SHT40 firmware %s starting", HIVEKIT_FW_VERSION);
    ESP_LOGI(TAG, "⚠️  UNTESTED — please report results at https://github.com/petarivanov-msft/hivekit/issues");

    xTaskCreate(zigbee_main_task, "zigbee_main", 8192, NULL, 5, NULL);
    xTaskCreate(sensor_task,      "sensor",       4096, NULL, 4, NULL);
}
