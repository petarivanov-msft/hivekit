# HiveKit

> ## 🚧 WIP — Experimental Branch In Play
>
> The `main` branch is the **known-good baseline** (`v0.3.2-known-good`) — it pairs reliably and reports CO2 / temperature / humidity.
>
> An experimental branch [`experiment/sleep-enable`](https://github.com/petarivanov-msft/hivekit/tree/experiment/sleep-enable) is under test to fix a parent-selection issue on multi-router meshes (the C6 currently only joins when physically next to the coordinator). It adds two `sdkconfig` lines:
>
> - `CONFIG_IEEE802154_SLEEP_ENABLE=y` — extends `macResponseWaitTime` so slow routers (Aqara E1 wall switches) can ACK association requests inside the scan window.
> - `CONFIG_ZB_ENABLE_ZGP=n` — disables Zigbee Green Power, freeing scan budget.
>
> Background: `esp-zigbee-sdk` 2.x shortened per-channel scan dwell time and tightened association timing vs 1.5.x. Some routers (especially Aqara) take 35–42 symbols to ACK, landing outside the default 2.x window. Florian's working build ([`florianL21/zigbee-co2-sensor`](https://github.com/florianL21/zigbee-co2-sensor)) uses 1.5.1 + sleep-enable, which is why his pairs everywhere and ours doesn't.
>
> Testing in progress — full report once verified. Use `main` if you want a working build today.

![Build](https://github.com/petarivanov-msft/hivekit/actions/workflows/build.yml/badge.svg)

ESP32-C6 / H2 Zigbee sensor framework with native Zigbee2MQTT support.

> **Status: Phase 0 done (TBD after first flash), Phase 1 in progress.**
> Not for general use yet — APIs are stabilising.

---

## Goal

Make it trivial to build a Zigbee sensor on cheap ESP32-C6 hardware and have it work natively with **Zigbee2MQTT** out of the box — no coordinator-specific tricks, no unsupported device warnings.

The gap this fills:
- ESPHome's Zigbee component (2026.5) supports C6 binary sensors only — no CO2 / temp / humidity yet
- One-off GitHub projects exist but reinvent the wheel per sensor; none target Z2M natively
- No open project combines: shared library + sensor catalogue + bundled Z2M converters + OTA + web flasher

---

## Quickstart for developers

### SCD40 on XIAO ESP32-C6

**Hardware**: Seeed XIAO ESP32-C6 + Adafruit SCD-41 breakout
```
SDA → GPIO22
SCL → GPIO23
VCC → 3V3
GND → GND
```

**Build**:
```bash
# Requires ESP-IDF v5.5 (see sensors/scd40-c6/README.md for setup)
cd sensors/scd40-c6
idf.py set-target esp32c6
idf.py update-dependencies
idf.py build
```

**Flash**:
```bash
# Hold BOOT button, plug USB, release BOOT
idf.py -p /dev/ttyUSB0 flash monitor
```

**Z2M converter**:
```bash
cp converters/hivekit-scd40.js /config/zigbee2mqtt/external_converters/
# Restart Z2M, then enable Permit Join
```

Full guide: [`sensors/scd40-c6/README.md`](sensors/scd40-c6/README.md)

---

## Roadmap

| Phase | What | Status |
|---|---|---|
| 0 — PoC | SCD40 on C6 pairs with Z2M, 3 sensor values appear | 🟡 Hardware validation pending |
| 1 — MVP | `hivekit` lib, SCD40 firmware, CI, Z2M converter | 🔵 In progress |
| 2 — Catalogue | BME280, SHT40, PIR, reed switch; H2 support | ⚪ Planned |
| 3 — OTA + polish | Web flasher, OTA, community | ⚪ Planned |

See [`docs/roadmap.md`](docs/roadmap.md) for the full breakdown.

---

## Repo Layout

```
hivekit/
├── components/
│   └── hivekit/          # Shared ESP-IDF component (core, button, LED, reporting)
├── sensors/
│   └── scd40-c6/         # SCD40 + CO2 + temp + humidity firmware for XIAO C6
├── converters/
│   └── hivekit-scd40.js  # Z2M external converter (drop into external_converters/)
├── docs/                 # Architecture, plans, specs
├── flasher/              # Web flasher (Phase 1 — not yet built)
└── .github/workflows/    # CI — builds all firmware targets
```

---

## Architecture

**Option A: thin shared component + per-sensor firmware**. See [`docs/architecture.md`](docs/architecture.md).

Key design decisions:
- One ESP-IDF project per sensor+board combo (small binaries, easy to understand)
- Shared `hivekit` component handles all Zigbee boilerplate
- ESP-Zigbee-SDK v2.x only (Apache-2.0, `ezb_*` APIs, no ZBOSS binary blob)
- Z2M identity: `manufacturerName="HiveKit"`, `modelIdentifier="hk-scd40-c6"` etc.

---

## License

Apache-2.0. ESP-Zigbee-SDK v2.0 is Apache-2.0, so pre-built firmware distribution is clean.

---

## Credits

- [florianL21/zigbee-co2-sensor](https://github.com/florianL21/zigbee-co2-sensor) — SCD-4x on C6 with web flasher (ZHA only) — inspired wiring approach
- [Espressif/esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk) — Zigbee stack + examples
- [Sensirion/embedded-i2c-scd4x](https://github.com/Sensirion/embedded-i2c-scd4x) — reference SCD4x I²C driver
