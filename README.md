# HiveKit

ESP32-C6 / H2 Zigbee sensor framework with native Zigbee2MQTT support.

![Build](https://github.com/petarivanov-msft/hivekit/actions/workflows/build.yml/badge.svg)

---

## Quickstart — no terminal

Flash a HiveKit sensor from your browser:

👉 **[https://petarivanov-msft.github.io/hivekit/](https://petarivanov-msft.github.io/hivekit/)**

Open in Chrome, Edge, or Opera on a **desktop**. Plug in your XIAO ESP32-C6, pick a sensor,
click **Install**. Done in ~60 seconds.

> Dev/pre-release builds: [https://petarivanov-msft.github.io/hivekit/dev/](https://petarivanov-msft.github.io/hivekit/dev/)

---

## Quickstart — developers

```bash
git clone https://github.com/petarivanov-msft/hivekit.git && cd hivekit
cd sensors/scd40-c6
idf.py set-target esp32c6
idf.py update-dependencies && idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Requires ESP-IDF v5.5. Full setup: [`sensors/scd40-c6/README.md`](sensors/scd40-c6/README.md).

---

## Repo layout

| Path | Contents |
|------|----------|
| `components/hivekit/` | Shared ESP-IDF component — Zigbee init, clusters, reporting |
| `sensors/scd40-c6/` | SCD40 CO2 + temp + humidity firmware for XIAO C6 |
| `converters/` | Z2M external converters (drop into `external_converters/`) |
| `flasher/` | Web flasher (GitHub Pages) |
| `docs/` | Architecture, specs, plans |
| `.github/workflows/` | CI — build + release pipeline |

---

## Branching

- **`main`** — stable. Tagged releases. Always safe to flash.
- **`dev`** — integration. Pre-release tags (`-beta`, `-rc`) cut from here.
- **`experiment/*`** — throwaway. Merge into `dev` once proven; delete after merge.

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the release flow.

---

## Roadmap

See [`docs/roadmap.md`](docs/roadmap.md).

---

## License

Apache-2.0. See [`LICENSE`](LICENSE).

---

## Credits

- [florianL21/zigbee-co2-sensor](https://github.com/florianL21/zigbee-co2-sensor) — SCD-4x on C6 with web flasher (ZHA only)
- [Espressif/esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk) — Zigbee stack + examples
- [Sensirion/embedded-i2c-scd4x](https://github.com/Sensirion/embedded-i2c-scd4x) — SCD4x I²C driver
