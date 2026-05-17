# State of the Art — ESP32-C6 + Zigbee2MQTT Ecosystem

*Researched: 2026-05-17*

---

## 1. ESP-Zigbee-SDK (Espressif Official)

### Current Version Status
- **v2.0** released approximately April/May 2026 — this is the watershed release
- v1.x (last: 1.6.8 on ESP-IDF v5.5.1) used the DSR ZBOSS stack as a binary blob
- **v2.0 removed ZBOSS entirely** and replaced with Espressif's own self-developed Zigbee stack (`esp-zigbee-lib`)
- `esp-zigbee-lib` is Apache-2.0 licensed — the entire SDK is now clean open-source-compatible
- New `ezb_*` API namespace in v2.0 (breaking change from `esp_zb_*` in v1.x)

### Capabilities
- Full Zigbee 3.0 + Zigbee Pro R23 + ZCL v8 compliance
- Device roles: Coordinator, Router, End Device, Sleepy End Device — all supported
- ZCL cluster coverage is comprehensive:
  - Basic, Power Configuration, Identify, Groups, Scenes, On/Off, Level Control
  - Temperature Measurement (0x0402), Relative Humidity (0x0405), Pressure (0x0403)
  - Carbon Dioxide Measurement (0x040d) — confirmed in SDK API docs
  - Illuminance (0x0400), Occupancy (0x0406)
  - Analog Input (0x000c), Binary Input (0x000f)
  - OTA Upgrade (0x0019)
  - ZCL Alarms, Poll Control, Binary Output/Value, Multistate Input/Output
  - Color Control, Thermostat, Electrical Measurement
- OTA: full ZCL OTA Upgrade Cluster implementation (both client and server roles)
- Sleep: light sleep supported (CONFIG_PM_ENABLE + tickless idle FreeRTOS)
  - Deep sleep is technically possible but stack must re-init on each wake — Espressif recommends against it for <30min polling intervals
  - Light sleep achieves low-µA idle with the Zigbee stack maintaining the connection via LP core
- ESP-IDF v5.3.2+ required for v1.6.x; v5.5.4 for v2.0

### Limitations
- **API churn is real**: v2.0 broke all v1.x code due to namespace change and architectural refactor. The zigbee_esphome component will need a major rewrite to migrate.
- Requires ESP-IDF toolchain — no Arduino v3 support for Zigbee on C6 (Arduino can wrap it but the toolchain setup remains painful)
- PlatformIO does not yet officially support Arduino 3.x (ESP-IDF 5.x) for C6 — another pain point
- Radio range on C6 reported as "especially weak" by some users (may be antenna/board design issue rather than silicon)
- Limited documentation for complex scenarios (multi-endpoint devices, custom clusters)

### Sources
- https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32c6/index.html
- https://github.com/espressif/esp-zigbee-sdk/blob/main/RELEASE_NOTES.md
- https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/migration-guide/v2.x/overview.html

---

## 2. ESPHome Zigbee Component

### History
- External component: `luar123/zigbee_esphome` — developed by community contributor luar123
- Initially targeted nRF52840 (Zigbee on Nordic chips via ZBOSS)
- ESPHome 2026.1.0: Full Zigbee support (sensors, switches, numbers) landed for **nRF52** platforms
- ESPHome 2026.5.0 (May 2026): Initial native Zigbee support for **ESP32-C6 and ESP32-H2** merged to mainline

### What Works on C6 Today (2026.5.0)
- **Binary sensors only** — GPIO state exposed as a Zigbee binary sensor
- ESP-IDF framework required (not Arduino)
- Can act as Zigbee router
- Integrates with Z2M and ZHA via standard clusters

### What's Missing on C6
- Temperature, humidity, CO2, pressure sensors — not yet natively supported
- Number/switch components — pending
- OTA Zigbee firmware updates — issue open but not scheduled
- nRF52 is months ahead of C6 in ESPHome Zigbee maturity

### Why It's Insufficient for the Project
- Only binary sensors is a non-starter for an environmental sensor project
- API migration required when ESP-Zigbee-SDK v2.0 becomes the base (breaking change)
- No pre-built binaries — user still needs to run ESPHome build pipeline
- No bundled Z2M converters
- Roadmap is undefined; C6 Zigbee in ESPHome is a side project for luar123

### Sources
- https://esphome.io/changelog/2026.1.0/
- https://community.home-assistant.io/t/esphome-esp32-zigbee-support/998550
- https://community.home-assistant.io/t/fyi-esphome-firmware-will-very-soon-also-have-native-zigbee-component-with-initial-support-for-building-zigbee-end-devices-with-esp32-microcontrollers-too/1007704

---

## 3. Notable GitHub Projects

