/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * sensor_scd40.c — SCD40/SCD41 I²C sensor wrapper
 *
 * This file wraps the Sensirion SCD4x I²C driver.
 *
 * SCD4x I²C protocol summary:
 *   - 7-bit address: 0x62
 *   - start_periodic_measurement: cmd 0x21B1
 *   - get_data_ready_status:      cmd 0xE4B8 → uint16 status (bits 0-10 = count)
 *   - read_measurement:           cmd 0xEC05 → 9 bytes (CO2 u16, T u16, RH u16 + CRCs)
 *   - stop_periodic_measurement:  cmd 0x3F86
 *
 * Reference: https://github.com/Sensirion/embedded-i2c-scd4x (BSD-3-Clause)
 * Espressif component registry: https://components.espressif.com/components?q=scd4x
 *
 * TODO (Phase 1): If sensirion/scd4x_i2c is available via IDF Component Manager,
 * uncomment the #include and use the driver's API directly. The stub below
 * implements the minimal I²C transactions to keep the project buildable without
 * the component being installed.
 *
 * When Sensirion's driver is available:
 *   scd4x_init() → uses scd4x_wake_up(), scd4x_start_periodic_measurement()
 *   scd40_read_measurement() → uses scd4x_get_data_ready_flag() + scd4x_read_measurement()
 *
 * The conversion formulas (from Sensirion datasheet):
 *   temperature_c = -45 + 175 * (raw_temp / 65535.0)
 *   humidity_pct  = 100 * (raw_rh / 65535.0)
 *   co2_ppm       = raw_co2  (direct uint16 in ppm)
 */

#include "sensor_scd40.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

/* TODO: uncomment when component is confirmed available:
 * #include "scd4x_i2c.h"
 */

static const char *TAG = "sensor_scd40";

#define SCD40_I2C_ADDR          0x62
#define SCD40_I2C_FREQ_HZ       100000

#define SCD40_CMD_START_MEAS    0x21B1
#define SCD40_CMD_DATA_READY    0xE4B8
#define SCD40_CMD_READ_MEAS     0xEC05

#ifndef CONFIG_HIVEKIT_SCD40_SDA_GPIO
#define CONFIG_HIVEKIT_SCD40_SDA_GPIO 22
#endif
#ifndef CONFIG_HIVEKIT_SCD40_SCL_GPIO
#define CONFIG_HIVEKIT_SCD40_SCL_GPIO 23
#endif

#define SCD40_SDA_GPIO          CONFIG_HIVEKIT_SCD40_SDA_GPIO
#define SCD40_SCL_GPIO          CONFIG_HIVEKIT_SCD40_SCL_GPIO

static i2c_master_bus_handle_t s_bus_handle  = NULL;
static i2c_master_dev_handle_t s_dev_handle  = NULL;

/* ── CRC-8 (Sensirion) ────────────────────────────────────────────────────── */

static uint8_t scd40_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* ── Low-level I²C helpers ────────────────────────────────────────────────── */

/**
 * @brief Send a 16-bit command to the SCD40.
 */
static esp_err_t scd40_send_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return i2c_master_transmit(s_dev_handle, buf, sizeof(buf), 100);
}

/**
 * @brief Send command then read N bytes response (with CRC checking).
 *
 * SCD40 response format: [data_hi, data_lo, crc] per word.
 * This function reads (n_words * 3) bytes and returns (n_words * 2) validated bytes.
 *
 * @param cmd       Command to send
 * @param out       Buffer for word data (n_words * 2 bytes)
 * @param n_words   Number of 16-bit words to read
 * @param delay_ms  Delay between write and read (command processing time)
 */
