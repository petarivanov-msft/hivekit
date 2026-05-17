# Architecture Decision — ESP32-C6 Zigbee Sensor Framework

*Researched: 2026-05-17*

---

## The Three Approaches

### Option A: Thin C Library + Per-Sensor Firmware Projects

**What it is**: A shared C/ESP-IDF component library (`libhivekit`) that provides common Zigbee scaffolding, plus separate firmware repos/directories per sensor type. Each sensor gets its own pre-built binary.

**Structure**:
```
hivekit/
├── lib/                    # Shared component: Zigbee init, OTA, sleep, Basic cluster config
│   ├── hivekit_core.h
│   ├── hivekit_ota.c
│   └── hivekit_sleep.c
├── sensors/
│   ├── scd40/              # CO2 + temp + humidity
│   │   ├── main/
│   │   └── CMakeLists.txt
│   ├── bme280/
│   ├── pir_occupancy/
│   └── reed_switch/
├── converters/             # Z2M external converters (JS)
│   ├── hivekit-scd40.js
│   └── hivekit-bme280.js
├── flasher/                # GitHub Pages web flasher
│   └── index.html
└── docs/
```

**How it scales**: Each new sensor = new subdirectory with sensor-specific code + a new Z2M converter. GitHub Actions builds all sensor targets on push.

**Pros**:
- Simplest to build and ship quickly
- Each firmware is minimal — no unused code paths
- Easy to debug — one firmware = one sensor type
- No parser/code-generation step — just C code
- Lower binary size → faster OTA
- Community can contribute single-sensor PRs easily
- Anyone can understand the codebase at a glance

**Cons**:
- N firmwares to maintain (but GitHub Actions handles building)
- User must pick the right binary — needs clear documentation
- Combinatorial explosion if sensors share hardware (e.g., BME280 + PIR on same board)
- No runtime configuration — changing sensor config requires reflash

**Effort to MVP**: 2–3 weekends (SCD40 PoC in 1 weekend, repo structure + CI + docs in 2nd)

---

### Option B: Config-Driven Build System ("ESPHome Experience")

**What it is**: User provides a YAML or JSON config file describing which sensors are connected and on which pins. A CLI (or GitHub Actions workflow or hosted build service) generates ESP-IDF code and compiles it.

**Structure**:
```yaml
# my-sensor.yaml
board: xiao-esp32c6
sensors:
  - type: scd40
    i2c: { sda: 22, scl: 23 }
  - type: bme280
    i2c: { sda: 22, scl: 23, address: 0x76 }
zigbee:
  manufacturer: "MyHomeNet"
  model: "hallway-sensor-v1"
```

**Pros**:
- "ESPHome experience" — users love it
- Single firmware binary per user config (optimal size)
- Naturally handles multi-sensor combos
- Strong differentiation from existing projects

**Cons**:
- Massive engineering effort (code generator, type system, validation)
- Hosted build service adds infrastructure cost and maintenance burden
- Security/abuse surface on a build server
- Rebuilding what ESPHome already does, but for a niche
- Will be superseded if ESPHome adds C6 sensor support (likely in 6–12 months)
- Not suitable for Phase 0 or Phase 1

**Effort to MVP**: 3–6 months of serious engineering, full-time

---

### Option C: Monolithic Firmware with Runtime Sensor Enable

**What it is**: A single firmware binary that includes all sensor drivers. On first boot (or via serial command), user specifies which sensors to activate and on which pins. Config stored in NVS.

**Structure**:
- One fat binary (~2MB+)
- Serial CLI for configuration: `sensor add scd40 sda=22 scl=23`
- Config persisted in flash NVS

**Pros**:
- One binary to distribute — simpler logistics
- Can reconfigure without reflash

**Cons**:
- Complex sensor detection logic
- Binary size grows with every supported sensor
- OTA is harder (larger binary = more OTA transfer time/risk)
- Harder to debug — all code paths live together
- Serial config is a poor UX
- Every sensor has to be integrated in a way that it degrades gracefully when not present
- Higher memory pressure on 512KB SRAM

**Effort to MVP**: 4–8 weeks, but fragile

---

## Pros/Cons Matrix

| Criteria | A: Per-sensor | B: Config-driven | C: Monolithic |
|---|---|---|---|
| Time to working PoC | 1 weekend | 2–3 months | 3–4 weeks |
| Time to 5 sensors | 2–3 months | 4–6 months | 2–3 months |
| OSS contribution friction | Low | Medium | Medium |
| Binary size | Small ✅ | Small ✅ | Large ❌ |
| OTA reliability | High ✅ | High ✅ | Lower ❌ |
| Multi-sensor combos | Manual (scripted) | Native ✅ | Native ✅ |
| UX for end user | Good (pick binary) | Excellent ✅ | Poor (serial CLI) |
| Maintenance burden | Low ✅ | Very high ❌ | Medium |
| ESPHome obsolescence risk | Medium | High ❌ | Medium |
| Parallelization by contributors | Excellent ✅ | Hard | Hard |
| Toolchain dependency on user | None ✅ | None (if hosted) | None ✅ |