### `florianL21/zigbee-co2-sensor` ⭐ Most Relevant
- **Targets**: Seeed XIAO ESP32C6 + Adafruit SCD-41 breakout
- **Clusters**: CO2 (0x040d), Temperature (0x0402), Humidity (0x0405)
- **Coordinator**: ZHA (not Z2M — notable gap)
- **Web flasher**: Yes — GitHub Pages + esptool-js, working in Chrome/Edge
- **3D case**: Yes — Printables link included
- **Power**: ~2.5mWh average over 14h (mains-powered oriented but battery feasible)
- **Sleep**: Uses single-shot SCD-4x mode; notes that deep sleep needs >30min polling for Zigbee
- **Weakness**: No Z2M converter bundled; targets ZHA only; no OTA; no battery management circuit
- Source: https://github.com/florianL21/zigbee-co2-sensor

### `xmow49/ESP32H2-Zigbee-Demo`
- Base demo that florianL21 built upon; H2 only
- Good reference for Zigbee stack initialization pattern
- Source: https://github.com/xmow49/ESP32H2-Zigbee-Demo

### `lmahmutov/esp32_c6_co2_sensor`
- C6 + Sensair S8 (UART CO2) + BME280 + SSD1306 OLED
- Russian project; no English docs; no web flasher; no Z2M converter
- Shows a multi-sensor single-firmware approach
- Source: https://github.com/lmahmutov/esp32_c6_co2_sensor

### `xyzroe/ZigUSB_C6`
- USB power monitor + switch with Z2M support and OTA
- One of very few C6 Zigbee projects with confirmed Z2M pairing and OTA
- Good reference for Z2M integration + OTA implementation
- Updated Nov 2024 / Dec 2025
- Source: https://github.com/xyzroe/ZigUSB_C6

### `firsttris/esp32c6-zigbee-router`
- ESPHome config to use C6 as a Zigbee router
- Works with Z2M and ZHA
- Not an end device; useful as a reference config
- Source: https://github.com/firsttris/esp32c6-zigbee-router

### `lhespress/zigpy-espzb`
- C6/H2 as remote Zigbee coordinator for ZHA via zigpy
- NCP firmware approach — different use case
- Source: https://github.com/lhespress/zigpy-espzb

### `AndroidCrypto/ESP32_C6_Zigbee_Coordinator`
- Tutorial-style coordinator project
- Good for understanding the coordinator side of the interview process
- Source: https://github.com/AndroidCrypto/ESP32_C6_Zigbee_Coordinator

---

## 4. ZBOSS Stack — Licensing

### Historical Situation (v1.x)
- ZBOSS is owned by DSR Corporation, licensed under the ZBOSS Open Initiative (ZOI)
- ZOI = "member-source" model: member companies get royalty-free source access, but it is **not** public open source
- Espressif joined ZOI in 2021; they could use ZBOSS but couldn't relicense the binaries as open source
- Distributing pre-built firmware containing ZBOSS was a grey area for independent OSS projects

### Current Situation (v2.0) — RESOLVED
- ESP-Zigbee-SDK v2.0 removes ZBOSS entirely
- `esp-zigbee-lib` (the new Espressif-owned Zigbee implementation) is Apache-2.0
- The entire SDK is Apache-2.0
- **Pre-built binary distribution is now unambiguously legal and open-source-compatible**
- There is still a pre-built library (you get `.a` files, not full source), but the license allows distribution

### Sources
- https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/migration-guide/v2.x/overview.html
- https://github.com/espressif/esp-zigbee-sdk/issues/803
- https://components.espressif.com/components/espressif/esp-zigbee-lib/versions/2.0.0/readme

---

## 5. Zigbee2MQTT External Converter System

### How It Works
1. Device pairs with Z2M coordinator — Z2M reads Basic cluster (manufacturerName, modelIdentifier)
2. Z2M looks up `zigbeeModel` array across all registered converters to find a match
3. If matched: device is configured per the converter (clusters, reporting, exposes)
4. If no match: device shown as "unsupported" with raw cluster data visible

### Converter Structure (modern API)
```javascript
const { modernExtend } = require('zigbee-herdsman-converters/lib/modernExtend');

module.exports = {
  zigbeeModel: ['HiveKit-SCD40-v1'],
  model: 'HKCO2-1',
  vendor: 'HiveKit',
  description: 'CO2 + Temperature + Humidity sensor (SCD40)',
  extend: [
    modernExtend.co2(),
    modernExtend.temperature(),
    modernExtend.humidity(),
    modernExtend.battery(),
  ],
};
```

### External Converter Distribution
- Drop `.js` files in `external_converters/` directory (auto-loaded since early 2025 — no config.yaml entry needed)
- Can be loaded from URL (remote HTTP index)
- Z2M frontend allows managing them under Settings > Dev console

### Path to Official Support
1. Write and test external converter
2. Fork `zigbee-herdsman-converters`
3. Add device definition to appropriate brand file
4. Submit PR to `dev` branch
5. Include 512x512 PNG device photo (transparent BG) for the supported devices page
- Z2M maintainers are active and turnaround is typically 1–4 weeks for clean PRs

### Device Fingerprinting
- `manufacturerName` set in firmware via `esp_zb_basic_cluster_add_attr()` or new v2.0 equivalent
- `modelIdentifier` likewise
- These are read during the interview (Basic cluster, attributes 0x0004 and 0x0005)
- Z2M matches on `zigbeeModel` array — can list multiple model strings for firmware revisions

