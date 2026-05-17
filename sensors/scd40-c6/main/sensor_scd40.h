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
 *
 * @param[out] reading  Populated on success.
 * @return ESP_OK on success.
 */
esp_err_t scd40_read_measurement(hivekit_scd40_reading_t *reading);

#ifdef __cplusplus
}
#endif
