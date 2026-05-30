# HiveKit PIR-C6

> **⚠️ UNTESTED — community verification welcome.**
> Flash and report results: <https://github.com/petarivanov-msft/hivekit/issues>

Motion sensor firmware for **Seeed XIAO ESP32-C6** + a PIR module.
Reports binary occupancy state (`occupied` / `unoccupied`) over Zigbee using
the **ZCL Occupancy Sensing cluster** (0x0406) and pairs natively with
Zigbee2MQTT via the included external converter.

---

## Recommended modules

### AM312 (✅ recommended)
- **Supply voltage:** 2.7–12 V (3.3 V direct from XIAO 3V3 pin — no level shifter needed)
- **Logic level:** 3.3 V — safe for direct ESP32-C6 GPIO connection
- **Quiescent current:** ~6 µA — very low standby draw
- **Hold time:** ~2 s (fixed, not adjustable)
- **Sensitivity:** fixed
- **Package:** TO-18 metal can, 3 legs (VCC, OUT, GND)

### HC-SR501
- **Supply voltage:** 5–12 V (**5 V minimum** — do NOT power from XIAO 3V3 pin)
- **Logic level OUT:** varies by silicon variant; most are 3.3 V-safe, but verify yours
- **Quiescent current:** ~65 µA
- **Hold time:** adjustable via **Tx** trimmer pot (0.5–300 s)
- **Sensitivity:** adjustable via **Sx** trimmer pot
- **Trigger mode:** jumper selectable — single trigger (H) or repeating trigger (L);
  use **H (single)** for Zigbee reporting (prevents endless occupied state)

> If using HC-SR501: power VCC from the XIAO 5V/VUSB pin (or an external 5V source),
> not from 3V3.

---

## Wiring

```
PIR Module   →   XIAO ESP32-C6
──────────────────────────────
VCC          →   3V3  (AM312)  /  5V (HC-SR501, via VUSB pin)
GND          →   GND
OUT          →   GPIO2  (default; change via Kconfig CONFIG_HIVEKIT_PIR_GPIO)
```

GPIO2 is the default. To use a different GPIO, set `CONFIG_HIVEKIT_PIR_GPIO`
in `sdkconfig.defaults` or via `idf.py menuconfig → HiveKit PIR Sensor`.

---

## HC-SR501 pot guidance

| Pot label | Function | Recommended setting |
|-----------|----------|-------------------|
| **Sx** | Sensitivity | Start at midpoint; increase for longer detection range |
| **Tx** | Hold time (retrigger delay) | Turn fully **anti-clockwise** for minimum (~2–5 s) to avoid prolonged occupied state |
| **Trigger mode jumper** | H = single trigger, L = repeating | Set to **H** |

---

## No deep sleep

This firmware does **not** use deep sleep. A PIR sensor must be continuously
powered and the GPIO interrupt service must remain active to detect motion events.
Deep sleep disables GPIO monitoring and would cause missed detections.

Power draw: ~50 µA (AM312 quiescent) + ESP32-C6 active-mode current (~15–25 mA
depending on Zigbee radio duty cycle). For battery-powered deployments, consider
USB power banks or mains power rather than coin cells.

---

## Zigbee2MQTT integration

1. Copy `converters/hivekit-pir.js` to your Z2M data folder
   (`<z2m-data>/external_converters/hivekit-pir.js`).
2. Add to `configuration.yaml`:
   ```yaml
   external_converters:
     - hivekit-pir.js
   ```
3. Restart Z2M.
4. Flash this firmware and pair the device (hold BOOT button 3 s).

Z2M will expose:
| Property | Type | Values |
|----------|------|--------|
| `occupancy` | binary | `true` (motion), `false` (no motion) |

---

## ZCL details

- Cluster: **0x0406** Occupancy Sensing
- Attribute 0x0000 `Occupancy`: bitmap8 (bit 0 = occupied)
- Attribute 0x0001 `OccupancySensorType`: `0` = PIR
- Reporting: event-driven on state change, plus a periodic keepalive every
  `CONFIG_HIVEKIT_PIR_KEEPALIVE_INTERVAL_S` seconds (default 300 s)

---

## Configuration (Kconfig)

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_HIVEKIT_PIR_GPIO` | `2` | GPIO connected to PIR OUT pin |
| `CONFIG_HIVEKIT_PIR_ACTIVE_HIGH` | `y` | `n` for active-low PIR modules |
| `CONFIG_HIVEKIT_PIR_DEBOUNCE_MS` | `250` | Minimum ms between state change reports |
| `CONFIG_HIVEKIT_PIR_KEEPALIVE_INTERVAL_S` | `300` | Keepalive report interval (seconds) |

Run `idf.py menuconfig → HiveKit PIR Sensor` to adjust.

---

## Build & flash

```bash
cd sensors/pir-c6
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Or use the [web flasher](https://petarivanov-msft.github.io/hivekit/).
