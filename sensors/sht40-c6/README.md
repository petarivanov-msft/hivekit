# HiveKit SHT40-C6

> ⚠️ **UNTESTED** — community verification welcome.
> Flash it and report your results: https://github.com/petarivanov-msft/hivekit/issues

Sensirion SHT40 temperature + humidity sensor firmware for the Seeed XIAO ESP32-C6.
Exposes **temperature** and **humidity** via Zigbee to Zigbee2MQTT (or ZHA).

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU       | Seeed XIAO ESP32-C6 |
| Sensor    | Sensirion SHT40 breakout board (any brand) |
| I²C SDA   | GPIO22 |
| I²C SCL   | GPIO23 |
| I²C addr  | 0x44 (SHT40-AD1B default) or 0x45 / 0x46 for alternate variants |
| Button    | GPIO9 (BOOT — pair / factory reset) |
| LED       | GPIO15 |

**Wiring:** connect SHT40 VDD to 3.3V, GND to GND, SDA to GPIO22, SCL to GPIO23.
Most SHT40 breakout boards default to address 0x44.

---

## I²C address

The SHT40 family has three possible I²C addresses depending on variant:
- **0x44** — SHT40-AD1B (default, most breakout boards)
- **0x45** — SHT40-BD1B
- **0x46** — SHT40-BD1B alternate

The default firmware uses **0x44**. To change, set `CONFIG_HIVEKIT_SHT40_I2C_ADDR=0x45`
(or `0x46`) in `sdkconfig.defaults` before building.

*Source: SHT40 datasheet §2.1*
*URL: https://sensirion.com/media/documents/33C09C07/626C2DBA/Datasheet_SHT4x.pdf*

---

## Sensor configuration

| Setting | Value |
|---------|-------|
| Measurement command | 0xFD (high precision, no heater) |
| Max conversion time | 8.2 ms |
| Read interval | 30 seconds (default, configurable) |

*Source: SHT40 datasheet §2.4, Table 4*

---

## Zigbee clusters

| Cluster ID | Name | Data type | Unit |
|------------|------|-----------|------|
| 0x0402 | Temperature Measurement | int16 ×100 | °C |
| 0x0405 | Relative Humidity | uint16 ×100 | % |

---

## Zigbee2MQTT converter

Copy `converters/hivekit-sht40.js` to `<z2m-data>/external_converters/` and add to `configuration.yaml`:

```yaml
external_converters:
  - hivekit-sht40.js
```

Restart Z2M. The device will appear with `temperature` and `humidity` exposes.

---

## Build

Requires ESP-IDF v5.5+ with esp-zigbee-lib v2.x.

```bash
cd sensors/sht40-c6
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
| ☆ **Untested** | Code is based on the Sensirion SHT40 datasheet and follows the SCD40/BME280 pattern. No hardware testing has been performed. |

If you test this firmware, please open an issue with your results (sensor brand/model, Z2M version, whether T and RH appeared correctly, CRC errors if any).

---

## Driver notes

The firmware implements the SHT40 I²C driver from scratch using ESP-IDF's `i2c_master` API
(same approach as SCD40 and BME280 — no external component needed).

The SHT40 protocol is much simpler than BME280: send a 1-byte measurement command,
wait ~10 ms, read 6 bytes (T_MSB, T_LSB, CRC_T, RH_MSB, RH_LSB, CRC_RH).

**CRC-8** is verified for both T and RH words (polynomial 0x31, initial 0xFF).
Test vector from SHT40 datasheet §4.4: `crc8([0xBE, 0xEF]) == 0x92` ✓

**Conversion formulas** per datasheet §4.6:
- `T [°C] = -45 + 175 × (S_T / 65535)`
- `RH [%] = -6 + 125 × (S_RH / 65535)`, clamped to [0, 100]

No calibration registers, no trim data — the SHT40 is factory-calibrated.
