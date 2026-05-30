# Task 7 — TC Link Key Investigation

**Date:** 2026-05-29
**Branch:** dev (HEAD baa2f4de)
**Status:** Research-only. Recommendation: **(A) NO CHANGE.**

## Summary

The v2 esp-zigbee-sdk End-Device static library has the standard Zigbee
trust-centre link key (`5A 69 67 42 65 65 41 6C 6C 69 61 6E 63 65 30 39`,
ASCII `"ZigBeeAlliance09"`) baked in as the default preconfigured global
link key. Neither HiveKit nor the florianL21 reference call any
`ezb_secur_set_global_link_key()` / `esp_zb_secur_TC_standard_*` API — and
none of Espressif's own ZED examples do either. Z2M's default expectation
matches that key out of the box. The `status=0x03 NETWORK_NOT_FOUND`
symptom was about beacon/network discovery (addressed by Tasks 1–6), not a
TC link key mismatch.

## What the v2 SDK does by default

- The default global TC link key is the standard Zigbee Alliance key.
  Evidence: `strings libesp-zigbee-core.zed.release.a` returns
  `ZigBeeAlliance09` (and `ZigBeeAlliance18` for the R23 dynamic-key flow),
  both inside `aps_secur.c.obj`.
- Public API to override it (in v2 namespace): `ezb_secur_set_global_link_key(const uint8_t *key)`
  declared in `components/esp-zigbee-lib/include/ezbee/secur.h`.
- Legacy compat macros that all alias to the same call:
  ```c
  #define esp_zb_secur_TC_standard_distributed_key_set(key)  ezb_secur_set_global_link_key(key)
  #define esp_zb_secur_TC_standard_preconfigure_key_set(key) ezb_secur_set_global_link_key(key)
  ```
  (`components/esp-zigbee-lib/include/compat/esp_zigbee_secur.h:40-41`).
- Migration guide explicitly says: "The multiple global link key is not
  supported yet, please use `ezb_secur_set_global_link_key` to set the
  global link key" — i.e. one global key, and if you don't set one the
  SDK uses its built-in default.
- **No Kconfig knob exists** for the TC link key in the SDK (`components/esp-zigbee-lib/Kconfig`
  only exposes `ZB_ENABLED`, role choice, radio choice, `ZB_DEBUG_MODE`). No
  `CONFIG_ZB_SECUR_PRECONFIGURED_TC_LINK_KEY` symbol.
- All v2 SDK end-device examples that target Z2M-compatible centralized
  networks call `ezb_aps_secur_enable_distributed_security(false)` and
  **do not** touch the TC link key:
  - `examples/home_automation_devices/temperature_sensor/main/temperature_sensor.c:195`
  - `examples/home_automation_devices/on_off_light/main/on_off_light.c:158`
  - `examples/home_automation_devices/on_off_switch/main/on_off_switch.c:243`
  - `examples/home_automation_devices/color_dimmable_light/main/color_dimmable_light.c:223`
  - `examples/sleepy_devices/light_sleep_end_device/main/light_sleep_end_device.c:264`
  - `examples/sleepy_devices/deep_sleep_end_device/main/deep_sleep_end_device.c:307`
  - `examples/ota_upgrade/ota_client/main/ota_client.c:265`
  Only `esp-zigbee-console` exposes a user CLI to override the key
  (`cli_cmd_bdb.c:555`), confirming it's an opt-in override, not a required
  step.

## What hivekit-ref does

- `main/zigbee.h`: only `#define INSTALLCODE_POLICY_ENABLE false`. No
  `ezb_secur_*` / `esp_zb_secur_*` calls anywhere in `main/`.
- `sdkconfig.defaults`: sets `CONFIG_MBEDTLS_KEY_EXCHANGE_ECJPAKE=y` and
  `CONFIG_MBEDTLS_ECJPAKE_C=y` (alongside `CONFIG_MBEDTLS_CMAC_C=y` and
  `CONFIG_MBEDTLS_SSL_PROTO_DTLS=y`), but no key plumbing in code.
