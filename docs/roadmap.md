# HiveKit Roadmap

Mirrors `~/.openclaw/workspace/projects/c6-zigbee/mvp-roadmap.md` (research artifact). See that file for full task list.

## Phase 0 — PoC (in progress)
Hardware validation only. See [`phase0-flash-plan.md`](phase0-flash-plan.md).

## Phase 1 — MVP ✅
- `components/hivekit/` shared library (init, clusters, reporting, sleep, OTA)
- `sensors/scd40-c6/` first sensor firmware
- `converters/hivekit-scd40.js` Z2M converter
- `flasher/` GitHub Pages web flasher — **live at https://petarivanov-msft.github.io/hivekit/**
- GitHub Actions CI (build + release pipeline, merged-bin, dual-channel Pages)
- Docs site

## Phase 2 — Catalogue
- BME280, SHT40, PIR, reed switch, BME680, ESP32-H2 variants

## Phase 3 — Polish
- OTA hardening, battery profiling, optional YAML config builds

## Phase 4 — Community traction
- Z2M upstream PRs, HACS listing, KiCad reference PCB
