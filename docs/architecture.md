# Architecture

**Decision**: Option A — thin shared `hivekit` ESP-IDF component + per-sensor firmware projects + pre-built binaries + web flasher.

See `~/.openclaw/workspace/projects/c6-zigbee/architecture-decision.md` for full pros/cons matrix.

## Why not YAML config (ESPHome-style)?

- ESPHome already does this — don't compete on their terms
- Code generation pipelines add CI complexity and slow contributor onboarding
- The "ESPHome experience" we want = web flasher + pre-built binaries, not YAML

## Repo layout (planned)

```
hivekit/
├── components/
│   └── hivekit/              # shared ESP-IDF component
│       ├── include/
│       │   └── hivekit.h
│       ├── hivekit_core.c
│       ├── hivekit_ota.c
│       └── CMakeLists.txt
├── sensors/
│   ├── scd40-c6/             # one project per sensor+board combo
│   │   ├── main/
│   │   ├── CMakeLists.txt
│   │   ├── sdkconfig.defaults
│   │   └── partitions.csv
│   ├── bme280-c6/
│   └── sht40-c6/
├── converters/               # Z2M external converters
│   ├── hivekit-scd40.js
│   ├── hivekit-bme280.js
│   └── manifest.json
├── flasher/                  # GitHub Pages — web flasher
│   ├── index.html
│   └── manifests/
├── docs/                     # mkdocs site
└── .github/
    └── workflows/
        ├── build.yml
        └── release.yml
```

## Naming for Z2M

Each firmware sets:
- `manufacturerName`: `"HiveKit"`
- `modelIdentifier`: `"hk-<sensor>-<board>"` e.g. `"hk-scd40-c6"`

This lets Z2M auto-match the right converter from `converters/`.
