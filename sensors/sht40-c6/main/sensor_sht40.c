/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * sensor_sht40.c — SHT40 I²C sensor driver
 *
 * ⚠️  UNTESTED — community verification welcome.
 *     Flash and report results: https://github.com/petarivanov-msft/hivekit/issues
 *
 * ═══════════════════════════════════════════════════════════════
 *  DRIVER CHOICE: hand-written (managed component not suitable)
 * ═══════════════════════════════════════════════════════════════
 * Rationale:
 *   - The Espressif component registry namespace "sensirion" does not exist
 *     (verified: GET https://components.espressif.com/components/sensirion/sht4x_i2c
 *     returned 404 on 2026-05-30). No official Sensirion managed component
 *     is available through the Espressif registry.
 *   - Sensirion's own embedded-sht library (BSD-3-Clause,
 *     https://github.com/Sensirion/embedded-sht) targets generic C without
 *     ESP-IDF integration and requires a HAL shim.
 *   - A minimal hand-written driver using the ESP-IDF i2c_master.h API
 *     (the same API used by SCD40 and BME280) is the cleanest path:
 *     fully self-contained, ≤200 lines, no external dependency.
 *   - The SHT40 protocol is extremely simple: send 1-byte command, wait,
 *     read 6 bytes. No calibration registers, no trim data, no complex
 *     oversampling config — far simpler than BME280.
 *   Source for API decision: SHT40 datasheet §2.4
 *   URL: https://sensirion.com/media/documents/33C09C07/626C2DBA/Datasheet_SHT4x.pdf
 *
 * ═══════════════════════════════════════════════════════════════
 *  COMMAND SET (SHT40 datasheet §2.4 Table 4)
 * ═══════════════════════════════════════════════════════════════
 *  Command  Precision  Max meas. time (typ.)
 *  0xFD     High       8.2 ms (6.9 ms typ.)
 *  0xF6     Medium     4.5 ms
 *  0xE0     Low        1.7 ms
 *  0x94     Soft reset  —  (1 ms)
 *  0x89     Serial number read  —
 *
 *  We use 0xFD (high precision) and wait 10 ms (comfortable margin over 8.2 ms).
 *
 * ═══════════════════════════════════════════════════════════════
 *  DATA FORMAT (SHT40 datasheet §4.5)
 * ═══════════════════════════════════════════════════════════════
 *  6 bytes returned after a measurement command:
 *    Byte 0  T_MSB     — temperature raw word high byte
 *    Byte 1  T_LSB     — temperature raw word low byte
 *    Byte 2  CRC_T     — CRC-8 over bytes [0,1]
 *    Byte 3  RH_MSB    — humidity raw word high byte
 *    Byte 4  RH_LSB    — humidity raw word low byte
 *    Byte 5  CRC_RH    — CRC-8 over bytes [3,4]
 *
 * ═══════════════════════════════════════════════════════════════
 *  CONVERSION FORMULAS (SHT40 datasheet §4.6)
 * ═══════════════════════════════════════════════════════════════
 *  S_T  = (T_MSB  << 8) | T_LSB
 *  S_RH = (RH_MSB << 8) | RH_LSB
 *  T    = -45 + 175 × (S_T  / 65535)   [°C]
 *  RH   = -6  + 125 × (S_RH / 65535)   [%]  → clamp to [0, 100]
 *
 * ═══════════════════════════════════════════════════════════════
 *  CRC-8 (SHT40 datasheet §4.4)
 * ═══════════════════════════════════════════════════════════════
 *  Polynomial : 0x31  (x⁸ + x⁵ + x⁴ + 1)
 *  Initial value: 0xFF
 *  Computed over each 2-byte data word independently.
 *  Test vector: crc8([0xBE, 0xEF]) == 0x92  (verified)
 *
 * ═══════════════════════════════════════════════════════════════
 *  ZCL CLUSTER NOTES
 * ═══════════════════════════════════════════════════════════════
 *  Temperature: cluster 0x0402, MeasuredValue int16 = °C × 100
 *  Humidity:    cluster 0x0405, MeasuredValue uint16 = % × 100
 *  Source: ZCL HA spec (ZigBee Cluster Library), §4.4/4.5
 */

#include "sensor_sht40.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor_sht40";

/* ── I²C configuration ────────────────────────────────────────────────────── */

