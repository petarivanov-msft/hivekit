# HiveKit — ESP32-C6 Zigbee Sensor Framework
## Research & Project Plan

*Researched and written: 2026-05-17*  
*Scope: A-to-Z research on building an open-source ESP32-C6 → Zigbee2MQTT sensor framework*

---

## Executive Summary

The gap is real and the timing is right. ESPHome's C6 Zigbee support as of May 2026 covers binary sensors only. Espressif's ESP-Zigbee-SDK v2.0 just dropped ZBOSS (licensing solved), the community is actively complaining about the lack of turnkey sensor firmware for C6, and at least 2–3 GitHub projects have proven the PoC is feasible. There's no project that does all of: Z2M integration, pre-built binaries, web flasher, and a sensor catalogue.

**Recommended architecture**: Thin C library (shared ESP-IDF component) + per-sensor firmware + pre-built binaries + web flasher + bundled Z2M converters. This is Option A in the architecture decision.

**Immediate next action**: Wire the SCD40 to your existing C6 and get it pairing with Z2M this weekend. Everything else follows from that PoC.

---

## Part 1: State of the Art

### 1.1 ESP-Zigbee-SDK (Espressif)

**The watershed news**: ESP-Zigbee-SDK v2.0 (released April/May 2026) removed the ZBOSS binary blob entirely. The new `esp-zigbee-lib` is Apache-2.0. This unblocks open-source distribution of pre-built firmware that includes the Zigbee stack.

