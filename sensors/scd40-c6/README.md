# HiveKit SCD40-C6 — Build & Flash Guide

## What this is

ESP-IDF v5.5 firmware for Seeed XIAO ESP32-C6 + Adafruit SCD-41 (or SCD-40) breakout.
Reports CO2 + temperature + humidity over Zigbee. Works natively with Zigbee2MQTT
via the external converter in `../../converters/hivekit-scd40.js`.

**Status**: Phase 1 scaffold — builds clean, logic stubbed where marked TODO.
Test with actual hardware after Phase 0 validates the SCD40 wiring.

---

## Hardware

| Pin | XIAO ESP32-C6 | SCD-41 Breakout |
|---|---|---|
| SDA | GPIO22 | SDA |
| SCL | GPIO23 | SCL |
| VCC | 3V3 | VIN (3.3V) |
| GND | GND | GND |

BOOT button: GPIO9 (built-in on XIAO)
Status LED: GPIO15 (built-in on XIAO — check your board revision)

---

## Prerequisites

```bash
# ESP-IDF v5.5.x
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && git checkout v5.5   # or latest v5.5.x tag
./install.sh esp32c6
source export.sh
```

---

## Build

```bash
cd sensors/scd40-c6
idf.py set-target esp32c6
idf.py update-dependencies   # downloads components from idf_component.yml
idf.py build
```

**First build takes 5–10 minutes** (ESP-IDF downloads Zigbee SDK + dependencies).

### If `sensirion/scd4x_i2c` is not found

The idf_component.yml has the Sensirion component commented out pending registry verification.
If it's not available:
1. Clone the reference driver: `git clone https://github.com/Sensirion/embedded-i2c-scd4x`
2. Copy the `sensirion_scd4x_i2c.c/h` files into `main/`
3. Add them to `main/CMakeLists.txt` SRCS
4. Update `sensor_scd40.c` to use Sensirion's API directly

---

## Flash

```bash
# Put XIAO in download mode: hold BOOT button, plug USB, release BOOT
idf.py -p /dev/ttyUSB0 flash monitor
# (use /dev/ttyACM0 or COMx on Windows)
```

Press Ctrl+] to exit the monitor.

---

## Z2M converter

After flashing:

1. Copy `converters/hivekit-scd40.js` to your Z2M `external_converters/` directory
2. Restart Z2M (or it auto-loads if `advanced.ext_pan_id` is configured)
3. Enable **Permit join** in Z2M dashboard
4. Power cycle the device. It will auto-enter pairing mode and keep retrying every 3s until it joins (no button press needed for first pair).
5. Watch Z2M logs — you should see `hk-scd40-c6` interviewing

---

## Button behaviour

| Press | Duration | Action |
|---|---|---|
| Single press | < 1s | Identify (LED blinks 3×) |
| Long press | 3 seconds | Pairing mode (network steering) |
| Very long press | 10 seconds | Factory reset (leave network + wipe NVS) |

---

## Zigbee identity

```
manufacturerName: "HiveKit"
modelIdentifier:  "hk-scd40-c6"
```

---

## Troubleshooting

**Build error: cannot find component `espressif/esp-zigbee-lib`**
→ Run `idf.py update-dependencies` after `idf.py set-target esp32c6`

**Z2M shows "unsupported device"**
→ Make sure `hivekit-scd40.js` is in your `external_converters/` directory and Z2M was restarted

**Interview fails ("cannot get node descriptor")**
→ Known ESP32-C6 issue; move device closer to coordinator and retry

**CO2 values look wrong (e.g. 0.0008 instead of 800)**
→ Z2M converter should handle the float→ppm conversion; check `hivekit-scd40.js`
