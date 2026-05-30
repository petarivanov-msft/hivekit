/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * HiveKit shared component — public API
 *
 * API SOURCE VERIFICATION:
 *   esp_zigbee.h:  https://github.com/espressif/esp-zigbee-sdk/blob/main/components/esp-zigbee-lib/include/esp_zigbee.h
 *   bdb.h:         https://github.com/espressif/esp-zigbee-sdk/blob/main/components/esp-zigbee-lib/include/ezbee/bdb.h
 *   core.h:        https://github.com/espressif/esp-zigbee-sdk/blob/main/components/esp-zigbee-lib/include/ezbee/core.h
 *   app_signals.h: https://github.com/espressif/esp-zigbee-sdk/blob/main/components/esp-zigbee-lib/include/ezbee/app_signals.h
 *   zcl_common.h:  https://github.com/espressif/esp-zigbee-sdk/blob/main/components/esp-zigbee-lib/include/ezbee/zcl/zcl_common.h
 *   zha.h:         https://github.com/espressif/esp-zigbee-sdk/blob/main/components/esp-zigbee-lib/include/ezbee/zha.h
 *
 * All ezb_* API calls verified against the above headers on 2026-05-17.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Device identity ──────────────────────────────────────────────────────── */

/**
 * @brief Configuration passed to hivekit_init().
 *
 * manufacturer_name and model_identifier are written into the Zigbee Basic
 * cluster and are what Z2M reads during interview to match the external
 * converter.
 */
typedef struct hivekit_config_s {
    const char *manufacturer_name; /*!< e.g. "HiveKit" */
    const char *model_identifier;  /*!< e.g. "hk-scd40-c6" */
    const char *fw_version;        /*!< e.g. "1.0.0" — written to SW build ID */
} hivekit_config_t;

/* ── Sensor value types ───────────────────────────────────────────────────── */

/**
 * @brief All three SCD40 measurements in one structure.
 */
typedef struct hivekit_scd40_reading_s {
    float    co2_ppm;       /*!< CO2 in parts per million (400–5000 for SCD40) */
    float    temperature_c; /*!< Temperature in degrees Celsius */
    float    humidity_pct;  /*!< Relative humidity in percent */
} hivekit_scd40_reading_t;

/**
 * @brief SHT40 measurements: temperature and humidity.
 */
typedef struct hivekit_sht40_reading_s {
    float temperature_c; /*!< Temperature in degrees Celsius (-40 to +125 for SHT40) */
    float humidity_pct;  /*!< Relative humidity in percent (0–100) */
} hivekit_sht40_reading_t;

/**
 * @brief PIR occupancy state (wraps a bool for future extensibility).
 */
typedef struct hivekit_pir_state_s {
    bool occupied; /*!< true = motion detected / occupied; false = unoccupied */
} hivekit_pir_state_t;

/**
 * @brief BME280 measurements: temperature, humidity, and pressure.
 */
typedef struct hivekit_bme280_reading_s {
    float temperature_c; /*!< Temperature in degrees Celsius (-40 to +85) */
    float humidity_pct;  /*!< Relative humidity in percent (0–100) */
    float pressure_hpa;  /*!< Atmospheric pressure in hPa (hectopascal) */
} hivekit_bme280_reading_t;

/* ── LED patterns ─────────────────────────────────────────────────────────── */

typedef enum hivekit_led_pattern_e {
    HIVEKIT_LED_OFF          = 0, /*!< Sleeping / idle */
    HIVEKIT_LED_SOLID        = 1, /*!< Booting / initialising */
    HIVEKIT_LED_SLOW_BLINK   = 2, /*!< Searching for network (1 Hz) */
    HIVEKIT_LED_FAST_BLINK   = 3, /*!< Factory reset in progress (5 Hz) */
    HIVEKIT_LED_SINGLE_FLASH = 4, /*!< Sending a reading (100 ms) */
    HIVEKIT_LED_ERROR        = 5, /*!< 3 short flashes, then off */
} hivekit_led_pattern_t;

/* ── Core lifecycle ───────────────────────────────────────────────────────── */

/**
 * @brief Initialise the HiveKit component.
 *
 * Call this before starting the Zigbee stack. It:
 *   - Stores the device identity for use in the Basic cluster
 *   - Initialises the LED driver
 *   - Registers the app signal handler (ezb_app_signal_add_handler)
 *
 * Must be called from the Zigbee main task before esp_zigbee_start().
 *
 * @param[in] cfg Device configuration. Must remain valid for the lifetime of the app.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t hivekit_init(const hivekit_config_t *cfg);

/**
 * @brief Register a custom signal handler to be called in addition to the built-in one.
 *
 * Optional. Call before hivekit_init() or immediately after.
 */
void hivekit_set_signal_handler_cb(void (*cb)(uint16_t signal_type, const void *params));

/* ── Cluster / endpoint setup ─────────────────────────────────────────────── */

/**
 * @brief Create the PIR Zigbee device: Basic + Identify + Occupancy Sensing clusters.
 *
 * Registers endpoint 1 with the ZHA HA profile (0x0104).
 * Clusters:
 *   0x0406 Occupancy Sensing  (bitmap8 bit 0: occupied)
 *   OccupancySensorType = 0 (PIR)
 *
 * This function does NOT start GPIO monitoring. Call pir_init() separately
 * after hivekit_create_pir_device() and esp_zigbee_start().
 *
 * @return ESP_OK on success.
 */
esp_err_t hivekit_create_pir_device(void);