#ifndef CONFIG_HIVEKIT_SHT40_SDA_GPIO
#define CONFIG_HIVEKIT_SHT40_SDA_GPIO 22
#endif
#ifndef CONFIG_HIVEKIT_SHT40_SCL_GPIO
#define CONFIG_HIVEKIT_SHT40_SCL_GPIO 23
#endif
#ifndef CONFIG_HIVEKIT_SHT40_I2C_ADDR
#define CONFIG_HIVEKIT_SHT40_I2C_ADDR 0x44
#endif

/* SHT40 supports up to 1 MHz I²C; use 400 kHz (fast-mode) for compatibility.
 * Source: SHT40 datasheet §2.1 */
#define SHT40_I2C_FREQ_HZ 400000

/* ── Command bytes (SHT40 datasheet §2.4 Table 4) ─────────────────────────── */

/* High precision measurement: T + RH, no heater, max 8.2 ms */
#define SHT40_CMD_MEASURE_HIGH   0xFD

/* Soft reset — restores sensor to default state; takes ~1 ms.
 * Source: SHT40 datasheet §2.3 */
#define SHT40_CMD_SOFT_RESET     0x94

/* Measurement wait: 10 ms gives a comfortable margin over the 8.2 ms max.
 * Source: SHT40 datasheet §2.4, Table 4 */
#define SHT40_MEASURE_WAIT_MS    10

/* Reset wait: datasheet §2.3 says 1 ms; 5 ms for safety. */
#define SHT40_RESET_WAIT_MS      5

/* I²C timeout per transaction, in ms */
#define SHT40_I2C_TIMEOUT_MS     50

/* ── Module-level state ───────────────────────────────────────────────────── */

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

/* ── CRC-8 (SHT40 datasheet §4.4) ────────────────────────────────────────── */

/**
 * @brief Compute CRC-8 over a 2-byte word.
 *
 * Polynomial: 0x31 (x⁸ + x⁵ + x⁴ + 1)
 * Initial value: 0xFF
 * Applied one bit at a time, MSB first.
 *
 * Test vector from SHT40 datasheet §4.4:
 *   crc8([0xBE, 0xEF]) == 0x92  ✓  (verified independently)
 *
 * Source: SHT40 datasheet §4.4 "Checksum Calculation"
 * URL: https://sensirion.com/media/documents/33C09C07/626C2DBA/Datasheet_SHT4x.pdf
 *
 * @param data  Pointer to 2 bytes.
 * @return CRC-8 checksum byte.
 */
