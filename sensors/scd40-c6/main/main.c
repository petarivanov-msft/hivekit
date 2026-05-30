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
 *         .zed_config  = { .ed_timeout = ED_AGING_TIMEOUT_64MIN, .keep_alive = CONFIG_HIVEKIT_ZIGBEE_KEEP_ALIVE_MS },
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
#include "esp_task_wdt.h"

#include "esp_zigbee.h"
#include "hivekit.h"
#include "hivekit_board.h"
#include "sensor_scd40.h"

/* ── Zigbee config helpers (not in SDK headers, defined per-project) ───────── */
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
#define HIVEKIT_MODEL        "hk-scd40-c6"
#define HIVEKIT_FW_VERSION   "1.0.0-phase1"

/* Sensor reading interval (ms). 30 000 = 30 seconds (Phase 1 debug). */
#define SENSOR_INTERVAL_MS   (30 * 1000)

/* Number of consecutive I²C hard-errors before attempting full re-init. */
#define SCD40_MAX_CONSEC_ERRORS  5

/* Task watchdog timeout for sensor_task (seconds). Must be longer than
 * SENSOR_INTERVAL_MS + ~6s for sensor init/warmup.  60s gives plenty of
 * headroom while still catching a true stall. */
#define SENSOR_WDT_TIMEOUT_S  60

/* ── Sensor task ──────────────────────────────────────────────────────────── */

static void sensor_task(void *pvParameters)
{
    esp_err_t err;

    /* Register this task with the task watchdog daemon so the system can
     * detect a stall in the measurement loop (Bug 2 — sensor stops reporting).
     * The zigbee_main_task is NOT registered here because the Zigbee main loop
     * internally drives its own timing and must not be interrupted. */
    err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not register sensor_task with WDT: %s (non-fatal)",
                 esp_err_to_name(err));
    }

    /* Init I²C + SCD40 sensor. May block while sensor warms up. */
    err = scd40_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SCD40 init failed: %s — sensor task exiting", esp_err_to_name(err));
        hivekit_led_set_pattern(HIVEKIT_LED_ERROR);
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "SCD40 ready, entering measurement loop (interval=%dms)", SENSOR_INTERVAL_MS);

    int consec_errors = 0;

    while (1) {
        /* Reset the watchdog every iteration — proves the loop is alive.
         * If the task stalls (e.g. I²C blocks indefinitely), the WDT fires
         * and resets the chip. */
        (void)esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));

        /* Reset watchdog again after the delay — the large sleep alone could
         * timeout if SENSOR_WDT_TIMEOUT_S is shorter than the interval. */
        (void)esp_task_wdt_reset();

        hivekit_scd40_reading_t reading;
        err = scd40_read_measurement(&reading);

        if (err == ESP_ERR_NOT_FOUND) {
            /* Data not ready — not a hard error; the SCD40 measures every 5 s
             * but we read every 30 s so this should not normally happen. Log
             * at debug level only. */
            ESP_LOGD(TAG, "SCD40 data not ready yet (skipping this cycle)");
            continue;
        }

        if (err != ESP_OK) {
            consec_errors++;
            ESP_LOGW(TAG, "SCD40 read failed (%d/%d): %s",
                     consec_errors, SCD40_MAX_CONSEC_ERRORS, esp_err_to_name(err));
            hivekit_led_set_pattern(HIVEKIT_LED_ERROR);

            if (consec_errors >= SCD40_MAX_CONSEC_ERRORS) {
                /* I²C bus appears hung. Attempt graceful recovery: de-init
                 * the bus and re-initialise the SCD40 from scratch (Bug 2).
                 * scd40_reinit() stops the measurement, resets the I²C bus,
                 * and starts periodic measurement again. */
                ESP_LOGW(TAG, "Too many consecutive errors — attempting SCD40 recovery");
                (void)esp_task_wdt_reset();
                esp_err_t re_err = scd40_reinit();
                if (re_err == ESP_OK) {
                    ESP_LOGI(TAG, "SCD40 recovery succeeded");
                    consec_errors = 0;
                } else {
                    ESP_LOGE(TAG, "SCD40 recovery failed: %s", esp_err_to_name(re_err));
                    /* Keep retrying; WDT will catch a true permanent stall. */
                }
            }
            continue;
        }

        /* Successful read — reset error counter. */
        consec_errors = 0;

        ESP_LOGI(TAG, "SCD40: CO2=%.0f ppm  T=%.2f °C  RH=%.1f %%",
                 reading.co2_ppm, reading.temperature_c, reading.humidity_pct);

        hivekit_led_set_pattern(HIVEKIT_LED_SINGLE_FLASH);
        hivekit_report_scd40(&reading);
    }
}

/* ── Zigbee main task ─────────────────────────────────────────────────────── */

static void zigbee_main_task(void *pvParameters)
{
    /* SOURCE: esp_zigbee.h — use local macro (macros live in example headers, not SDK) */
    esp_zigbee_config_t config = HIVEKIT_ZIGBEE_DEFAULT_CONFIG();

    ESP_ERROR_CHECK(esp_zigbee_init(&config));
    ESP_LOGI(TAG, "Keep-alive: %d ms", CONFIG_HIVEKIT_ZIGBEE_KEEP_ALIVE_MS);

    /* Init hivekit (registers signal handler, LED, etc.) */
    static const hivekit_config_t hk_cfg = {
        .manufacturer_name = HIVEKIT_MANUFACTURER,
        .model_identifier  = HIVEKIT_MODEL,
        .fw_version        = HIVEKIT_FW_VERSION,
    };
    ESP_ERROR_CHECK(hivekit_init(&hk_cfg));

    /* Build the Zigbee device (clusters, attributes) */
    ESP_ERROR_CHECK(hivekit_create_scd40_device());

    /* Pre-start commissioning setup — MUST be called before esp_zigbee_start().
     * SOURCE: esp-zigbee-sdk examples/temperature_sensor.c
     *
     * 1) Disable distributed security so we join a centralized trust-centre
     *    network (Zigbee2MQTT, ZHA, deCONZ all use centralized TC).
     *    v2 SDK defaults to distributed=true which silently rejects Z2M beacons
     *    with NWK_NO_NETWORKS (status=0x03).
     * 2) Scan all Zigbee channels 11-26 in a single primary mask call. */
    ezb_aps_secur_enable_distributed_security(false);
    /* Match florianL21/zigbee-co2-sensor reference (ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK).
     * v2 SDK has no convenience macro; literal mask covers 802.15.4 channels 11-26. */
    ezb_bdb_set_primary_channel_set(CONFIG_HIVEKIT_ZIGBEE_PRIMARY_CHANNEL_MASK);
    ESP_LOGI(TAG, "Primary channel mask: 0x%08x (ch 11-26 by default)",
             CONFIG_HIVEKIT_ZIGBEE_PRIMARY_CHANNEL_MASK);

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
    /* Select on-PCB chip antenna — must be the very first action.
     * Drives GPIO3=0 (RF amp enable) + GPIO14=0 (chip antenna).
     * Guarded by CONFIG_HIVEKIT_BOARD_XIAO_C6_ANTENNA (default y). */
    ESP_ERROR_CHECK(hivekit_board_xiao_c6_init_antenna());

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