/**
 * @brief Push PIR occupancy state into the Zigbee attribute cache and trigger a report.
 *
 * Updates:
 *   - Occupancy Sensing Occupancy attribute (0x0406, attr 0x0000): bitmap8 bit 0
 *
 * Event-driven: called from the PIR consumer task on any debounced state change,
 * and periodically as a keepalive report.
 *
 * Must acquire esp_zigbee_lock before calling.
 *
 * @param[in] occupied true = motion present / occupied.
 * @return ESP_OK on success.
 */
esp_err_t hivekit_report_pir(bool occupied);

/**
 * @brief Create the SCD40 Zigbee device: Basic + Identify + Temp + Humidity + CO2 clusters.
 *
 * Registers endpoint 1 with the ZHA HA profile (0x0104).
 * Uses ezb_af_create_device_desc() + cluster desc builders from the v2 SDK.
 *
 * TODO (Phase 1): Add OTA Upgrade cluster (client role) once OTA support is confirmed.
 *
 * @return ESP_OK on success.
 */
esp_err_t hivekit_create_scd40_device(void);

/* ── Cluster / endpoint setup (BME280) ───────────────────────────────────── */

/* ── Cluster / endpoint setup (SHT40) ───────────────────────────────────── */

/**
 * @brief Create the SHT40 Zigbee device: Basic + Identify + Temp + Humidity clusters.
 *
 * Registers endpoint 1 with the ZHA HA profile (0x0104).
 * Clusters:
 *   0x0402 msTemperatureMeasurement  (int16, ×100 °C)
 *   0x0405 msRelativeHumidity        (uint16, ×100 %)
 *
 * @return ESP_OK on success.
 */
esp_err_t hivekit_create_sht40_device(void);

/**
 * @brief Push SHT40 readings into the Zigbee attribute cache.
 *
 * Updates two attributes:
 *   - Temperature Measurement MeasuredValue (0x0402, attr 0x0000): int16, ×100
 *   - Relative Humidity MeasuredValue       (0x0405, attr 0x0000): uint16, ×100
 *
 * Must acquire esp_zigbee_lock before calling.
 *
 * @param[in] reading Sensor measurements.
 * @return ESP_OK on success.
 */
esp_err_t hivekit_report_sht40(const hivekit_sht40_reading_t *reading);

/**
 * @brief Create the BME280 Zigbee device: Basic + Identify + Temp + Humidity + Pressure clusters.
 *
 * Registers endpoint 1 with the ZHA HA profile (0x0104).
 * Clusters:
 *   0x0402 msTemperatureMeasurement  (int16, ×100 °C)
 *   0x0405 msRelativeHumidity        (uint16, ×100 %)
 *   0x0403 msPressureMeasurement     (int16, integer hPa)
 *
 * @return ESP_OK on success.
 */
esp_err_t hivekit_create_bme280_device(void);

/**
 * @brief Push BME280 readings into the Zigbee attribute cache.
 *
 * Updates three attributes:
 *   - Temperature Measurement MeasuredValue (0x0402, attr 0x0000): int16, ×100
 *   - Relative Humidity MeasuredValue       (0x0405, attr 0x0000): uint16, ×100
 *   - Pressure Measurement MeasuredValue    (0x0403, attr 0x0000): int16, hPa
 *
 * Must acquire esp_zigbee_lock before calling.
 *
 * @param[in] reading Sensor measurements.
 * @return ESP_OK on success.
 */
esp_err_t hivekit_report_bme280(const hivekit_bme280_reading_t *reading);

/* ── Attribute reporting ──────────────────────────────────────────────────── */

/**
 * @brief Push SCD40 readings into the Zigbee attribute cache and report.
 *
 * Updates three attributes:
 *   - Temperature Measurement MeasuredValue (0x0402, attr 0x0000): int16, ×100
 *   - Relative Humidity MeasuredValue       (0x0405, attr 0x0000): uint16, ×100
 *   - CO2 Measurement MeasuredValue         (0x040d, attr 0x0000): single float, ppm/1e6
 *
 * Call this from your sensor task. Must acquire esp_zigbee_lock before calling.
 *
 * @param[in] reading Sensor measurements.
 * @return ESP_OK on success.
 */
esp_err_t hivekit_report_scd40(const hivekit_scd40_reading_t *reading);

/* ── LED helpers ──────────────────────────────────────────────────────────── */

/**
 * @brief Set the LED state machine pattern.
 *
 * Thread-safe — delegates to led_indicator, which is RTOS-safe.
 */
void hivekit_led_set_pattern(hivekit_led_pattern_t pattern);

/**
 * @brief Flash the LED N times with a given on-time in ms.
 */
void hivekit_led_blink(uint8_t count, uint32_t on_ms);

/* ── Button callbacks (invoked by hivekit_button.c) ─────────────────────── */

/**
 * @brief Callback invoked on single short press (< 1s).
 *
 * Default behaviour: blink LED 3× for identify.
 * Override by calling hivekit_set_button_single_press_cb().
 */
void hivekit_on_button_single_press(void);

/**
 * @brief Callback invoked on 3-second long press (pairing).
 *
 * Default behaviour: calls ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING).
 * SOURCE: https://github.com/espressif/esp-zigbee-sdk/blob/main/components/esp-zigbee-lib/include/ezbee/bdb.h
 */
void hivekit_on_button_long_press_3s(void);

/**
 * @brief Callback invoked on 10-second very long press (factory reset).
 *
 * Default behaviour: calls ezb_bdb_reset_via_local_action() if joined,
 * else nvram erase + restart.
 * SOURCE: https://github.com/espressif/esp-zigbee-sdk/blob/main/components/esp-zigbee-lib/include/ezbee/bdb.h
 */
void hivekit_on_button_long_press_10s(void);

/**
 * @brief Override the single-press callback. Pass NULL to restore default.
 */
void hivekit_set_button_single_press_cb(void (*cb)(void));

#ifdef __cplusplus
}
#endif
