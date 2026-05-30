/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * sensor_bme280.c — BME280 I²C sensor driver
 *
 * ═══════════════════════════════════════════════════════════════
 *  DRIVER CHOICE: hand-written (no external managed component)
 * ═══════════════════════════════════════════════════════════════
 * Rationale:
 *   - Bosch SensorAPI (BSD-3-Clause, https://github.com/boschsensortec/BME280_SensorAPI)
 *     requires a platform HAL shim (~60 lines) AND is not in the Espressif
 *     component registry, so it would need manual vendoring.
 *   - esp-idf-lib/bmp280 (Apache-2) uses the older esp-idf i2c driver (i2c.h),
 *     not the new i2c_master.h API that IDF v5.5+ prefers and that our SCD40
 *     driver already uses.
 *   - A minimal hand-written driver using i2c_master.h keeps the build
 *     self-contained, matches the SCD40 pattern exactly, and stays under
 *     300 lines (driver only, not counting this comment block).
 *   - Calibration compensation algorithm is ported faithfully from the
 *     Bosch BME280 datasheet §4.2.3 "Compensation formulas in double precision"
 *     (rev 1.14, 2018). We use the double-precision path for accuracy; the
 *     integer-only path from Appendix A would save ~8KB of RAM but introduces
 *     fixed-point truncation errors at extreme temperatures.
 *
 * ═══════════════════════════════════════════════════════════════
 *  REGISTER MAP REFERENCE (BME280 datasheet §5.3)
 * ═══════════════════════════════════════════════════════════════
 *  Register   Name            R/W  Description
 *  ─────────  ──────────────  ───  ─────────────────────────────
 *  0xD0       id              R    Chip ID (must be 0x60 for BME280)
 *  0xE0       reset           W    Write 0xB6 to trigger soft reset
 *  0xF2       ctrl_hum        R/W  Humidity oversampling [2:0]
 *  0xF3       status          R    [3]=measuring, [0]=im_update
 *  0xF4       ctrl_meas       R/W  [7:5]=osrs_t, [4:2]=osrs_p, [1:0]=mode
 *  0xF5       config          R/W  [7:5]=t_sb, [4:2]=filter, [0]=spi3w_en
 *  0xF7–0xF9  press_msb/lsb/xlsb  R  Raw pressure 20-bit [19:4], [3:0]
 *  0xFA–0xFC  temp_msb/lsb/xlsb   R  Raw temperature 20-bit
 *  0xFD–0xFE  hum_msb/lsb         R  Raw humidity 16-bit
 *
 *  Calibration registers:
 *  0x88–0x9F  Trim T1–T3, P1–P9 (little-endian, some signed int16)
 *  0xA1       Trim H1 (uint8)
 *  0xE1–0xE7  Trim H2–H6 (mixed endian, see parse_calib_hum())
 *
 * ═══════════════════════════════════════════════════════════════
 *  OPERATING MODE
 * ═══════════════════════════════════════════════════════════════
 *  We use FORCED mode per Bosch recommendation §3.5.1 "Weather monitoring":
 *  - Write mode bits = 0b01 to ctrl_meas → sensor wakes, measures, sleeps
 *  - Poll status register 0xF3 bit [3] until measuring=0
 *  - Read data registers 0xF7–0xFE in one burst
 *  - Wait HIVEKIT_BME280_MEASUREMENT_INTERVAL_MS before next cycle
 *
 *  Recommended settings for indoor environmental sensing:
 *  - Temperature: osrs_t = 010 (×2)
 *  - Humidity:    osrs_h = 001 (×1)
 *  - Pressure:    osrs_p = 101 (×16)
 *  - IIR filter:  filter = 100 (coefficient 16)
 *  - Standby:     N/A in forced mode
 *  Source: BME280 datasheet Table 7, §3.5.1
 *
 * ═══════════════════════════════════════════════════════════════
 *  COMPENSATION ALGORITHM
 * ═══════════════════════════════════════════════════════════════
 *  Ported from BME280 datasheet §4.2.3 (double-precision C code).
 *  The algorithm uses 12–18 trim parameters stored in sensor NVM.
 *  Temperature compensation produces t_fine, a shared intermediate
 *  value reused by pressure and humidity compensation.
 *  Source: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
 *
 * ═══════════════════════════════════════════════════════════════
 *  ZCL CLUSTER NOTES
 * ═══════════════════════════════════════════════════════════════
 *  Temperature: cluster 0x0402, MeasuredValue int16 = °C × 100
 *  Humidity:    cluster 0x0405, MeasuredValue uint16 = % × 100
 *  Pressure:    cluster 0x0403, MeasuredValue int16 = hPa (integer)
 *    Z2M uses MeasuredValue (int16 hPa) for pressure — the simplest path.
 *    ScaledValue (int16 × 10^Scale) is optional; we omit it to keep parity
 *    with how Z2M reads known BME280 devices (e.g. iCasa IQKL-3210-GLR02).
 *    The resolution loss from rounding to integer hPa is ±0.5 hPa, which
 *    is within the BME280's absolute accuracy (±1 hPa at 0–65°C).
 *  Source for cluster IDs: ZCL HA spec (ZigBee Cluster Library), §4.4/4.5/4.7
 */

#include "sensor_bme280.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor_bme280";

/* ── I²C configuration ────────────────────────────────────────────────────── */

#ifndef CONFIG_HIVEKIT_BME280_SDA_GPIO
#define CONFIG_HIVEKIT_BME280_SDA_GPIO 22
#endif
#ifndef CONFIG_HIVEKIT_BME280_SCL_GPIO
#define CONFIG_HIVEKIT_BME280_SCL_GPIO 23
#endif
#ifndef CONFIG_HIVEKIT_BME280_I2C_ADDR
#define CONFIG_HIVEKIT_BME280_I2C_ADDR 0x76
#endif

#define BME280_I2C_FREQ_HZ       400000  /* 400 kHz fast-mode; BME280 supports up to 3.4 MHz */

/* ── Register addresses (BME280 datasheet §5.3) ────────────────────────────── */

#define BME280_REG_ID            0xD0
#define BME280_REG_RESET         0xE0
#define BME280_REG_CTRL_HUM      0xF2
#define BME280_REG_STATUS        0xF3
#define BME280_REG_CTRL_MEAS     0xF4
#define BME280_REG_CONFIG        0xF5
#define BME280_REG_PRESS_MSB     0xF7  /* Burst read 0xF7–0xFE = 8 bytes: P×3, T×3, H×2 */
#define BME280_REG_CALIB_00      0x88  /* Trim T1..P9 (26 bytes, little-endian) */
#define BME280_REG_CALIB_H1      0xA1  /* Trim H1 (1 byte) */
#define BME280_REG_CALIB_H2LSB   0xE1  /* Trim H2..H6 (7 bytes, mixed endian) */

#define BME280_CHIP_ID           0x60  /* Expected chip ID for BME280 (vs 0x58 for BMP280) */
#define BME280_RESET_VALUE       0xB6  /* Soft-reset trigger value */

/* ── Control register values ──────────────────────────────────────────────── */

/* ctrl_hum (0xF2): humidity oversampling × 1 = 0b001 */
#define BME280_CTRL_HUM_OSRS_H1  0x01

/* ctrl_meas (0xF4): osrs_t=010 (×2), osrs_p=101 (×16), mode=01 (forced) */
/* bits: [7:5]=010, [4:2]=101, [1:0]=01 → 0b01001101 = 0x4D (force) */
/* For sleep mode (bits[1:0]=00): 0b01001100 = 0x4C */
#define BME280_CTRL_MEAS_FORCED  0x4D
#define BME280_CTRL_MEAS_SLEEP   0x4C

/* config (0xF5): t_sb=000 (not used in forced mode), filter=100 (coeff 16), spi3w=0 */
/* bits: [7:5]=000, [4:2]=100, [1:0]=00 → 0b00010000 = 0x10 */
#define BME280_CONFIG_FILTER16   0x10

/* Status register measurement flag */
#define BME280_STATUS_MEASURING  (1 << 3)

/* ── Calibration trim structure ───────────────────────────────────────────── */

typedef struct {
    /* Temperature (Bosch datasheet Table 17): T1 unsigned, T2/T3 signed */
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;

    /* Pressure (Table 18): P1 unsigned, P2..P9 signed */
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;

    /* Humidity (Table 19): all signed except H1, H3 */
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;  /* 12-bit, mixed layout: reg[E3][7:4] | reg[E4][7:0] */
    int16_t  dig_H5;  /* 12-bit: reg[E4][3:0] | reg[E5][7:0] */
    int8_t   dig_H6;
} bme280_calib_t;

/* ── Module-level state ───────────────────────────────────────────────────── */

static i2c_master_bus_handle_t s_bus   = NULL;
static i2c_master_dev_handle_t s_dev   = NULL;
static bme280_calib_t          s_calib = {0};

/* ── Low-level I²C helpers ────────────────────────────────────────────────── */

/**
 * @brief Write one byte to a register.
 */
static esp_err_t bme280_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, 2, 50);
}

