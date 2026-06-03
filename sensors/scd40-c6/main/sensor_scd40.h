/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * sensor_scd40.h — SCD40/SCD41 sensor driver wrapper interface
 */

#pragma once

#include "esp_err.h"
#include "hivekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise I²C bus and start SCD40 periodic measurement.
 *
 * I²C pins: SDA=GPIO22, SCL=GPIO23 (configurable via Kconfig).
 * Calls start_periodic_measurement() on the SCD40.
 * The first measurement is ready ~5s after start.
 *
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t scd40_init(void);

/**
 * @brief Read one measurement from SCD40.
 *
 * Calls get_data_ready_flag(), then read_measurement() on the SCD40.
 * If data is not ready, returns ESP_ERR_NOT_FOUND (caller should retry later).
 * If data_ready_status returns 0 for SCD40_MAX_DATA_NOT_READY_CYCLES (3)
 * consecutive calls, the sensor is considered stalled and ESP_ERR_TIMEOUT is
 * returned — this triggers the scd40_reinit() recovery path in sensor_task.
 *
 * @param[out] reading  Populated on success.
 * @return ESP_OK on success.
 * @return ESP_ERR_NOT_FOUND if data is not yet ready (transient; retry).
 * @return ESP_ERR_TIMEOUT if data_ready_status has been 0 for 3 consecutive
 *         cycles — sensor is stalled; caller should invoke scd40_reinit().
 * @return Other ESP_ERR_* codes for I²C bus failures.
 */
esp_err_t scd40_read_measurement(hivekit_scd40_reading_t *reading);

/**
 * @brief Re-initialise the SCD40 after repeated I²C errors (Bug 2 fix).
 *
 * Tears down the existing I²C bus handles, waits briefly, then calls
 * scd40_init() again. Designed to recover from a hung I²C bus where the
 * periodic measurement loop has stalled.
 *
 * @return ESP_OK on success.
 */
esp_err_t scd40_reinit(void);

#ifdef __cplusplus
}
#endif
