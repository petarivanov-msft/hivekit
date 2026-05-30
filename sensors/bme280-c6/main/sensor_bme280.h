/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * sensor_bme280.h — BME280 sensor driver interface
 *
 * I²C addresses:
 *   0x76 — SDO pin tied to GND (default for most breakout boards)
 *   0x77 — SDO pin tied to VDDIO (3.3V)
 *   Source: BME280 datasheet §6.2, Table 13
 *
 * I²C pins (XIAO ESP32-C6): SDA=GPIO22, SCL=GPIO23
 *   (same bus as SCD40; only one sensor per board at a time)
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief All three BME280 measurements in one structure.
 *
 * Pressure is in hPa (hectopascal). Sea-level nominal is 1013.25 hPa.
 * Temperature is in degrees Celsius (-40 to +85 for BME280).
 * Humidity is in percent relative humidity (0–100 %RH).
 */
typedef struct bme280_reading_s {
    float temperature_c;  /*!< Temperature in °C */
    float humidity_pct;   /*!< Relative humidity in % */
    float pressure_hpa;   /*!< Pressure in hPa */
} bme280_reading_t;

/**
 * @brief Initialise I²C bus, verify BME280 chip-ID, load calibration, and
 *        apply recommended indoor configuration.
 *
 * Recommended indoor config (Bosch BME280 datasheet §3.5.1 "Weather monitoring"):
 *   - Temperature oversampling ×2
 *   - Humidity oversampling ×1
 *   - Pressure oversampling ×16
 *   - IIR filter coefficient 16
 *   - Mode: FORCED (single-shot, returns to sleep after each measurement)
 *
 * This function reads calibration trim data (registers 0x88–0xA1, 0xE1–0xF0)
 * from the sensor and stores them internally for compensation.
 *
 * @return ESP_OK on success, error code otherwise.
 *         ESP_ERR_NOT_FOUND — chip ID 0xD0 did not return 0x60 (not a BME280).
 *         ESP_ERR_INVALID_CRC — calibration readback consistency check failed.
 */
esp_err_t bme280_init(void);

/**
 * @brief Trigger one forced-mode measurement and read the results.
 *
 * In forced mode the sensor performs one measurement, then returns to sleep.
 * Measurement time depends on oversampling settings; at the configured settings
 * (T×2, H×1, P×16) the typical measurement time is ~40 ms.
 * This function waits for measurement completion before returning.
 *
 * Compensation formulas port the reference C algorithm from:
 *   Bosch BME280 datasheet §4.2.3 (rev 1.14, 2018)
 *   Source: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
 *
 * @param[out] reading  Populated on success.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if sensor stays busy, other on I²C error.
 */
esp_err_t bme280_read_forced(bme280_reading_t *reading);

#ifdef __cplusplus
}
#endif