/**
 * @brief Read N bytes starting from a register address.
 */
static esp_err_t bme280_read_regs(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, 50);
}

/* ── Calibration loading ──────────────────────────────────────────────────── */

/**
 * @brief Parse temperature + pressure trim from registers 0x88–0x9F (26 bytes).
 *
 * Layout (all little-endian, per BME280 datasheet Table 17–18):
 *   [0–1]   dig_T1 uint16
 *   [2–3]   dig_T2 int16
 *   [4–5]   dig_T3 int16
 *   [6–7]   dig_P1 uint16
 *   [8–9]   dig_P2 int16
 *   ... (P3–P9 follow same pattern every 2 bytes)
 *   [24]    dig_H1 uint8 (register 0xA1 = [24+0x88–0x88])
 */
static void parse_calib_tp(const uint8_t *d)
{
    /* Temperature */
    s_calib.dig_T1 = (uint16_t)((d[1] << 8) | d[0]);
    s_calib.dig_T2 = (int16_t) ((d[3] << 8) | d[2]);
    s_calib.dig_T3 = (int16_t) ((d[5] << 8) | d[4]);

    /* Pressure */
    s_calib.dig_P1 = (uint16_t)((d[7]  << 8) | d[6]);
    s_calib.dig_P2 = (int16_t) ((d[9]  << 8) | d[8]);
    s_calib.dig_P3 = (int16_t) ((d[11] << 8) | d[10]);
    s_calib.dig_P4 = (int16_t) ((d[13] << 8) | d[12]);
    s_calib.dig_P5 = (int16_t) ((d[15] << 8) | d[14]);
    s_calib.dig_P6 = (int16_t) ((d[17] << 8) | d[16]);
    s_calib.dig_P7 = (int16_t) ((d[19] << 8) | d[18]);
    s_calib.dig_P8 = (int16_t) ((d[21] << 8) | d[20]);
    s_calib.dig_P9 = (int16_t) ((d[23] << 8) | d[22]);
    /* d[24] = 0xA0 (reserved, skip) */
}

