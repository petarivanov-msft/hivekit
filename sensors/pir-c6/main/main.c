/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * main.c — HiveKit PIR sensor firmware entry point
 *
 * ⚠️  UNTESTED — community verification welcome.
 *     Flash and report results: https://github.com/petarivanov-msft/hivekit/issues
 *
 * Target: Seeed XIAO ESP32-C6 + PIR module (AM312 recommended; HC-SR501 supported)
 * PIR:    OUT=GPIO2 (default; configurable via Kconfig)
 * Button: BOOT GPIO9 (pairing / factory reset)
 * LED:    GPIO15
 *
 * Zigbee identity:
 *   manufacturerName = "HiveKit"
 *   modelIdentifier  = "hk-pir-c6"
 *
 * Clusters exposed:
 *   0x0406 Occupancy Sensing (bitmap8 bit0: occupied/unoccupied)
 *
 * Operating mode:
 *   Interrupt-driven, always-on. Deep sleep is NOT used — a PIR must stay awake
 *   to detect motion. Power draw: ~50 µA quiescent (AM312) + ESP32-C6 awake current.
 *   See README.md for details.
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
#include "sensor_pir.h"

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
#define HIVEKIT_MODEL        "hk-pir-c6"
#define HIVEKIT_FW_VERSION   "1.0.0-phase2-untested"

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

    /* Build Zigbee device (Occupancy Sensing cluster) */
    ESP_ERROR_CHECK(hivekit_create_pir_device());

    /* Disable distributed security — Z2M uses centralized TC */
    ezb_aps_secur_enable_distributed_security(false);
    ezb_bdb_set_primary_channel_set(CONFIG_HIVEKIT_ZIGBEE_PRIMARY_CHANNEL_MASK);
    ESP_LOGI(TAG, "Primary channel mask: 0x%08x", CONFIG_HIVEKIT_ZIGBEE_PRIMARY_CHANNEL_MASK);

    /* Initialise PIR GPIO ISR AFTER Zigbee is ready, so the first report
     * has a valid attribute store to write into. */
    ESP_ERROR_CHECK(pir_init());

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

    ESP_LOGI(TAG, "HiveKit PIR firmware %s starting", HIVEKIT_FW_VERSION);
    ESP_LOGI(TAG, "PIR GPIO: %d, active-%s",
             CONFIG_HIVEKIT_PIR_GPIO,
             CONFIG_HIVEKIT_PIR_ACTIVE_HIGH ? "high" : "low");
    ESP_LOGI(TAG, "⚠️  UNTESTED — please report results at https://github.com/petarivanov-msft/hivekit/issues");

    xTaskCreate(zigbee_main_task, "zigbee_main", 8192, NULL, 5, NULL);
}
