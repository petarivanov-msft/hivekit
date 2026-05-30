# HiveKit Web Flasher — Developer Guide

Live URL (stable): https://petarivanov-msft.github.io/hivekit/
Dev URL: https://petarivanov-msft.github.io/hivekit/dev/

---

## How it works

The flasher is a single-page HTML app in `flasher/` that uses
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) — a web component that
drives the WebSerial API to flash firmware directly from the browser.

Key files:

| File | Purpose |
|------|---------|
| `flasher/index.html` | Main UI — sensor dropdown, Install button, instructions |
| `flasher/style.css` | Stylesheet (brand palette, dark/light mode, responsive) |
| `flasher/manifest-scd40-c6.json` | Stable channel manifest for the SCD40 sensor |
| `flasher/manifest-scd40-c6-dev.json` | Dev channel manifest for the SCD40 sensor |
| `flasher/manifest-template.json` | Template for adding new sensors |
| `flasher/assets/logo.svg` | HiveKit logo SVG |
| `flasher/assets/favicon.svg` | Browser tab icon |

The `__VERSION__` and `__ASSET_BASE__` placeholders in the manifests and
`index.html` are substituted at deploy time by the `release.yml` workflow using
`sed`. The `__CHANNEL__` placeholder sets the channel display in the footer.

---

## Dual-channel publishing

Two channels are served from GitHub Pages:

| Channel | URL subpath | Trigger |
|---------|------------|---------|
| **Stable** | `/hivekit/` (root) | Push of `v*` tags excluding `-alpha`/`-beta`/`-rc` |
| **Dev** | `/hivekit/dev/` | Push to `dev` branch OR pre-release tags (`-beta`, etc.) |

Dev builds create an ephemeral pre-release tag `dev-<shortsha>` automatically,
upload the merged firmware binary as a release asset, and deploy to the `/dev/`
subpath without clobbering the stable root.

---

## Adding a new sensor

### Step 0: Add shared Zigbee bindings

Before writing sensor-specific code, check whether your sensor's ZCL clusters
are already handled in the shared HiveKit core:

- **`components/hivekit/hivekit_core.c`** — registers Zigbee endpoints, cluster
  attribute tables, and reporting callbacks. Add or extend endpoint descriptors
  here for your sensor's clusters (e.g. 0x0402 temperature, 0x0405 humidity,
  0x0403 pressure).
- **`components/hivekit/include/hivekit.h`** — public API: `hivekit_report_*`
  helpers and the `hivekit_sensor_data_t` union. Add your sensor's data type
  and any new report function declarations here.

These two files were the most error-prone part of adding BME280 — easy to miss
because they live outside `sensors/` but must be touched for every new sensor.
Get them right before touching anything in `sensors/`.

### 1. Add the firmware

Follow the existing `sensors/scd40-c6/` pattern. Your sensor lives in
`sensors/<sensor-id>/` with its own `CMakeLists.txt`, `sdkconfig`, and
`partitions.csv`.

### 2. Add a CI build job

In `.github/workflows/build.yml`, add a new job:

```yaml
build-<sensor-id>:
  name: Build <sensor-id> (ESP32-C6, IDF v5.5)
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
      with: { submodules: recursive }
    - uses: espressif/esp-idf-ci-action@v1
      with:
        esp_idf_version: v5.5
        target: esp32c6
        path: sensors/<sensor-id>
        command: idf.py build
```

Also update `release.yml` to add a `merge_bin` step for your sensor.

### 3. Create the manifest

Copy `flasher/manifest-template.json` to `flasher/manifest-<sensor-id>.json`.
Replace `__SENSOR__` with your sensor ID (e.g. `bme280-c6`). Leave `__VERSION__`
and `__ASSET_BASE__` as-is — the workflow substitutes them at deploy time.

For the dev manifest, copy to `flasher/manifest-<sensor-id>-dev.json`.

### 4. Add to the dropdown

In `flasher/index.html`, add a new `<option>` inside `<select id="sensor-select">`:

```html
<option
  value="bme280-c6"
  data-manifest="manifest-bme280-c6.json"
  data-tested="false"
>
  BME280 (XIAO ESP32-C6) — temp + humidity + pressure ☆ Untested
</option>
```

Also add an entry to the `sensorMeta` object in the inline `<script>`:

```js
'bme280-c6': {
  desc: 'Atmospheric pressure, temperature, and humidity sensor.',
  tested: false
}
```

For untested sensors:
- Use `data-tested="false"` (do **not** add `disabled` — the option should be selectable)
- The badge updates automatically to “☆ Untested”
- An amber warning banner (`#untested-warning`) is shown when an untested sensor is selected
- The README in `sensors/<sensor-id>/` should have a clear **UNTESTED** notice
- Change `data-tested="false"` to `"true"` once real-hardware verification is confirmed

Change `data-tested="false"` to `"true"` once you've flashed and verified the
sensor on real hardware with Zigbee2MQTT.

### 5. Test locally

```bash
cd flasher
python3 -m http.server 8080
# Open http://localhost:8080 in Chrome
```

### 6. Ship it

Open a PR targeting `main`. The `build.yml` CI will validate the firmware
compiles. After merge, push a stable tag to trigger the full release pipeline.

---

## Testing the dev channel

1. Push a commit to the `dev` branch (or push a pre-release tag like `v0.3.4-beta.1`).
2. The `release.yml` workflow creates a `dev-<shortsha>` pre-release tag,
   uploads the merged binary, and deploys to `/dev/`.
3. Open https://petarivanov-msft.github.io/hivekit/dev/ — you'll see the amber
   "🧪 Dev channel" banner.
4. Flash and test. Once happy, cut a stable tag to promote to root.

---

## Known caveats

- **WebSerial requires HTTPS** — Pages serves over HTTPS by default (`*.github.io`).
  If you move to a custom domain, ensure the cert is valid.
- **Browser support** — Chrome, Edge, Opera (desktop only). Firefox and Safari
  do not implement Web Serial. The flasher shows a friendly notice on unsupported browsers.
- **ESP Web Tools v10** — pinned to `^10` via `unpkg.com`. If Espressif releases
  a breaking v11, update the CDN URL in `index.html`.
- **First-time flash failure** (known: "Invalid head of packet 0x45") — usually
  means the XIAO didn't enter bootloader mode correctly. Retry the BOOT+RESET
  sequence and try again.