/**
 * @brief Parse humidity trim from registers 0xA1 and 0xE1–0xE7.
 *
 * H2–H6 layout (BME280 datasheet Table 19, §4.2.2):
 *   hd[0]       = 0xE1 = H2 LSB
 *   hd[1]       = 0xE2 = H2 MSB          → dig_H2 = int16(hd[1]<<8 | hd[0])
 *   hd[2]       = 0xE3 = H3 (uint8)
 *   hd[3]       = 0xE4 = H4 MSB [11:4]
 *   hd[4]       = 0xE5 = H4 LSB [3:0] in bits[3:0], H5 bits[7:4] in bits[7:4]
 *   hd[5]       = 0xE6 = H5 MSB [11:4]
 *   hd[6]       = 0xE7 = H6 (int8)
 *
 * dig_H4 = int16(hd[3] << 4 | (hd[4] & 0x0F))   (bits[3:0] of 0xE5)
 * dig_H5 = int16((hd[4] >> 4) | (hd[5] << 4))   (bits[7:4] of 0xE5 + 0xE6)
 *
 * Note: dig_H4 and dig_H5 are 12-bit 2's complement values stored as int16.
 * The datasheet uses bit-field packing; the layout above matches the reference
 * C code in Bosch BME280_SensorAPI (bme280.c, parse_humidity_calib_data()).
 */
