# Phase 1 Plan — HiveKit MVP

*Created: 2026-05-17*

## Goal

First **real HiveKit firmware**: SCD40/SCD41 on XIAO ESP32-C6 reporting CO2 + temperature + humidity via Zigbee, natively supported by Zigbee2MQTT via an external converter.

Phase 0 proves the hardware works (using florianL21's pre-built fw). Phase 1 is our own firmware built on top of the `hivekit` shared component with correct Z2M identity.

---

## Acceptance Criteria for "Phase 1 Done"

- [ ] `sensors/scd40-c6/` builds cleanly with ESP-IDF v5.5 + `idf.py build`
- [ ] Device pairs with Z2M (interview succeeds, no "unsupported device")
- [ ] All 3 sensor values (CO2 ppm, temp °C, humidity %) appear in Z2M dashboard
- [ ] Sensor values update on the configured interval (default 5 min)
- [ ] BOOT button 3s press → network steering (LED slow blinks)
- [ ] BOOT button 10s press → factory reset (LED fast blinks, then reboot)
- [ ] `converters/hivekit-scd40.js` works as Z2M external converter drop-in
- [ ] GitHub Actions build passes on push to main
- [ ] README gives enough info to build, flash, and pair

---

## What We Build

### Hardware Target
- Board: Seeed XIAO ESP32-C6
- Sensor: Adafruit SCD-41 breakout (SCD-40 also works — same I²C protocol)
- I²C: SDA=GPIO22, SCL=GPIO23
- Button: BOOT button GPIO9 (active LOW, internal pull-up)
- LED: GPIO15 (or onboard LED, TBD per board variant) — see `Kconfig.projbuild`
- Power: USB or 3.3V LDO

### Firmware Stack
- ESP-IDF v5.5
- ESP-Zigbee-SDK v2.x (via IDF Component Manager, `espressif/esp-zigbee-lib ~2.0.0`)
- Sensirion SCD4x I²C driver (via component manager, or vendored)
- `espressif/button ^4.0.0` for BOOT button handling
- `espressif/led_indicator ^0.9.0` for LED state machine

### Zigbee Identity (Z2M matching)
```
manufacturerName: "HiveKit"
modelIdentifier:  "hk-scd40-c6"
```
Z2M will match this against `zigbeeModel` in `converters/hivekit-scd40.js`.

### ZCL Clusters (Endpoint 1, HA profile 0x0104)
| Cluster | ID | Direction | Attribute |
|---|---|---|---|
| Basic | 0x0000 | Server | mfr name, model ID, SW build |
| Identify | 0x0003 | Server | — |
| Temperature Measurement | 0x0402 | Server | MeasuredValue (int16, ×100) |
| Relative Humidity | 0x0405 | Server | MeasuredValue (uint16, ×100) |
| CO2 Measurement | 0x040d | Server | MeasuredValue (single float, fraction of full scale) |

**Note on CO2 cluster (0x040d)**: ZCL specifies CO2 MeasuredValue as a `single` float in the range 0.0–1.0 (fraction of full scale, where 1.0 = 1,000,000 ppm). Z2M's `modernExtend.co2()` reads this and converts to ppm. So:
`float co2_zcl = co2_ppm / 1_000_000.0f`

---

## Day-by-Day Plan

### Day 1 — SDK verification + scaffold (this doc)
- [x] Read all research docs
- [x] Fetch and verify v2 SDK headers (bdb.h, core.h, zha.h, cluster desc headers)
- [x] Understand `esp_zigbee.h` wrapper + `esp_zigbee_launch_mainloop()` pattern
- [x] Create scaffold: `components/hivekit/`, `sensors/scd40-c6/`, converters, CI

### Day 2 — Phase 0 success (Petar's task)
- Flash florianL21 firmware via web flasher
- Confirm SCD40 hardware works with Z2M
- Note down what Z2M interview looks like
- Flag any I²C or pairing issues

### Day 3 — Build environment setup (Petar's task)
- Install ESP-IDF v5.5.x on dev machine
- Run `idf.py build` on `sensors/scd40-c6/`
- Fix any toolchain/dependency issues
- Report what the `idf_component.yml` actually resolves to

### Day 4 — First flash of HiveKit firmware
- Fix any build errors from Day 3
- Flash to XIAO C6
- Confirm Zigbee pairing with Z2M
- Confirm Z2M shows "hk-scd40-c6" (manufacturer: HiveKit)
- Drop `converters/hivekit-scd40.js` into Z2M external_converters/
- Confirm sensor values appear

### Day 5 — Polish + CI
- Test BOOT button (3s pairing, 10s reset)
- Test LED patterns
- Verify CI builds pass on GitHub
- Update README with actual build/flash steps verified by Petar

---

## Build / Flash Workflow

### Prerequisites
```bash
# Install ESP-IDF v5.5.x
cd ~/esp && git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && git checkout v5.5.x
./install.sh esp32c6
source export.sh
```

### Build
```bash
cd sensors/scd40-c6
idf.py set-target esp32c6
idf.py build
```

### Flash
```bash
# Put XIAO C6 in download mode: hold BOOT, plug USB, release BOOT
idf.py -p /dev/ttyUSB0 flash monitor
```
Or use the web flasher (Phase 1 deliverable, not yet built).

### Pair with Z2M
1. Enable "Permit join" in Z2M dashboard
2. Power on the device
3. If already joined, hold BOOT for 3s to trigger steering
4. Watch Z2M logs for interview

---

## Known Risks & Mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| ESP-Zigbee-SDK v2.x CO2 cluster API differs from what we scaffold | Medium | All API calls in code have `// SOURCE:` comments with the exact header URL; Petar must verify at build time |
| Interview failure ("cannot get node descriptor") | Medium | Known C6 issue; keep device <1m from coordinator; ensure primary channels set correctly |
| Sensirion SCD4x component name wrong on registry | Low | idf_component.yml has a TODO comment; verify with `idf.py build` |
| Button component changed API in v4.x | Low | All calls documented; check `espressif/button` changelog if build fails |
| LED GPIO varies by board revision | Low | Made configurable via Kconfig |
| CO2 float encoding wrong (Z2M shows wrong values) | Medium | Documented the conversion; easy to fix if it's off |

---

## What's NOT in Phase 1

- OTA (deferred to Phase 2/3 — complexity risk)
- Web flasher (deferred — GitHub Pages setup is separate)
- Battery power optimisation (mains-powered first)
- Second sensor type (BME280, SHT40)
- Official Z2M PR (needs 30+ days real-world testing first)

---

## Reference: florianL21's approach

florianL21/zigbee-co2-sensor is MIT-licensed and targets ZHA (not Z2M). We drew inspiration for:
- Hardware wiring (GPIO22/23 for I²C is confirmed working on XIAO C6)
- SCD4x periodic measurement pattern
- Button + LED handling pattern

We do **not** copy their code. Our implementation is independent, uses the v2 SDK APIs, targets Z2M, and has different firmware identity (HiveKit vs florianL21).