static uint8_t sht40_crc8(const uint8_t *data)
{
    uint8_t crc = 0xFF; /* Initial value — SHT40 datasheet §4.4 */

    for (int i = 0; i < 2; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31); /* Polynomial 0x31 */
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ── Low-level I²C helpers ────────────────────────────────────────────────── */

/**
 * @brief Send a single-byte command to the SHT40.
 */
static esp_err_t sht40_send_cmd(uint8_t cmd)
{
    return i2c_master_transmit(s_dev, &cmd, 1, SHT40_I2C_TIMEOUT_MS);
}

/**
 * @brief Read N bytes from the SHT40 (no preceding register write).
 *
 * The SHT40 does not use register-addressed reads: after sending a command
 * and waiting for measurement, the host initiates a plain I²C read to
 * collect the response bytes.
 * Source: SHT40 datasheet §2.4
 */
static esp_err_t sht40_read_bytes(uint8_t *out, size_t len)
{
    return i2c_master_receive(s_dev, out, len, SHT40_I2C_TIMEOUT_MS);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t sht40_init(void)
{
    /* Init I²C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port               = I2C_NUM_0,
        .sda_io_num             = CONFIG_HIVEKIT_SHT40_SDA_GPIO,
        .scl_io_num             = CONFIG_HIVEKIT_SHT40_SCL_GPIO,
        .clk_source             = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt      = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus),
                        TAG, "I²C bus init failed");

    /* Add SHT40 device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CONFIG_HIVEKIT_SHT40_I2C_ADDR,
        .scl_speed_hz    = SHT40_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev),
                        TAG, "SHT40 device add failed");

    /* Soft reset to ensure clean state.
     * Source: SHT40 datasheet §2.3 — soft reset command 0x94.
     * During power-up the sensor may be in an undefined state; soft reset
     * brings it to a known default configuration. */
    esp_err_t err = sht40_send_cmd(SHT40_CMD_SOFT_RESET);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SHT40 soft reset failed: %s — check wiring and I²C address 0x%02x",
                 esp_err_to_name(err), CONFIG_HIVEKIT_SHT40_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    vTaskDelay(pdMS_TO_TICKS(SHT40_RESET_WAIT_MS));

    ESP_LOGI(TAG, "SHT40 detected and reset (addr=0x%02x, SDA=GPIO%d, SCL=GPIO%d)",
             CONFIG_HIVEKIT_SHT40_I2C_ADDR,
             CONFIG_HIVEKIT_SHT40_SDA_GPIO,
             CONFIG_HIVEKIT_SHT40_SCL_GPIO);

    return ESP_OK;
}

esp_err_t sht40_read_high_precision(sht40_reading_t *reading)
{
    ESP_RETURN_ON_FALSE(reading, ESP_ERR_INVALID_ARG, TAG, "NULL reading pointer");

    /* Send high-precision measurement command.
     * 0xFD: measure T + RH, high precision, no heater.
     * Source: SHT40 datasheet §2.4, Table 4 */
    ESP_RETURN_ON_ERROR(sht40_send_cmd(SHT40_CMD_MEASURE_HIGH),
                        TAG, "Measurement command failed");

    /* Wait for measurement to complete.
     * Max conversion time at high precision: 8.2 ms.
     * We wait 10 ms for a comfortable margin.
     * Source: SHT40 datasheet §2.4, Table 4 */
    vTaskDelay(pdMS_TO_TICKS(SHT40_MEASURE_WAIT_MS));

    /* Read 6 response bytes: T_MSB, T_LSB, CRC_T, RH_MSB, RH_LSB, CRC_RH.
     * Source: SHT40 datasheet §4.5 "Data Readout" */
    uint8_t buf[6];
    ESP_RETURN_ON_ERROR(sht40_read_bytes(buf, sizeof(buf)),
                        TAG, "Response read failed");

    /* Verify CRC for temperature word (bytes 0 and 1).
     * Source: SHT40 datasheet §4.4 */
    uint8_t crc_t_calc = sht40_crc8(&buf[0]);
    if (crc_t_calc != buf[2]) {
        ESP_LOGE(TAG, "Temperature CRC mismatch: calculated 0x%02x, received 0x%02x",
                 crc_t_calc, buf[2]);
        return ESP_ERR_INVALID_CRC;
    }

    /* Verify CRC for humidity word (bytes 3 and 4).
     * Source: SHT40 datasheet §4.4 */
    uint8_t crc_rh_calc = sht40_crc8(&buf[3]);
    if (crc_rh_calc != buf[5]) {
        ESP_LOGE(TAG, "Humidity CRC mismatch: calculated 0x%02x, received 0x%02x",
                 crc_rh_calc, buf[5]);
        return ESP_ERR_INVALID_CRC;
    }

    /* Reconstruct raw 16-bit words.
     * Source: SHT40 datasheet §4.5 */
    uint16_t s_t  = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t s_rh = ((uint16_t)buf[3] << 8) | buf[4];

    /* Convert to physical values.
     * Source: SHT40 datasheet §4.6 "Conversion of Signal Output"
     *   T  [°C] = -45 + 175 × (S_T  / 65535)
     *   RH [%]  = -6  + 125 × (S_RH / 65535)  → clamped to [0, 100] */
    float temp_c  = -45.0f + 175.0f * ((float)s_t  / 65535.0f);
    float humi_pct = -6.0f + 125.0f * ((float)s_rh / 65535.0f);

    /* Clamp humidity to [0, 100] — per datasheet recommendation §4.6 */
    if (humi_pct > 100.0f) humi_pct = 100.0f;
    if (humi_pct <   0.0f) humi_pct =   0.0f;

    reading->temperature_c = temp_c;
    reading->humidity_pct  = humi_pct;

    ESP_LOGI(TAG, "SHT40: T=%.2f°C  RH=%.1f%%  (raw: S_T=0x%04x S_RH=0x%04x)",
             reading->temperature_c, reading->humidity_pct, s_t, s_rh);

    return ESP_OK;
}