static void parse_calib_hum(uint8_t h1, const uint8_t *hd)
{
    s_calib.dig_H1 = h1;
    s_calib.dig_H2 = (int16_t)((hd[1] << 8) | hd[0]);
    s_calib.dig_H3 = hd[2];
    s_calib.dig_H4 = (int16_t)(((int16_t)hd[3] << 4) | (hd[4] & 0x0F));
    s_calib.dig_H5 = (int16_t)(((int16_t)hd[5] << 4) | (hd[4] >> 4));
    s_calib.dig_H6 = (int8_t)hd[6];
}

/* ── Compensation formulas (double-precision, BME280 datasheet §4.2.3) ─────── */

/**
 * @brief Compensate raw temperature ADC value.
 *
 * @param adc_T   20-bit raw temperature from registers 0xFA–0xFC
 * @param t_fine  [out] Shared intermediate for pressure/humidity compensation
 * @return Temperature in °C (double)
 *
 * Source: BME280 datasheet §4.2.3, "Returns temperature in DegC, double precision"
 */
static double compensate_temperature(int32_t adc_T, double *t_fine)
{
    double var1, var2, T;
    var1 = ((double)adc_T / 16384.0 - (double)s_calib.dig_T1 / 1024.0)
           * (double)s_calib.dig_T2;
    var2 = (((double)adc_T / 131072.0 - (double)s_calib.dig_T1 / 8192.0)
            * ((double)adc_T / 131072.0 - (double)s_calib.dig_T1 / 8192.0))
           * (double)s_calib.dig_T3;
    *t_fine = var1 + var2;
    T = *t_fine / 5120.0;
    return T;
}

/**
 * @brief Compensate raw pressure ADC value.
 *
 * @param adc_P   20-bit raw pressure from registers 0xF7–0xF9
 * @param t_fine  Intermediate from compensate_temperature()
 * @return Pressure in hPa (= Pa / 100). Returns 0.0 on division guard.
 *
 * Source: BME280 datasheet §4.2.3, "Returns pressure in hPa as double"
 * The datasheet returns Pa; we divide by 100 to get hPa.
 */
