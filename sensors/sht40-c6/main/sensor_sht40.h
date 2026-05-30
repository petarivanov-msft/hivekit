/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * sensor_sht40.h — SHT40 sensor driver interface
 *
 * ⚠️  UNTESTED — community verification welcome.
 *     Flash and report results: https://github.com/petarivanov-msft/hivekit/issues
 *
 * I²C addresses (SHT40 datasheet §2.1):
 *   0x44 — default address (SHT40-AD1B / most breakout boards)
 *   0x45 — alternate (SHT40-BD1B)
 *   0x46 — alternate (SHT40-BD1B variant)
 *   This driver targets 0x44.
 *
 * I²C pins (XIAO ESP32-C6): SDA=GPIO22, SCL=GPIO23
 *   (same bus as SCD40/BME280; only one sensor per board at a time)
 *
 * SHT40 datasheet:
 *   https://sensirion.com/media/documents/33C09C07/626C2DBA/Datasheet_SHT4x.pdf
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Temperature and humidity measurement from the SHT40.
 */
typedef struct sht40_reading_s {
    float temperature_c; /*!< Temperature in degrees Celsius (-40 to +125) */
    float humidity_pct;  /*!< Relative humidity in percent (0–100, clamped) */
} sht40_reading_t;

/**
 * @brief Initialise the I²C bus, verify SHT40 presence, and reset the sensor.
 *
 * Issues a soft-reset command (0x94) and verifies the sensor responds on the
 * expected I²C address. Does NOT perform a measurement.
 *
 * I²C address: CONFIG_HIVEKIT_SHT40_I2C_ADDR (default 0x44).
 * Source: SHT40 datasheet §2.1 (I²C addresses), §2.3 (soft reset 0x94)
 * URL: https://sensirion.com/media/documents/33C09C07/626C2DBA/Datasheet_SHT4x.pdf
 *
 * @return ESP_OK on success.
 *         ESP_ERR_NOT_FOUND if no ACK received (check wiring + address).
 */
esp_err_t sht40_init(void);

/**
 * @brief Trigger a high-precision measurement and read T + RH.
 *
 * Sends command 0xFD (high precision, no heater), waits the maximum
 * measurement time (~8.2 ms per SHT40 datasheet §2.4 Table 4), then
 * reads 6 bytes: T_MSB, T_LSB, CRC_T, RH_MSB, RH_LSB, CRC_RH.
 *
 * CRC-8 is verified for both words (polynomial 0x31, initial 0xFF).
 * Source: SHT40 datasheet §2.4 (command 0xFD), §4.4 (CRC-8)
 * URL: https://sensirion.com/media/documents/33C09C07/626C2DBA/Datasheet_SHT4x.pdf
 *
 * Conversion formulas (datasheet §4.6):
 *   T  = -45 + 175 × (S_T  / 65535)     [°C]
 *   RH = -6  + 125 × (S_RH / 65535)     [%]  → clamped to [0, 100]
 *
 * CRC test vector: crc8([0xBE, 0xEF]) == 0x92 (datasheet §4.4)
 *
 * @param[out] reading Populated on success.
 * @return ESP_OK on success.
 *         ESP_ERR_INVALID_CRC if either T or RH CRC fails.
 *         ESP_ERR_TIMEOUT if the measurement read stalls.
 *         Other codes on I²C error.
 */
esp_err_t sht40_read_high_precision(sht40_reading_t *reading);

#ifdef __cplusplus
}
#endif