### Known Interview Issues with ESP32-C6
- Several GitHub issues (#24202, #26728) report "Interview failed: cannot get node descriptor"
- Usually caused by: device being too far from coordinator, race condition during join, or wrong channel config
- Workaround: ensure device is close to coordinator during initial pairing; retry join if interview fails

### Sources
- https://www.zigbee2mqtt.io/advanced/support-new-devices/01_support_new_devices.html
- https://www.zigbee2mqtt.io/advanced/more/external_converters.html
- https://github.com/Koenkk/zigbee2mqtt/discussions/23142

---

## 6. Zigbee Cluster Library (ZCL) — Relevant Sensor Clusters

| Cluster | ID | Z2M Support | ESP SDK Support | Notes |
|---|---|---|---|---|
| Temperature Measurement | 0x0402 | ✅ `fz.temperature` | ✅ | Standard, solid |
| Relative Humidity | 0x0405 | ✅ `fz.humidity` | ✅ | Standard |
| Carbon Dioxide | 0x040d | ⚠️ Partial | ✅ | zigpy has it; Z2M `modernExtend.co2()` works |
| Pressure Measurement | 0x0403 | ✅ | ✅ | Standard |
| Illuminance | 0x0400 | ✅ | ✅ | |
| Occupancy | 0x0406 | ✅ | ✅ | |
| On/Off | 0x0006 | ✅ | ✅ | Reed switch, button |
| Level Control | 0x0008 | ✅ | ✅ | |
| Analog Input | 0x000c | ✅ | ✅ | TVOC (SGP40 mapped here) |
| OTA Upgrade | 0x0019 | ✅ | ✅ | Full support both sides |
| Power Configuration | 0x0001 | ✅ | ✅ | Battery % reporting |

### Zigbee 3.0 Device Type IDs for Multi-Sensors
- `0x0302` Temperature Sensor
- `0x0107` Occupancy Sensor
- `0x010a` Light Sensor
- `0x0106` Contact Sensor
- For multi-sensor (CO2 + temp + humidity): use `0x0302` as primary or define a custom device type with multiple clusters on endpoint 1

---

## 7. OTA Cluster Details

### How Zigbee OTA Works
1. Device implements OTA Upgrade Client (cluster 0x0019) on an endpoint
2. Device periodically sends `QueryNextImage` to coordinator
3. Z2M acts as OTA server — it checks its index for available images
4. If image available: Z2M sends `ImageNotify`; device requests blocks; Z2M delivers them
5. Device validates, applies, reboots

### Z2M OTA Index Mechanism
- Config key: `zigbee_ota_override_index_location` in `configuration.yaml`
- Points to a local JSON file or remote URL
- File contains array of image descriptors: `{ fileName, url, manufacturerCode, imageType, fileVersion }`
- Can be hosted on GitHub Releases or any HTTPS endpoint
- Can also use auto-update via `zigbee-OTA` release assets

### Espressif SDK OTA Support
- `esp_zb_zcl_ota_upgrade_query_next_image_req_cmd_req()` — client query API
- Full example at: `esp-zigbee-sdk/examples/ota_upgrade/`
- Realistic effort: 2–4 days to implement and test end-to-end with Z2M

### Sources
- https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/user-guide/zcl_ota_upgrade.html
- https://www.zigbee2mqtt.io/guide/configuration/ota-device-updates.html

---

## 8. Hardware Comparison

### ESP32-C6
- RISC-V dual-core (160MHz HP + 20MHz LP), 512KB SRAM, 802.15.4 + WiFi 6 + BT 5.3
- Deep sleep: 1.4µA; light sleep much higher but workable
- Active Zigbee TX: ~20–30mA (WiFi radio off but powered; adds overhead vs H2)
- Best boards: **Seeed XIAO ESP32C6** (XIAO form factor, excellent for compact builds), **ESP32-C6-DevKitC-1** (official Espressif dev board, best for prototyping)
- Price: ~$5–10 for XIAO; ~$8–15 for DevKitC

### ESP32-H2
- RISC-V single-core (96MHz), 802.15.4 + BT 5.3 only (no WiFi)
- Active Zigbee TX: ~10–15mA (roughly half C6 due to no WiFi silicon)
- Better choice for pure battery-powered Zigbee nodes
- Less widely stocked; fewer dev board options
- florianL21 project officially supports both C6 and H2 on the same codebase

### ESP32-C5 (newer)
- Dual-band (2.4GHz + 5GHz) WiFi 6E + BT + 802.15.4
- Less mature SDK support as of mid-2026; Zigbee examples sparse
- Not worth targeting for MVP — ecosystem not ready

### Recommendation
- **Primary target: ESP32-C6** (Seeed XIAO C6 as reference board — compact, affordable, good community)
- **Secondary target: ESP32-H2** (same codebase, just different board config — add as a supported variant in Phase 1)
- Skip C5 until ecosystem matures (~6–12 months)