static double compensate_pressure(int32_t adc_P, double t_fine)
{
    double var1, var2, p;
    var1 = (t_fine / 2.0) - 64000.0;
    var2 = var1 * var1 * (double)s_calib.dig_P6 / 32768.0;
    var2 = var2 + var1 * (double)s_calib.dig_P5 * 2.0;
    var2 = (var2 / 4.0) + ((double)s_calib.dig_P4 * 65536.0);
    var1 = ((double)s_calib.dig_P3 * var1 * var1 / 524288.0
            + (double)s_calib.dig_P2 * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * (double)s_calib.dig_P1;

    /* Guard against division by zero (implausible calibration data) */
    if (var1 == 0.0) {
        ESP_LOGW(TAG, "Pressure compensation: P1 zero guard triggered");
        return 0.0;
    }

    p = 1048576.0 - (double)adc_P;
    p = (p - var2 / 4096.0) * 6250.0 / var1;
    var1 = (double)s_calib.dig_P9 * p * p / 2147483648.0;
    var2 = p * (double)s_calib.dig_P8 / 32768.0;
    p = p + (var1 + var2 + (double)s_calib.dig_P7) / 16.0;

    return p / 100.0; /* Pa → hPa */
}

/**
 * @brief Compensate raw humidity ADC value.
 *
 * @param adc_H   16-bit raw humidity from registers 0xFD–0xFE
 * @param t_fine  Intermediate from compensate_temperature()
 * @return Relative humidity in % (0.0–100.0, clamped)
 *
 * Source: BME280 datasheet §4.2.3, "Returns humidity in %rH as double"
 */
static double compensate_humidity(int32_t adc_H, double t_fine)
{
    double var_H;
    var_H = t_fine - 76800.0;
    var_H = (adc_H - ((double)s_calib.dig_H4 * 64.0
                       + (double)s_calib.dig_H5 / 16384.0 * var_H))
            * ((double)s_calib.dig_H2 / 65536.0
               * (1.0 + (double)s_calib.dig_H6 / 67108864.0 * var_H
                  * (1.0 + (double)s_calib.dig_H3 / 67108864.0 * var_H)));
    var_H = var_H * (1.0 - (double)s_calib.dig_H1 * var_H / 524288.0);

    /* Clamp per datasheet recommendation */
    if (var_H > 100.0) var_H = 100.0;
    if (var_H < 0.0)   var_H = 0.0;

    return var_H;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t bme280_init(void)
{
    /* Init I²C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port               = I2C_NUM_0,
        .sda_io_num             = CONFIG_HIVEKIT_BME280_SDA_GPIO,
        .scl_io_num             = CONFIG_HIVEKIT_BME280_SCL_GPIO,
        .clk_source             = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt      = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus),
                        TAG, "I²C bus init failed");

    /* Add BME280 device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CONFIG_HIVEKIT_BME280_I2C_ADDR,
        .scl_speed_hz    = BME280_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev),
                        TAG, "BME280 device add failed");

    /* Soft reset to ensure clean state (datasheet §5.4.2) */
    ESP_RETURN_ON_ERROR(bme280_write_reg(BME280_REG_RESET, BME280_RESET_VALUE),
                        TAG, "Soft reset write failed");
    vTaskDelay(pdMS_TO_TICKS(10)); /* Reset takes ~2ms; 10ms is comfortable */

    /* Verify chip ID (register 0xD0 must return 0x60 for BME280) */
    uint8_t chip_id = 0;
    ESP_RETURN_ON_ERROR(bme280_read_regs(BME280_REG_ID, &chip_id, 1),
                        TAG, "Chip ID read failed");
    if (chip_id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "Unexpected chip ID: 0x%02x (expected 0x60 for BME280, "
                 "0x58 = BMP280). Check wiring and I²C address (SDO pin).", chip_id);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "BME280 detected (chip_id=0x%02x, addr=0x%02x, SDA=GPIO%d, SCL=GPIO%d)",
             chip_id, CONFIG_HIVEKIT_BME280_I2C_ADDR,
             CONFIG_HIVEKIT_BME280_SDA_GPIO, CONFIG_HIVEKIT_BME280_SCL_GPIO);

    /* Read temperature + pressure calibration (registers 0x88–0xA0, 25 bytes) */
    uint8_t calib_tp[25] = {0};
    ESP_RETURN_ON_ERROR(bme280_read_regs(BME280_REG_CALIB_00, calib_tp, sizeof(calib_tp)),
                        TAG, "T/P calibration read failed");
    parse_calib_tp(calib_tp);

    /* Read H1 (register 0xA1) */
    uint8_t h1 = 0;
    ESP_RETURN_ON_ERROR(bme280_read_regs(BME280_REG_CALIB_H1, &h1, 1),
                        TAG, "H1 calibration read failed");

    /* Read H2–H6 (registers 0xE1–0xE7, 7 bytes) */
    uint8_t calib_hum[7] = {0};
    ESP_RETURN_ON_ERROR(bme280_read_regs(BME280_REG_CALIB_H2LSB, calib_hum, sizeof(calib_hum)),
                        TAG, "H2-H6 calibration read failed");
    parse_calib_hum(h1, calib_hum);

    ESP_LOGI(TAG, "Calibration: T1=%u T2=%d T3=%d P1=%u P2=%d H1=%u H2=%d H6=%d",
             s_calib.dig_T1, s_calib.dig_T2, s_calib.dig_T3,
             s_calib.dig_P1, s_calib.dig_P2, s_calib.dig_H1,
             s_calib.dig_H2, s_calib.dig_H6);

    /* Apply recommended indoor config BEFORE first forced-mode trigger */

    /* ctrl_hum MUST be written before ctrl_meas (datasheet §5.4.3):
     * Changes to ctrl_hum only take effect after a subsequent write to ctrl_meas */
    ESP_RETURN_ON_ERROR(bme280_write_reg(BME280_REG_CTRL_HUM, BME280_CTRL_HUM_OSRS_H1),
                        TAG, "ctrl_hum write failed");

    /* config: IIR filter coefficient 16, standby N/A in forced mode */
    ESP_RETURN_ON_ERROR(bme280_write_reg(BME280_REG_CONFIG, BME280_CONFIG_FILTER16),
                        TAG, "config write failed");

    /* ctrl_meas: put device to sleep mode initially (mode bits = 00)
     * This also locks in the osrs_t and osrs_p settings.
     * Actual forced-mode trigger happens in bme280_read_forced(). */
    ESP_RETURN_ON_ERROR(bme280_write_reg(BME280_REG_CTRL_MEAS, BME280_CTRL_MEAS_SLEEP),
                        TAG, "ctrl_meas sleep write failed");

    ESP_LOGI(TAG, "BME280 ready (T×2, H×1, P×16, IIR=16, forced mode)");
    return ESP_OK;
}

esp_err_t bme280_read_forced(bme280_reading_t *reading)
{
    ESP_RETURN_ON_FALSE(reading, ESP_ERR_INVALID_ARG, TAG, "NULL reading pointer");

    /* Trigger a single forced-mode measurement.
     * ctrl_meas bits [1:0] = 01 → forced mode (execute one measurement, then sleep).
     * The ctrl_hum register was set in bme280_init() and persists in hardware.
     * Source: BME280 datasheet §5.4.3, Table 25 */
    ESP_RETURN_ON_ERROR(bme280_write_reg(BME280_REG_CTRL_MEAS, BME280_CTRL_MEAS_FORCED),
                        TAG, "ctrl_meas forced write failed");

    /* Poll until measuring bit clears.
     * Typical measurement time for T×2, H×1, P×16:
     *   t_meas = 1.25 + (2.3×2) + (2.3×16 + 0.575) + (2.3×1 + 0.575) ≈ 44 ms
     * Source: BME280 datasheet §9.1, equation (1)
     * We poll up to 100 ms with 5 ms intervals. */
    const int POLL_MAX_MS = 100;
    const int POLL_STEP_MS = 5;
    int waited = 0;
    uint8_t status = 0;
    while (waited < POLL_MAX_MS) {
        vTaskDelay(pdMS_TO_TICKS(POLL_STEP_MS));
        waited += POLL_STEP_MS;
        esp_err_t err = bme280_read_regs(BME280_REG_STATUS, &status, 1);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Status read error: %s", esp_err_to_name(err));
            continue;
        }
        if (!(status & BME280_STATUS_MEASURING)) {
            break; /* Done measuring */
        }
    }
    if (status & BME280_STATUS_MEASURING) {
        ESP_LOGE(TAG, "BME280 still measuring after %dms — sensor may be faulty", POLL_MAX_MS);
        return ESP_ERR_TIMEOUT;
    }

    /* Burst-read 8 bytes: 0xF7–0xFE (press_msb..hum_lsb) */
    uint8_t raw[8];
    ESP_RETURN_ON_ERROR(bme280_read_regs(BME280_REG_PRESS_MSB, raw, sizeof(raw)),
                        TAG, "Data burst read failed");

    /* Extract 20-bit pressure ADC (bits [19:4] in raw[0..1], bits [3:0] in raw[2][7:4]) */
    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | ((int32_t)raw[2] >> 4);

    /* Extract 20-bit temperature ADC */
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | ((int32_t)raw[5] >> 4);

    /* Extract 16-bit humidity ADC */
    int32_t adc_H = ((int32_t)raw[6] << 8) | raw[7];

    /* Skipped (disabled) sensor returns 0x80000 for 20-bit fields and 0x8000 for humidity.
     * These values indicate the corresponding measurement is not activated.
     * Source: BME280 datasheet §4.1 */
    if (adc_T == 0x80000) {
        ESP_LOGE(TAG, "Temperature ADC skipped — sensor disabled?");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Compensate raw values using calibration trim */
    double t_fine = 0.0;
    double temp_c  = compensate_temperature(adc_T, &t_fine);
    double pres_hpa = compensate_pressure(adc_P, t_fine);
    double humi_pct = compensate_humidity(adc_H, t_fine);

    reading->temperature_c = (float)temp_c;
    reading->pressure_hpa  = (float)pres_hpa;
    reading->humidity_pct  = (float)humi_pct;

    ESP_LOGI(TAG, "BME280: T=%.2f°C  RH=%.1f%%  P=%.2f hPa",
             reading->temperature_c, reading->humidity_pct, reading->pressure_hpa);

    return ESP_OK;
}
