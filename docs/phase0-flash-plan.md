# Phase 0 — Flash Plan (for tomorrow)

**Goal**: Confirm Petar's existing ESP32-C6 + SCD40 hardware works as a Zigbee end-device with Z2M, using florianL21's pre-built firmware as the validation tool.

This is **hardware validation only** — we're not writing any code in Phase 0. If it works, we know the hardware is good and we can move to Phase 1 (porting to a Z2M-native HiveKit firmware).

---

## Prerequisites checklist (do BEFORE flashing)

### 1. Hardware confirmation
- [ ] Confirm board model — florianL21's firmware is built for: **Seeed XIAO ESP32-C6** OR **bare ESP32-C6** with I²C on GPIO22 (SDA) / GPIO23 (SCL)
- [ ] **If your board is the DevKitC-1**: same chip, firmware will work, but you may need to manually wire I²C
- [ ] Confirm sensor is **SCD-40 or SCD-41** (both work — same I²C protocol, same Sensirion SCD-4x driver)
- [ ] Confirm wiring:
  - SDA → GPIO22
  - SCL → GPIO23
  - VCC → 3V3
  - GND → GND

### 2. Software prep (do on your Windows machine, not the VM)
- [ ] Use **Google Chrome or Microsoft Edge** (Web Serial not in Safari/Firefox)
- [ ] USB-C cable that supports **data** (not power-only — many random USB-C cables are charge-only)
- [ ] Backup current ESPHome config from the device first:
  - SSH to home server, find current YAML
  - Save copy locally — Phase 0 will overwrite it and you'll lose Thread/Matter functionality
- [ ] Decide: are you OK losing the office CO2 sensor on Home Assistant during testing? (Yes if you accept a few hours of no readings while we test Zigbee.)

### 3. Z2M prep
- [ ] Confirm Z2M is running and accepting new devices (enable "permit join" before flashing)
- [ ] Note the Z2M coordinator location — be **within 1m** during initial pairing (C6 antennas on dev boards can be flaky)

---

## Flashing steps (tomorrow)

1. **Hold BOOT button** on the C6 → plug in USB-C → release BOOT (puts chip in download mode, avoids any deep-sleep issue)
2. Open Chrome/Edge → go to: **https://florianl21.github.io/zigbee-co2-sensor/**
3. Click **Connect** → pick the USB serial device (probably "USB JTAG/serial debug unit")
4. ✅ Tick "**Erase device**" (clean slate — ensures any old ESPHome flash is wiped)
5. Click **Flash** → wait ~30-60 seconds
6. Once done, **unplug and replug** the USB cable
7. In Z2M: ensure **Permit join** is enabled (Dashboard → top right "Permit join (All)")
8. Watch the Z2M logs (`docker logs -f zigbee2mqtt` or in the UI) — should see "interview" start
9. Within ~30 seconds you should see the new device appear

---

## Expected result

In Z2M dashboard you should see a new device with:
- Manufacturer: probably `florianL21` or similar
- Model: SCD-4x CO2 sensor
- Exposes: **temperature, humidity, co2** (updating every 5 minutes)

If all three values appear and update — **Phase 0 is a success**. Hardware is validated.

---

## If it fails

| Symptom | Likely cause | Fix |
|---|---|---|
| Browser can't see serial port | Wrong USB cable (power-only) or driver missing | Try different cable; install CP210x or CH340 driver if needed |
| Flash fails partway | Chip not in download mode | Power-cycle while holding BOOT |
| Z2M never sees device after flash | Out of coordinator range, or join window closed | Move closer to coordinator; re-enable permit join |
| Device joins but no sensor values | I²C wiring wrong or SCD-4x not detected | Check SDA/SCL wires, check 3V3 power to sensor |
| Joins then drops within minutes | Known C6 antenna weakness | Move closer; consider external antenna later |

---

## OTA — current state

**florianL21's firmware does NOT support OTA.** To update it, you'd need to:
- Hold BOOT button, plug in USB
- Re-flash via web flasher

This is fine for Phase 0 (we just need to validate hardware).

**For HiveKit's own firmware (Phase 1+)** OTA *is* on the roadmap:
- ESP-Zigbee-SDK v2.0 supports the Zigbee OTA cluster (image notify → query → block transfer → upgrade)
- Z2M handles the OTA index file (`ota_index.json` per manufacturer)
- Realistic to implement in Phase 1 (~1 weekend extra effort), risk it slips to Phase 2 if there are integration headaches
- **Bottom line: yes, OTA is possible. Not in tomorrow's flash, but planned for HiveKit's own firmware.**

---

## After Phase 0 success — what's next

1. Note manufacturerName + modelIdentifier shown in Z2M (we'll change these for HiveKit firmware)
2. Test how the sensor behaves in Z2M for a few days — note any disconnects, value drops, etc.
3. Start Phase 1: scaffolding the `hivekit` ESP-IDF component, writing our own firmware, Z2M converter, web flasher, CI/CD.

---

## Tomorrow's TL;DR

1. Confirm wiring (GPIO22=SDA, GPIO23=SCL)
2. Chrome/Edge → https://florianl21.github.io/zigbee-co2-sensor/
3. Hold BOOT, plug in, release BOOT
4. Connect → tick "Erase device" → Flash
5. Permit join in Z2M
6. Wait for interview
7. Confirm 3 sensor values appear
