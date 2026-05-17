# MVP Roadmap — HiveKit Zigbee Sensor Framework

*Created: 2026-05-17 | Format: Kanban-ready task list*

---

## Phase 0: PoC — "Does it actually work?"
**Goal**: SCD40 on C6 pairs with Z2M, reports CO2 + temp + humidity. No polish needed.  
**Effort**: ~1 weekend (2 sessions, ~8h total)  
**Success criteria**: Z2M shows 3 sensor values updating every 5 minutes

### Tasks

- [ ] **[P0-01]** Set up ESP-IDF v5.5.x + ESP-Zigbee-SDK v2.0 development environment on dev machine
- [ ] **[P0-02]** Clone ESP-Zigbee-SDK and study the `HA_temperature_humidity_sensor` example end-to-end
- [ ] **[P0-03]** Wire SCD40 breakout to XIAO ESP32C6 (I2C: SDA=GPIO22, SCL=GPIO23, 3.3V power)
- [ ] **[P0-04]** Integrate Sensirion SCD4x I2C driver (use Sensirion's own C driver or port)
- [ ] **[P0-05]** Implement 3-cluster Zigbee device: CO2 (0x040d) + temp (0x0402) + humidity (0x0405)
- [ ] **[P0-06]** Set correct manufacturerName and modelIdentifier in Basic cluster
- [ ] **[P0-07]** Flash to C6, pair with Z2M, verify interview succeeds
- [ ] **[P0-08]** Verify all 3 sensor values appear in Z2M dashboard
- [ ] **[P0-09]** Write basic Z2M external converter JS file for the device
- [ ] **[P0-10]** Document wiring and gotchas in a README.md

**Key Risks**:
- ESP-Zigbee-SDK v2.0 API (`ezb_*`) may have regressions — fall back to v1.6.8 if unstable
- Interview failure from Z2M side (known C6 issue) — ensure device is <1m from coordinator
- SCD4x driver I2C conflicts with Zigbee radio timing (use LP core for I2C or careful task pinning)

---

## Phase 1: MVP — "A real open-source project"
**Goal**: Repo is clean, CI builds pass, SCD40 works end-to-end with OTA, basic docs published.  
**Effort**: ~2 weekends  
**Success criteria**: A stranger can follow the README, flash the binary, and have a working device

### Tasks

**Repo Structure**
- [ ] **[P1-01]** Create GitHub repo (name TBD — see naming section)
- [ ] **[P1-02]** Set up monorepo structure: `components/hivekit/`, `sensors/scd40-c6/`, `converters/`, `flasher/`, `docs/`
- [ ] **[P1-03]** Configure `.gitmodules` for ESP-Zigbee-SDK as submodule or depend via IDF Component Manager
- [ ] **[P1-04]** Write Apache-2.0 `LICENSE` file

**Shared Library (hivekit component)**
- [ ] **[P1-05]** Extract common Zigbee init code into `components/hivekit/hivekit_core.c`
- [ ] **[P1-06]** Implement `hivekit_init()`, `hivekit_add_cluster()`, `hivekit_start()`, `hivekit_report()`
- [ ] **[P1-07]** Implement `hivekit_sleep_ms()` using light sleep (CONFIG_PM_ENABLE)
- [ ] **[P1-08]** Handle network re-join on disconnect (signal handler for `ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT`)
- [ ] **[P1-09]** Implement OTA client in `hivekit_ota.c` — poll, download, apply

**SCD40 Sensor Firmware**
- [ ] **[P1-10]** Refactor PoC code to use `hivekit` component
- [ ] **[P1-11]** Support both mains-powered (5-min polling) and battery-hint mode (15-min polling, light sleep)
- [ ] **[P1-12]** Add boot button: hold 3s = factory reset (leave network + erase NVS)
- [ ] **[P1-13]** Status LED: fast blink = joining, slow blink = connected, off = sleeping

**CI/CD**
- [ ] **[P1-14]** GitHub Actions: build `scd40-c6` on push to main
- [ ] **[P1-15]** GitHub Actions release workflow: on tag push, build all targets → upload `.bin` to GitHub Release
- [ ] **[P1-16]** Pin ESP-IDF version in CI (Docker container or `idf_tools.py install`)

**Web Flasher**
- [ ] **[P1-17]** Set up GitHub Pages branch
- [ ] **[P1-18]** Implement `flasher/index.html` using `esptool-js` (espressif/esptool-js)
- [ ] **[P1-19]** Create Espressif web installer manifest JSON (points to GitHub Release `.bin`)
- [ ] **[P1-20]** Test flashing works in Chrome and Edge (not Safari — Web Serial not supported)

**Z2M Converter**
- [ ] **[P1-21]** Finalize `converters/hivekit-scd40.js` using `modernExtend`
- [ ] **[P1-22]** Test external converter drop-in with Z2M (place in `external_converters/`)
- [ ] **[P1-23]** Add device photo for Z2M supported devices page (512x512 PNG, transparent BG)

**Documentation**
- [ ] **[P1-24]** Write `docs/getting-started.md` (wiring diagram, flash instructions, Z2M pairing steps)
- [ ] **[P1-25]** Set up MkDocs with Material theme (mkdocs-material)
- [ ] **[P1-26]** Deploy docs to GitHub Pages alongside flasher

**Key Risks**:
- OTA implementation complexity — may slip to Phase 2 if blocking
- esptool-js Web Serial API requires HTTPS — GitHub Pages covers this
- CI build time: ESP-IDF builds are slow (~5-10 min) — cache aggressively in Actions

---

## Phase 2: Sensor Catalogue — "People actually use it"
**Goal**: 5 sensors working, Z2M converters bundled, community finds the project.  
**Effort**: ~6 weeks (part-time)  
**Success criteria**: 50+ GitHub stars, at least 1 external contributor PR

### Tasks

**Sensor Additions**
- [ ] **[P2-01]** Add `sensors/bme280-c6/` — temp + humidity + pressure (3 clusters)
- [ ] **[P2-02]** Add `sensors/sht40-c6/` — temp + humidity (simpler alternative to SCD40)
- [ ] **[P2-03]** Add `sensors/pir-c6/` — PIR motion → occupancy cluster (0x0406)
- [ ] **[P2-04]** Add `sensors/reed-c6/` — reed switch/contact sensor → IAS Zone or On/Off cluster
- [ ] **[P2-05]** Add `sensors/scd40-h2/` — same SCD40 firmware, ESP32-H2 target
- [ ] **[P2-06]** Add `sensors/bme680-c6/` — temp + humidity + pressure + VOC (SGP40 analog input cluster)

**Per-Sensor Work** (each)
- [ ] Integrate hardware driver
- [ ] Wire to hivekit component
- [ ] Write Z2M external converter
- [ ] Add CI build target
- [ ] Write wiring guide in docs

**Z2M Converter Distribution**
- [ ] **[P2-07]** Create `converters/` directory as the canonical drop-in location
- [ ] **[P2-08]** Create `converters/manifest.json` listing all converters for easy discovery
- [ ] **[P2-09]** Submit PRs to `zigbee-herdsman-converters` for each sensor (after 30+ days of real-world testing)

**Community**
- [ ] **[P2-10]** Write CONTRIBUTING.md (how to add a new sensor: code + converter + docs)
- [ ] **[P2-11]** Create GitHub Discussions: Usage, Show & Tell, Sensor Requests
- [ ] **[P2-12]** Post to r/esp32, r/homeassistant, Zigbee2MQTT discussions
- [ ] **[P2-13]** Submit to ESPHome community showcase

**Key Risks**:
- BME680 VOC measurement doesn't map cleanly to a standard ZCL cluster — may need analog_input workaround
- Interview failures with some Z2M coordinator firmware versions — document known-good configs

---

## Phase 3: OTA + Config-Driven Builds (Optional)
**Goal**: Robust OTA working; explore YAML config if community demands it.  
**Effort**: 4–8 weeks  
**Success criteria**: OTA tested and shipped for SCD40 device; clear signal on YAML demand

### Tasks

**OTA Polish**
- [ ] **[P3-01]** Implement hosted OTA index file (hosted on GitHub Releases)
- [ ] **[P3-02]** Add Z2M OTA index override config example to docs
- [ ] **[P3-03]** Sign firmware images (Espressif secure boot optional, at least SHA256 integrity)
- [ ] **[P3-04]** Test OTA rollback on failed update

**YAML Config (if warranted)**
- [ ] **[P3-05]** Design YAML schema for sensor configuration
- [ ] **[P3-06]** Implement Python code generator: YAML → ESP-IDF C files
- [ ] **[P3-07]** Integrate with CI: user-submitted YAML → built binary via GitHub Actions workflow_dispatch

**Battery Profiling**
- [ ] **[P3-08]** Measure actual current draw for each sensor in various sleep modes
- [ ] **[P3-09]** Document expected battery life per sensor (CR2032, 18650, AA)
- [ ] **[P3-10]** Add battery % reporting via Power Configuration cluster

---

## Phase 4: Polish + Community Traction
**Goal**: Project feels production-ready; community contributes regularly.  
**Effort**: Ongoing  
**Success criteria**: 5 external contributors, in Z2M official supported list

### Tasks

- [ ] **[P4-01]** Custom PCB reference design (KiCad) for XIAO C6 + SCD40 + LiPo battery
- [ ] **[P4-02]** Home Assistant HACS integration listing
- [ ] **[P4-03]** All sensors accepted in zigbee-herdsman-converters mainline
- [ ] **[P4-04]** Version stability — freeze hivekit component API v1.0
- [ ] **[P4-05]** Multi-language docs (at minimum German and French — strong Zigbee communities)
- [ ] **[P4-06]** YouTube "getting started" video

---

## Deferred / Won't Do (for now)
- ESP32-C5 support (ecosystem too immature)
- Thread/Matter compatibility (separate project)
- Mobile app
- Cloud integration (antithetical to Z2M's local philosophy)