static esp_err_t scd40_read(uint16_t cmd, uint16_t *out, size_t n_words, uint32_t delay_ms)
{
    uint8_t cmd_buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    uint8_t rx[n_words * 3];

    esp_err_t err = i2c_master_transmit(s_dev_handle, cmd_buf, 2, 100);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    err = i2c_master_receive(s_dev_handle, rx, sizeof(rx), 100);
    if (err != ESP_OK) return err;

    for (size_t i = 0; i < n_words; i++) {
        uint8_t crc = scd40_crc8(&rx[i * 3], 2);
        if (crc != rx[i * 3 + 2]) {
            ESP_LOGE(TAG, "CRC mismatch word %zu: expected 0x%02x got 0x%02x",
                     i, crc, rx[i * 3 + 2]);
            return ESP_ERR_INVALID_CRC;
        }
        out[i] = ((uint16_t)rx[i * 3] << 8) | rx[i * 3 + 1];
    }
    return ESP_OK;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t scd40_init(void)
{
    /* Init I²C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port      = I2C_NUM_0,
        .sda_io_num    = SCD40_SDA_GPIO,
        .scl_io_num    = SCD40_SCL_GPIO,
        .clk_source    = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus_handle),
                        TAG, "I²C bus init failed");

    /* Add SCD40 device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SCD40_I2C_ADDR,
        .scl_speed_hz    = SCD40_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle),
                        TAG, "SCD40 device add failed");

    /* Stop any ongoing measurement (safe to send even if idle) */
    (void)scd40_send_cmd(0x3F86); /* stop_periodic_measurement */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Start periodic measurement (runs every 5 seconds on SCD40) */
    ESP_RETURN_ON_ERROR(scd40_send_cmd(SCD40_CMD_START_MEAS),
                        TAG, "SCD40 start_periodic_measurement failed");

    /* Wait for first measurement (SCD40: ~5s, SCD41: ~5s) */
    ESP_LOGI(TAG, "SCD40 starting — waiting 5s for first measurement...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "SCD40 ready (SDA=GPIO%d, SCL=GPIO%d)", SCD40_SDA_GPIO, SCD40_SCL_GPIO);
    return ESP_OK;
}

esp_err_t scd40_reinit(void)
{
    ESP_LOGW(TAG, "scd40_reinit: tearing down I²C bus and reinitialising (Bug 2 — I²C recovery)");

    /* Try a graceful stop first; if the bus is hung this may fail — that's OK. */
    if (s_dev_handle) {
        (void)scd40_send_cmd(0x3F86); /* stop_periodic_measurement */
        vTaskDelay(pdMS_TO_TICKS(200));
        (void)i2c_master_bus_rm_device(s_dev_handle);
        s_dev_handle = NULL;
    }

    if (s_bus_handle) {
        (void)i2c_del_master_bus(s_bus_handle);
        s_bus_handle = NULL;
    }

    /* Brief pause to let the I²C lines settle. */
    vTaskDelay(pdMS_TO_TICKS(500));

    return scd40_init();
}

esp_err_t scd40_read_measurement(hivekit_scd40_reading_t *reading)
{
    ESP_RETURN_ON_FALSE(reading, ESP_ERR_INVALID_ARG, TAG, "NULL reading");

    /* Check data ready status */
    uint16_t status;
    ESP_RETURN_ON_ERROR(scd40_read(SCD40_CMD_DATA_READY, &status, 1, 1),
                        TAG, "data_ready_status failed");

    /* Bits 10:0 are the count (>= 1 means data available) */
    if ((status & 0x07FF) == 0) {
        return ESP_ERR_NOT_FOUND; /* Data not ready yet */
    }

    /* Read measurement: 3 words = [co2_raw, temp_raw, rh_raw] */
    uint16_t words[3];
    ESP_RETURN_ON_ERROR(scd40_read(SCD40_CMD_READ_MEAS, words, 3, 1),
                        TAG, "read_measurement failed");

    /* Convert (Sensirion SCD4x datasheet, rev 1.1):
     *   CO2  ppm = raw (direct uint16)
     *   T    °C  = -45 + 175 * (raw / 65535.0)
     *   RH   %   = 100 * (raw / 65535.0)
     */
    reading->co2_ppm       = (float)words[0];
    reading->temperature_c = -45.0f + 175.0f * ((float)words[1] / 65535.0f);
    reading->humidity_pct  = 100.0f * ((float)words[2] / 65535.0f);

    return ESP_OK;
}
