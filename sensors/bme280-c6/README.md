# HiveKit BME280-C6

> ⚠️ **UNTESTED** — community verification welcome.
> Flash it and report your results: https://github.com/petarivanov-msft/hivekit/issues

BME280 atmospheric sensor firmware for the Seeed XIAO ESP32-C6.
Exposes **temperature**, **humidity**, and **barometric pressure** via Zigbee to Zigbee2MQTT (or ZHA).

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU       | Seeed XIAO ESP32-C6 |
| Sensor    | Bosch BME280 breakout board (any brand) |
| I²C SDA   | GPIO22 |
| I²C SCL   | GPIO23 |
| I²C addr  | 0x76 (SDO→GND, default) or 0x77 (SDO→3.3V) |
| Button    | GPIO9 (BOOT — pair / factory reset) |
| LED       | GPIO15 |

**Wiring:** connect BME280 VCC to 3.3V, GND to GND, SDA to GPIO22, SCL to GPIO23.
If your module has a SDO/ADDR pin, leave it unconnected or tie to GND for address 0x76.

---

## I²C address

The BME280 has two possible I²C addresses:
- **0x76** — SDO pin tied to GND (default for most breakout boards)
- **0x77** — SDO pin tied to VDDIO (3.3V)

The default firmware uses **0x76**. To change, set `CONFIG_HIVEKIT_BME280_I2C_ADDR=0x77` in `sdkconfig.defaults` before building.

*Source: BME280 datasheet §6.2, Table 13*

---

## Sensor configuration

Indoor environmental sensing (Bosch recommendation §3.5.1):

| Setting | Value |
|---------|-------|
| Temperature oversampling | ×2 |
| Humidity oversampling | ×1 |
| Pressure oversampling | ×16 |
| IIR filter coefficient | 16 |
| Mode | Forced (single-shot per interval) |
| Read interval | 30 seconds (default) |

*Source: BME280 datasheet §3.5.1 "Weather monitoring" recommended settings*

---

## Zigbee clusters

| Cluster ID | Name | Data type | Unit |
|------------|------|-----------|------|
| 0x0402 | Temperature Measurement | int16 ×100 | °C |
| 0x0405 | Relative Humidity | uint16 ×100 | % |
| 0x0403 | Pressure Measurement | int16 | hPa (integer) |

---

## Zigbee2MQTT converter

Copy `converters/hivekit-bme280.js` to `<z2m-data>/external_converters/` and add to `configuration.yaml`:

```yaml
external_converters:
  - hivekit-bme280.js
```

Restart Z2M. The device will appear with `temperature`, `humidity`, and `pressure` exposes.

---

## Build

Requires ESP-IDF v5.5+ with esp-zigbee-lib v2.x.

```bash
cd sensors/bme280-c6
idf.py set-target esp32c6
idf.py build
idf.py flash
```

Or use the [web flasher](https://petarivanov-msft.github.io/hivekit/) — no terminal required.

---

## Pairing

1. Flash the firmware
2. Enable **Permit Join** in Zigbee2MQTT
3. Hold the BOOT button for 3 seconds (LED blinks fast), then release
4. The device will join and appear in Z2M

---

## Status

| Status | Notes |
|--------|-------|
| ☆ **Untested** | Code is based on the Bosch BME280 datasheet and follows the SCD40 pattern. No hardware testing has been performed. |

If you test this firmware, please open an issue with your results (sensor brand/model, Z2M version, whether T/H/P all appeared correctly).

---

## Driver notes

The firmware implements the BME280 I²C driver from scratch using ESP-IDF's `i2c_master` API (no external component).

Calibration compensation uses the **double-precision** reference algorithm from *BME280 datasheet §4.2.3 (rev 1.14)*.
This is the same algorithm used in the official Bosch SensorAPI (BSD-3-Clause).
The trim parameters (dig_T1–T3, dig_P1–P9, dig_H1–H6) are read from sensor NVM on init.