---

## Recommendation: Option A (Thin C Library + Per-Sensor)

**Rationale**:

1. **Speed to market is paramount.** Option A gets a working SCD40 device pairing with Z2M in a weekend. Option B takes months before anyone can use it. The community window for "first mover" is real — ESPHome will eventually catch up.

2. **The UX problem is solved by the web flasher, not the build system.** Pre-built binaries + GitHub Pages + esptool-js means the user flow is: plug in board → visit URL → click Flash → put device in pairing mode → done. No CLI. No YAML. This matches ESPHome's UX *for users* without the complexity of ESPHome's build pipeline.

3. **Combinatorial explosion doesn't materialise in practice.** Most Zigbee sensor builds are: one environmental sensor (SCD40 / BME280 / SHT40) + optional battery management. Multi-sensor combos can be pre-built variants (e.g., `scd40-bme280-c6.bin`). It's manageable.

4. **Option B is what ESPHome is already building.** Building a second YAML-driven system just to compete with ESPHome on their own terms is the wrong move. Differentiate by being laser-focused on Zigbee+Z2M with great defaults, not by being a general config platform.

5. **Apache-2.0 with esp-zigbee-sdk v2.0 means clean OSS.** No ZBOSS licensing headaches. Distribute binaries freely.

### Migration Path A → B
If the project gains traction, a config-driven layer can be added *on top of* the library in Phase 3+. The shared library (Option A) is the foundation that Option B would use internally anyway. Start with A, grow into B only if community demand justifies it.

---

## Recommended Repository Structure (Option A)

```
hivekit/
├── README.md
├── LICENSE (Apache-2.0)
├── .github/
│   └── workflows/
│       ├── build.yml           # Build all sensor firmware targets
│       └── release.yml         # Tag → GitHub Release with .bin assets
├── components/
│   └── hivekit/                # Shared ESP-IDF component
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── hivekit.h       # Core API: init, set_model, report_value, sleep
│       ├── hivekit_core.c
│       ├── hivekit_ota.c       # OTA client implementation
│       └── hivekit_sleep.c     # Light sleep helpers
├── sensors/
│   ├── scd40-c6/               # SCD40 on C6 (Phase 0)
│   ├── scd40-h2/               # Same sensor, H2 target
│   ├── bme280-c6/              # Phase 2
│   ├── sht40-c6/
│   ├── pir-c6/
│   └── reed-c6/
├── converters/                 # Z2M external converters
│   ├── hivekit-scd40.js
│   ├── hivekit-bme280.js
│   └── index.json              # Z2M OTA index + converter manifest
├── flasher/                    # GitHub Pages web flasher
│   ├── index.html              # esptool-js based
│   ├── manifest.json           # Espressif web installer manifest
│   └── firmwares/              # Symlinks/redirects to GH Releases
└── docs/
    ├── index.md
    ├── getting-started.md
    ├── sensors/
    └── contributing.md
```

---

## Firmware Internal Architecture

Each per-sensor firmware follows this pattern:

```c
// main/main.c
#include "hivekit.h"
#include "driver/i2c.h"
#include "scd4x.h"         // Sensirion SCD4x driver

void app_main(void) {
    // 1. Init Zigbee with device identity
    hivekit_config_t cfg = {
        .manufacturer_name = "HiveKit",
        .model_identifier  = "HKCO2-1",
        .device_type       = ZB_DEVICE_TYPE_TEMPERATURE_SENSOR, // endpoint 1
    };
    hivekit_init(&cfg);

    // 2. Register clusters
    hivekit_add_cluster(CLUSTER_CARBON_DIOXIDE);
    hivekit_add_cluster(CLUSTER_TEMPERATURE);
    hivekit_add_cluster(CLUSTER_HUMIDITY);
    hivekit_add_cluster(CLUSTER_OTA);
    hivekit_add_cluster(CLUSTER_BATTERY);

    // 3. Start Zigbee + join network
    hivekit_start();

    // 4. Sensor loop
    while (1) {
        float co2, temp, humidity;
        scd4x_read_single_shot(&co2, &temp, &humidity);

        hivekit_report(CLUSTER_CARBON_DIOXIDE, co2);
        hivekit_report(CLUSTER_TEMPERATURE, temp);
        hivekit_report(CLUSTER_HUMIDITY, humidity);

        hivekit_sleep_ms(300000); // 5-min light sleep
    }
}
```

The `hivekit` component handles: Zigbee stack init, cluster registration, attribute reporting, OTA polling, network re-join on disconnect, Basic cluster attributes, sleep management.
