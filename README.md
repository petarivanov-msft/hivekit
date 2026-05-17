# HiveKit

ESP32-C6 / H2 Zigbee sensor framework, native Zigbee2MQTT compatible.

> **Status: early — Phase 0 PoC in progress.** Not for general use yet.

## Goal

Make it trivial to build a custom Zigbee sensor on cheap ESP32-C6 / H2 hardware and pair it with **Zigbee2MQTT** out of the box.

The gap this fills:
- ESPHome's Zigbee component (2026.5) supports C6 but only binary sensors — no CO2 / temp / humidity yet.
- One-off GitHub projects exist for specific sensors but each reinvents the wheel.
- No shared library + sensor catalogue + bundled Z2M converters + OTA + web flasher exists.

## Planned features

- Shared `hivekit` ESP-IDF component (cluster setup, reporting, sleep, OTA, factory reset)
- Per-sensor firmware projects (SCD40, BME280, SHT40, PIR, reed, etc.)
- Pre-built binaries per board + sensor combo (no toolchain install needed)
- Web flasher (esptool-js) — flash from browser
- Bundled Z2M external converters (drop-in)
- OTA via Zigbee cluster (Phase 1/2)

## Roadmap

See [`docs/roadmap.md`](docs/roadmap.md). TL;DR:

| Phase | Effort | Status |
|---|---|---|
| 0 — PoC: SCD40 on C6 → Z2M | 1 weekend | 🟡 In progress |
| 1 — MVP: hivekit lib, CI, web flasher, OTA | 2 weekends | ⚪ |
| 2 — Catalogue: BME280, SHT40, PIR, reed | ~6 weeks p/t | ⚪ |
| 3 — Polish + community | TBD | ⚪ |

## License

Apache-2.0 (planned). ESP-Zigbee-SDK v2.0 is Apache-2.0 so binary distribution is clean.

## Credits

PoC stage builds on prior art:
- [florianL21/zigbee-co2-sensor](https://github.com/florianL21/zigbee-co2-sensor) — SCD-4x on C6 with web flasher (ZHA)
- [xmow49/ESP32H2-Zigbee-Demo](https://github.com/xmow49/ESP32H2-Zigbee-Demo) — base implementation