- Conclusion: relies on SDK default key.

## What HiveKit currently does

- `sensors/scd40-c6/main/main.c:65-90`: `HIVEKIT_ZED_CONFIG()` sets
  `install_code_policy = false` and `device_type = END_DEVICE`. No TC key
  call.
- `sensors/scd40-c6/main/main.c:142`: calls
  `ezb_aps_secur_enable_distributed_security(false)` before
  `esp_zigbee_start(false)` — matches every Z2M-targeting SDK example.
- `grep -i "tc.link|link_key|ZigBeeAlliance|5A69|secur|tclk"` across
  `components/hivekit/` and `sensors/scd40-c6/` returns zero hits (other
  than the `enable_distributed_security` call above).
- `sensors/scd40-c6/sdkconfig.defaults`: no MbedTLS / TC key flags. Z2M
  compatibility relies entirely on SDK defaults + the
  `enable_distributed_security(false)` call.
- Conclusion: identical posture to the reference — relies on SDK default
  key.

## Is ECJPAKE required for the standard TC link key path?

No. EC-J-PAKE (Elliptic-Curve J-PAKE) is the password-authenticated key
exchange used by:

1. The Zigbee BDB Key Establishment Cluster (KEC) for **install-code
   based** provisioning (BDB §10), and
2. R23 dynamic link-key negotiation.

It is **not** used to derive or transport the standard preconfigured
global TC link key — that path is pure AES-CCM\* and only needs
`CONFIG_MBEDTLS_AES_C` + `CONFIG_MBEDTLS_CMAC_C` (and the latter only
because Zigbee key transport uses AES-MMO/CCM\* primitives that the
SDK's mbedTLS shim may pull in).

Why hivekit-ref enables ECJPAKE anyway: the reference inherited it from
the older esp-idf Zigbee example template. It's harmless (a few KB of
flash) but not load-bearing for current HiveKit pairing. HiveKit does
**not** currently set those flags and is joining the network logic
without them — confirming they are not required for the TCLK path.

If we later add install-code support or move to R23, we'll need to
re-enable them.

## Recommendation: (A) NO CHANGE

The default TC link key in the v2 SDK is already `ZigBeeAlliance09`,
which is exactly what Z2M expects out of the box. The `0x03
NETWORK_NOT_FOUND` symptom is a discovery-stage failure (no beacons of an
acceptable network observed) and is not produced by a key mismatch — a
key mismatch would surface later as e.g.
`EZB_BDB_STATUS_TCLK_EX_FAILURE`, `TC_REJECTED`, or a join-then-leave
loop after the device receives the network key.

With Tasks 1–6 landed (distributed security disabled, channel mask
covering 11–26, install-code policy off, NVS partitions correct, etc.),
the remaining work is bench validation. If after Tasks 1–6 the device
gets to network discovery and joins but immediately leaves with a TCLK
error, **then** file a follow-up to explicitly call
`ezb_secur_set_global_link_key()` with the literal `ZigBeeAlliance09`
bytes before `esp_zigbee_start()` — but do not pre-emptively add that
call now, as it would mask any genuine future SDK behaviour change and
duplicates what the SDK is already doing.

### Diagnostic if pairing still fails after Tasks 1–6

Look for these in the device log to disambiguate:

| Symptom | Cause |
|---|---|
| `status=0x03 NETWORK_NOT_FOUND`, no beacons logged | Discovery — channel mask / antenna / RF (not TCLK) |
| Joined → received key → immediate leave with `TCLK_EX_FAILURE` | TCLK mismatch — then add explicit `ezb_secur_set_global_link_key()` |
| `bdb_comm` says key exchange not required but coordinator demands | `ezb_secur_set_tclk_exchange_required(true)` |

## No code change made.