**API change**: v2.0 uses a new `ezb_*` namespace (breaking from v1.x's `esp_zb_*`). Existing community projects based on v1.x will need migration. The ESPHome zigbee_esphome component will need a major rewrite for v2.0.

**Capabilities (v2.0)**:
- Zigbee 3.0 + Pro R23 + ZCL v8
- All device roles: Coordinator, Router, End Device, Sleepy End Device
- ZCL clusters: Temperature (0x0402), Humidity (0x0405), CO2 (0x040d), Pressure (0x0403), Occupancy (0x0406), Illuminance (0x0400), Analog Input (0x000c), OTA (0x0019), Power Config (0x0001) — all present and documented
- Light sleep: CONFIG_PM_ENABLE + tickless FreeRTOS idle — works well
- Deep sleep: technically possible but Espressif recommends against it for polling intervals <30min (stack re-init stresses the network)
- OTA Upgrade Cluster: full client + server support, documented with examples

**Limitations**:
- Requires ESP-IDF v5.5.4 — no Arduino framework, PlatformIO support lags
- API stability concerns: v1.x → v2.0 was a breaking change; may happen again
- Radio range on C6 reported as weaker than dedicated Zigbee chips (likely antenna design issue on dev boards)

### 1.2 ESPHome Zigbee (luar123/zigbee_esphome → mainline)

**Current status as of ESPHome 2026.5.0**: Binary sensors only on C6/H2. Full sensor support (temp, humidity, numbers) only on nRF52 platforms.

**Why it's insufficient**:
- Can't expose CO2, temperature, humidity as actual Zigbee sensors on C6 natively
- No pre-built binaries for C6
- No bundled Z2M converters
- The underlying component needs a full rewrite for ESP-Zigbee-SDK v2.0
- Timeline for full C6 sensor support in ESPHome is unknown

### 1.3 Notable Community Projects

| Project | Hardware | Sensors | Z2M? | OTA? | Web Flash? | Status |
|---|---|---|---|---|---|---|
| florianL21/zigbee-co2-sensor | XIAO C6 + SCD-41 | CO2+T+H | No (ZHA only) | No | Yes ✅ | Active |
| lmahmutov/esp32_c6_co2_sensor | C6 + Sensair S8 + BME280 | CO2+T+H+P | Unknown | No | No | Low activity |
| xyzroe/ZigUSB_C6 | C6 | USB power | Yes ✅ | Yes ✅ | Unknown | Active |
| xmow49/ESP32H2-Zigbee-Demo | H2 | Generic | No | No | No | Reference only |

**Takeaway**: florianL21 is the closest thing to what we want but targets ZHA not Z2M, has no OTA, and has no sensor catalogue. ZigUSB_C6 proves OTA + Z2M works on C6.

### 1.4 ZBOSS Licensing — Resolved

Historical concern: ZBOSS was a binary-only stack under DSR's ZOI membership model. Distributing pre-built firmware containing ZBOSS was a grey area.

**Current status**: Non-issue with SDK v2.0. Apache-2.0 throughout. Pre-built binary distribution is clean.

### 1.5 Zigbee2MQTT Converter System

**How it works**: Z2M reads Basic cluster attributes (manufacturerName, modelIdentifier) during interview, matches against registered converters via `zigbeeModel` array, configures reporting and exposes accordingly.

**Distribution**: Drop `.js` files in `external_converters/` — auto-loaded. No config.yaml change needed (changed in early 2025).

**Modern converter API**:
```javascript
const { modernExtend } = require('zigbee-herdsman-converters/lib/modernExtend');
module.exports = {
  zigbeeModel: ['HiveKit-SCD40-v1'],
  vendor: 'HiveKit', model: 'HKCO2-1',
  description: 'CO2 + Temperature + Humidity (SCD40)',
  extend: [modernExtend.co2(), modernExtend.temperature(), modernExtend.humidity(), modernExtend.battery()],
};
```

**Path to official support**: PR to `zigbee-herdsman-converters` dev branch, include device photo (512x512 PNG transparent). Maintainers active, typically 1–4 weeks turnaround.

**Known pain point**: C6 devices occasionally fail interview with "cannot get node descriptor" — usually resolved by being closer to coordinator during initial pairing.

### 1.6 ZCL Clusters — Sensor Mapping

| Sensor | Clusters Used | Z2M Support | ESP SDK |
|---|---|---|---|
| SCD40 (CO2+T+H) | 0x040d, 0x0402, 0x0405 | ✅ | ✅ |
| BME280 (T+H+P) | 0x0402, 0x0405, 0x0403 | ✅ | ✅ |
| BME680 (T+H+P+VOC) | 0x0402, 0x0405, 0x0403, 0x000c | ✅ (VOC via analog_input) | ✅ |
| SHT40 (T+H) | 0x0402, 0x0405 | ✅ | ✅ |
| SGP40 (TVOC) | 0x000c (analog_input) | ✅ | ✅ |
| PIR/mmWave | 0x0406 (occupancy) | ✅ | ✅ |
| Reed switch | 0x0006 (on/off) | ✅ | ✅ |
| Button | 0x0006 (on/off) or IAS Zone | ✅ | ✅ |

### 1.7 OTA Strategy

Z2M implements full ZCL OTA Upgrade Cluster server. Process:
1. Device polls `QueryNextImage` on configurable interval
2. Z2M checks `zigbee_ota_override_index_location` (JSON file or HTTPS URL) for available images
3. If match found by `{manufacturerCode, imageType, fileVersion}`: Z2M initiates transfer
4. Device downloads in blocks, validates, applies on reboot

Implementation effort: 2–4 days. ESP-Zigbee-SDK has a full working example. The main work is setting up the index JSON and hosting the binary on GitHub Releases or similar.

### 1.8 Hardware

**ESP32-C6**: Primary target. RISC-V 160MHz HP core, 512KB SRAM, WiFi6 + BT5.3 + 802.15.4, 1.4µA deep sleep. Best dev boards: **Seeed XIAO ESP32C6** (compact, affordable, proven in florianL21 project) and **ESP32-C6-DevKitC-1** (official Espressif, best for prototyping).

**ESP32-H2**: No WiFi, lower power (~half the active current of C6 for Zigbee), same 802.15.4 radio. Ideal for battery-powered nodes. Same codebase as C6 — just different `idf_target`. **Worth supporting as a secondary target from Phase 1.**

**ESP32-C5**: Too early. Zigbee SDK support is sparse. Skip until mid-2027.

---

## Part 2: Community Pain Points

Based on GitHub issues, Reddit, HA forums:

1. **"I have to install ESP-IDF just to flash a sensor"** — biggest barrier. Web flasher solves this.
2. **"I got it paired but Z2M shows 'unsupported device'"** — no bundled converter. This project solves it.
3. **"It joined Z2M but interview keeps failing"** — known C6 issue; needs documentation.
4. **"Deep sleep + Zigbee = constant reboots"** — Espressif guidance not well-publicised. Use light sleep.
5. **"There's no working example with CO2 sensor + Z2M"** — confirmed gap. Only ZHA examples exist.
6. **"How do I do OTA updates?"** — zero working C6 Zigbee sensor projects support OTA.
7. **"PlatformIO doesn't support the Zigbee SDK"** — real, frustrating; out of scope for us.
8. **"Is the ZBOSS license OK for my project?"** — was a concern, now resolved with v2.0.
9. **"The radio range is terrible"** — antenna design issue; document recommended antenna configurations.
10. **"There are no good multi-sensor examples"** — most examples are single-cluster minimal demos.

---

## Part 3: Architecture Decision

*Full analysis in `architecture-decision.md`. Summary:*

**Recommendation: Option A — Thin C Library + Per-Sensor Firmware**

A shared `hivekit` ESP-IDF component handles all the boilerplate (Zigbee init, cluster registration, OTA, sleep, re-join). Individual sensor firmwares are minimal C files that call into hivekit and talk to the hardware sensor.

**Why not Option B (config-driven)**: Takes 3–6 months to build the code generator infrastructure. ESPHome is already doing this. Wrong differentiation.

**Why not Option C (monolithic)**: Binary bloat, harder OTA, worse debugging.

**UX**: The "ESPHome experience" comes from pre-built binaries + web flasher (esptool-js via GitHub Pages), not from a build system. Users never need to install anything.

---

## Part 4: Z2M Integration Plan

### Converter Distribution Strategy
1. **Bundled in repo** (`converters/` directory): primary distribution — users drop files in `external_converters/`
2. **PR to zigbee-herdsman-converters**: submit after 30+ days real-world testing per sensor
3. **Both**: ultimately yes, but don't rush official PRs — quality matters more than speed

### Device Identity
Set in firmware via Basic cluster attributes:
- `manufacturerName`: "HiveKit"  
- `modelIdentifier`: "HKCO2-1" (for SCD40), "HKBME-1" (BME280), etc.
- Version scheme: `v{firmware_major}` suffix if needed for converter disambiguation

### OTA Index
Host at `https://raw.githubusercontent.com/{org}/hivekit/main/ota/index.json`:
```json
[
  {
    "fileName": "hivekit-scd40-c6-v1.2.0.ota",
    "url": "https://github.com/{org}/hivekit/releases/download/v1.2.0/hivekit-scd40-c6-v1.2.0.ota",
    "manufacturerCode": 0x1234,
    "imageType": 0x0001,
    "fileVersion": 0x00010200
  }
]
```

---

## Part 5: MVP Sensor Catalogue

Priority order for Phase 0-2:

| Priority | Sensor | IC | Clusters | Interface | Notes |
|---|---|---|---|---|---|
| ⭐⭐⭐ | CO2 + Temp + Humidity | SCD40 | 3 clusters | I2C | Petar's existing hardware |
| ⭐⭐⭐ | Temp + Humidity + Pressure | BME280 | 3 clusters | I2C/SPI | Most common combo |
| ⭐⭐ | Temp + Humidity | SHT40 | 2 clusters | I2C | Premium T+H, popular |
| ⭐⭐ | PIR Occupancy | Generic PIR | 0x0406 | GPIO | Simple, high demand |
| ⭐⭐ | Contact Sensor | Reed switch | 0x0006 | GPIO | Door/window sensors |
| ⭐ | TVOC | SGP40 | 0x000c | I2C | Analog input cluster, awkward |
| ⭐ | Temp + Humidity + VOC | BME680 | 4 clusters | I2C | VOC mapping tricky |
| ⭐ | Button | Tactile switch | 0x0006 | GPIO | Useful for remotes |

---

## Part 6: Project Plan (Phased)

### Phase 0: PoC (~1 weekend)
**Deliverable**: SCD40 on Petar's C6 pairs with Z2M and reports all 3 sensors  
**Effort**: 1–2 days  
**Risk**: Interview failures (known C6 issue) — keep device close to coordinator

### Phase 1: MVP (~2 weekends)
**Deliverables**:
- GitHub repo with clean structure
- `hivekit` shared component (core, OTA, sleep)
- SCD40 firmware using hivekit
- GitHub Actions CI (build + release)
- Web flasher (GitHub Pages + esptool-js)
- Z2M converter for SCD40
- Basic docs (getting started, wiring, pairing)

**Effort**: 3–4 days  
**Risk**: OTA implementation complexity (can defer to Phase 3 if needed)

### Phase 2: Sensor Catalogue (~6 weeks part-time)
**Deliverables**: BME280, SHT40, PIR, reed switch firmware; H2 support; Z2M PR submitted  
**Effort**: 2–3 days per sensor  
**Risk**: BME680 VOC cluster mapping is non-standard; PIR needs careful debounce logic

### Phase 3: OTA + Optional Config System (~4–8 weeks)
**Deliverables**: Robust OTA flow tested end-to-end; YAML config generator (if warranted)  
**Effort**: 2–4 weeks  
**Risk**: YAML config is a significant engineering investment — only worth it with 100+ stars

### Phase 4: Polish + Traction (ongoing)
**Deliverables**: PCB reference design, HACS listing, all sensors in Z2M mainline, community growth

---

## Part 7: Naming, Licensing, Branding

### Name Proposals

**1. HiveKit** ⭐ (recommended)
- Sensor = bee collecting data from its environment → hive = Z2M coordinator
- No Zigbee trademark, no Z2M trademark issues
- `.io` domain likely available; GitHub org probably available
- Feels professional; scalable to future (HiveKit CO2, HiveKit Multi, etc.)

**2. Zigforge**
- Forging Zigbee firmware — industrial connotation
- Clean from trademark perspective
- Slightly less memorable than HiveKit

**3. ClearBee**
- Clear air quality + bee metaphor
- More specific to environmental sensors (could limit scope perception)
- Risk: too cutesy for a technical project

### License Recommendation: **Apache-2.0**

Rationale:
- ESP-Zigbee-SDK v2.0 is Apache-2.0 — using Apache-2.0 for the project is consistent
- Apache-2.0 provides patent protection (MIT doesn't) — relevant for a protocol implementation
- Compatible with commercial use (encourages adoption)
- Well-understood by the ESP32 community (ESP-IDF itself is Apache-2.0)

MIT would also be acceptable but Apache-2.0 is the stronger choice here.

### Repo Structure

**Monorepo** (recommended):
- Everything in one repo: component library, sensor firmwares, converters, flasher, docs
- Simplifies CI, version coordination, contributor onboarding
- Split into separate repos only if community explicitly requests it (e.g., if converters get big enough to warrant their own release cycle)

---

## Part 8: Risks & Open Questions

### Risk 1: ESP-Zigbee-SDK API Churn
**Severity**: High | **Likelihood**: Medium

v2.0 broke v1.x entirely. It could happen again. The `hivekit` component acts as an abstraction layer — all sensor code is insulated. If v3.0 breaks things, only hivekit needs updating. Pin the SDK version in CI (IDF_COMPONENT_MANAGER_USE_COMPONENT_MANAGER=1 with a lockfile).

**Mitigation**: version-pin in CI; maintain a compatibility matrix in docs.

### Risk 2: ESPHome Absorbs the Use Case
**Severity**: Medium | **Likelihood**: High (12–18 month horizon)

ESPHome will eventually add full C6 Zigbee sensor support. When that happens, HiveKit's value proposition weakens.

**Mitigation**: Stay relevant by:
1. Being much simpler to use (no ESPHome build pipeline, no YAML, just a web flasher)
2. Bundling Z2M converters — ESPHome doesn't do this
3. Targeting OTA and battery-optimized builds specifically
4. Moving upmarket: custom PCBs, 3D cases, sensor kits as a potential revenue stream

### Risk 3: ZBOSS Licensing
**Severity**: Low (resolved) | **Likelihood**: n/a

SDK v2.0 is Apache-2.0. Non-issue. Document this clearly in README to head off questions.

### Risk 4: Radio Range Complaints
**Severity**: Medium | **Likelihood**: High

C6 has a known reputation for weak Zigbee radio range. This is mostly an antenna design issue on dev boards (XIAO's PCB antenna is small).

**Mitigation**: Document recommended antenna configurations; test with external antenna modules; note that H2 has similar radio power.

### Risk 5: Maintenance Burden
**Severity**: Medium | **Likelihood**: Medium (long-term)

Maintaining N sensor firmwares across Espressif SDK version upgrades is real work. The shared library approach helps — SDK changes are absorbed in one place.

**Mitigation**: GitHub Actions automation; clear contribution guide; ruthlessly defer new sensors unless there's demonstrated demand.

### Open Questions

1. **Is the CO2 cluster (0x040d) fully supported by Z2M `modernExtend.co2()`?** — zigpy has the cluster; Z2M converter works. Confirmed by florianL21 project using ZHA (which goes through zigpy). Z2M should work.

2. **What's the exact power draw of C6 in light sleep with Zigbee active?** — roughly 1–5mA average depending on polling interval. Needs empirical measurement. H2 is likely 40–60% of this.

3. **Can SCD40 measurement be triggered reliably from the LP core while HP core light sleeps?** — probably not; SCD4x needs ~5ms I2C transactions. Use wake-measure-sleep pattern instead.

4. **Is there a Z2M-blessed way to distribute external converters via URL?** — yes, `zigbee_ota_override_index_location` supports HTTPS URL. For converters, users can also use `external_converters: ['https://...']` in config.yaml.

---

## Immediate Next Actions

1. **This weekend**: Wire SCD40 to your C6 XIAO. Flash florianL21's firmware (it works, it has a web flasher). Confirm Z2M or ZHA sees the device. This validates your hardware before writing a line of code.

2. **Next weekend**: Set up ESP-IDF v5.5.x environment. Port florianL21's code to Z2M (write the converter). Confirm it pairs with Z2M and all 3 sensors report correctly.

3. **Create the GitHub repo** (name: HiveKit, or shortlist of 3 to pick from). Commit the PoC code even before it's clean — momentum matters.

4. **Check ESP-Zigbee-SDK v2.0 stability** before committing to it — look at the `esp-zigbee-sdk/issues` for v2.0 regressions. If unstable, start with v1.6.8 and migrate later.

---

*See also:*
- `state-of-the-art.md` — deep dive on each section
- `architecture-decision.md` — full pros/cons with code examples
- `mvp-roadmap.md` — kanban-ready task list
- `links.md` — all URLs referenced
