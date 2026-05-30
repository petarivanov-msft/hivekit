# HiveKit — Pairing & Roaming Fix Plan

> Branch: `dev`  ·  Created: 2026-05-29  ·  Status: planning
> Reference framework: `florianL21/zigbee-co2-sensor` (mirrored at `/home/windows/code/hivekit-ref`)

## Goal

> "The main issue was pairing and also hopping on other routers. The repo we used as a framework was fine. We need to keep it in V2 so keep that in mind."

In plain terms: the SCD40-C6 firmware as it stands today fails to pair reliably (loops `status=0x03 NETWORK_NOT_FOUND` for many minutes, sometimes never joins) and after joining it does not hop between Zigbee routers — it stays bound to the coordinator and goes silent when carried out of range. The reference framework we forked behaves correctly on both fronts. We need to bring the firmware behaviour back in line with the reference **without** dropping off esp-zigbee-sdk v2.

## Out of scope

- **No SDK downgrade.** Stay on `espressif/esp-zigbee-lib ~2.0.0`. The reference happens to be on 1.5.1; use it as a behavioural oracle only.
- **No ESP-IDF version bump.** Keep v5.5.x. If IDF turns out to be the cause, that's its own future task with its own justification.
- **No changes to Zigbee2MQTT** (converter, coordinator config, channel, permit-join behaviour) unless a task below produces hard evidence that the firmware is innocent.
- **No new sensor work** (SCD40 readings, LED patterns, OTA, button UX). This plan is firmware Zigbee networking only.
- **No release tag.** That happens once the device is confirmed to pair and roam reliably on real hardware.

## Assumptions

- Branch `dev` exists and is the integration branch for this work. Each task lands as its own commit on `dev`. Controller handles eventual push/PR.
- The Path B Phase-3 redo (manual endpoint construction for non-temperature sensors) is **deferred** — it is unrelated to pairing/roaming and the current `ezb_zha_create_temperature_sensor()` + cluster-patch approach is good enough for SCD40 to attempt to join.
- We believe (from `STATE.md`) that as of commit `cf0669d` the firmware did pair and report once on Monday morning before regressing. So the byte sequence on the air is "good enough" once we *do* join — the open issues are: getting the join, and surviving network topology changes (router hops).
- Hardware: Seeed XIAO ESP32-C6. The XIAO C6 ships with both an on-PCB chip antenna and a U.FL connector and uses a GPIO-driven RF switch (GPIO3 = power, GPIO14 = select). **The current firmware never touches these pins.** This is almost certainly hurting RSSI and is a prime suspect for both poor pairing and poor roaming. (Reference does call `configure_internal_antenna()` for the same chip.)
- Flashing and testing each candidate on real hardware; each task must give a single-commit "flash this, expected vs observed" check that takes ≤5 min on the bench.

## Diff inventory — what differs from reference (relevant subset)

Surveyed `components/hivekit/hivekit_core.c`, `sensors/scd40-c6/main/main.c`, `sensors/scd40-c6/sdkconfig.defaults` vs reference's `main/main.c`, `main/zigbee_task.c`, `main/zigbee.h`, `main/sdkconfig.defaults`. Items that plausibly affect pair/roam behaviour:

| # | Topic | Reference (works) | HiveKit (broken) |
|---|---|---|---|
| A | Internal antenna select | `configure_internal_antenna()` toggles GPIO3 + GPIO14 in `app_main` before anything else | Never called — RF switch left in power-on default (frequently the U.FL/external path, no antenna attached → ~−20 to −30 dBm penalty) |
| B | `keep_alive` (ED→parent) | `ED_KEEP_ALIVE = 180000` (180 s) | `4000` (4 s) — relentlessly polls; if a router is slow the ED times out and treats parent as lost |
| C | Post-join long-poll interval | `zb_zdo_pim_set_long_poll_interval(ED_KEEP_ALIVE)` called from `ESP_ZB_BDB_SIGNAL_STEERING` SUCCESS branch | No equivalent call after join → SDK default short poll remains |
| D | `ed_timeout` (aging) | `ESP_ZB_ED_AGING_TIMEOUT_64MIN` | `EZB_NWK_ED_TIMEOUT_64MIN` (same intent — verify the v2 enum actually maps to 64 min, not a smaller value) |
| E | Primary channel mask | `ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK` (scan all 11–26) | Primary = ch11 only; secondary = 11–26. If Z2M is on anything but 11 first scan wastes time and the device gives up before secondary kicks in cleanly |
| F | Steering retry mechanism | `esp_zb_scheduler_alarm(... STEERING, 1000)` — schedules on the Zigbee timer | `vTaskDelay(pdMS_TO_TICKS(3000))` **inside the app signal handler** → blocks the Zigbee main loop for 3 s, causing beacon/ACK drops |
| G | Init failure recovery | `esp_restart()` after 2 s on `DEVICE_FIRST_START` / `REBOOT` failure | Inline `vTaskDelay(1000)` + retry → same blocking problem |
| H | Sleep / PM coupling | `esp_pm_configure(...light_sleep=true)` **and** `esp_zb_sleep_enable(true)` **and** `esp_zb_sleep_set_threshold(...)` — full coherent stack | `CONFIG_IEEE802154_SLEEP_ENABLE=y` set but `CONFIG_PM_ENABLE` commented out and no `esp_zb_sleep_*` API calls. Half-configured sleep is the worst of both worlds: MAC may sleep without PM scheduling wake-ups properly |
| I | TC link key install | n/a (v1 SDK auto-installs default TC link key for ZB3 join) | v2 SDK requires explicit install (`ezb_aps_secur_*`?). Repo had a `4daa43a` "TC link key fix" commit that was force-discarded 2026-05-19. Z2M expects the default Zigbee 2015 TC link key (`5A:69:67:42:65:65:41:6C:6C:69:61:6E:63:65:30:39`) |
| J | BDB comm status cast | Uses `signal_struct->esp_err_status == ESP_OK` (top-level err) | Casts `ezb_app_signal_get_params(app_signal)` to `ezb_bdb_comm_status_t *` and compares to `EZB_BDB_STATUS_SUCCESS`. If the v2 param layout differs from our header assumption the success branch never runs → factory-new device never starts steering |
| K | Power-down behaviour | Has `configure_internal_antenna` + matches PM | Has nothing special on cold boot |

We do NOT yet know which one of A–K is the dominant cause. The reference's v1 SDK shadows several of these issues (TC link key is automatic, channel scan and PM defaults are different), so a direct line-by-line transplant isn't possible. The tasks below isolate each suspect for individual A/B testing on real hardware in a single flash cycle.

## Tasks

> All tasks land on `dev`. Each is independently flashable. Acceptance criteria are written as a single "flash → observe → record" loop that takes ≤5 min. Each task ends with an entry appended to `docs/plans/pairing-and-roaming-results.md` (controller will create the file on first commit).

### Task 1 — XIAO C6 internal-antenna selection (HIGH priority, smallest blast radius)

**Why:** Suspect A. RF switch is the cheapest single behavioural gap to close. Reference uses identical chip on identical board and explicitly sets `GPIO3=0` (RF amp on) + `GPIO14=0` (internal antenna). HiveKit leaves both floating/default.

**Change:**
- Add `hivekit_board_xiao_c6_init_antenna(void)` in a new file `components/hivekit/hivekit_board.c` (+ matching declaration in `include/hivekit.h`). Implementation mirrors the reference `configure_internal_antenna()`.
- Call it from `sensors/scd40-c6/main/main.c` `app_main()` as the very first line (before NVS).
- Guard with `CONFIG_HIVEKIT_BOARD_XIAO_C6_ANTENNA` (default `y`) so other boards aren't poked.

**Acceptance criteria:**
1. `idf.py build` succeeds for `sensors/scd40-c6`.
2. Boot log shows `hivekit_board: internal antenna selected (GPIO3=0, GPIO14=0)` once.
3. **Bench check:** With a fresh-erased ESP placed 5 m from coordinator with one wall between, device joins on first steering attempt (< 30 s). Without this commit, same setup fails for ≥5 minutes. Record both runs in the results log.

**Files:**
- NEW `components/hivekit/hivekit_board.c`
- NEW `components/hivekit/include/hivekit_board.h` (or extend `hivekit.h`)
- EDIT `components/hivekit/CMakeLists.txt`, `components/hivekit/Kconfig`
- EDIT `sensors/scd40-c6/main/main.c`

---

### Task 2 — Stop blocking the Zigbee signal handler on retries

**Why:** Suspects F + G. `vTaskDelay()` inside `hivekit_app_signal_handler` runs on the Zigbee main task and blocks the stack for 1–3 seconds at a time. During that window the MAC layer cannot ACK beacons, send poll requests, or respond to the trust centre. This alone can convert a recoverable transient (slow router ACK, missed beacon) into a hard fail loop.

**Change:**
- Replace both `vTaskDelay() + ezb_bdb_start_top_level_commissioning(...)` patterns with a v2-SDK scheduler-alarm equivalent. In esp-zigbee-sdk v2 this is exposed as `ezb_scheduler_alarm()` or `esp_zigbee_scheduler_alarm()` (verify exact symbol at implementation time — header `ezbee/scheduler.h`).
- If no scheduler alarm is available in v2 (verify before implementing), fall back to an `xTimerCreate(... pdFALSE)` one-shot FreeRTOS timer; the timer callback acquires `esp_zigbee_lock_acquire(portMAX_DELAY)` before calling the BDB API and releases after.
- On `DEVICE_FIRST_START` / `DEVICE_REBOOT` failure, prefer `esp_restart()` after a 2-second one-shot timer (matches reference). Inline-restart is acceptable because the entire process tears down.

**Acceptance criteria:**
1. Build succeeds. No `vTaskDelay()` remains inside `hivekit_app_signal_handler`.
2. Boot log shows `[hivekit_core] Network steering failed (status=0x03), scheduling retry in 3s` followed exactly 3 s later by `[hivekit_core] Zigbee stack ready — starting BDB init` (or steering success). No 3-second gap with zero log output.
3. **Bench check:** With Z2M permit-join open, device joins within the first 1–2 retry cycles instead of the current "many retries". Record retry count for 3 cold-boot attempts.

**Files:**
- EDIT `components/hivekit/hivekit_core.c`

---

### Task 3 — Channel-scan strategy: full mask first, in one call

**Why:** Suspect E. Reference scans all 11–26 in one go. HiveKit's "primary=ch11 / secondary=all" optimisation assumes the SDK will roll secondary into the same network-steering attempt, but v2's `ezb_bdb_set_secondary_channel_set` only kicks in after a *separate* steering retry — and our current steering retry is broken (see Task 2). Until that's all settled, just match the reference: one mask, all channels.

**Change:**
- In `sensors/scd40-c6/main/main.c` `zigbee_main_task()`, replace the two-mask setup with a single `ezb_bdb_set_primary_channel_set(0x07FFF800)` (channels 11–26 = `1 << 11 .. 1 << 26`). Remove the secondary set call.
- Add a Kconfig int `CONFIG_HIVEKIT_ZIGBEE_PRIMARY_CHANNEL_MASK` defaulting to `0x07FFF800` so power users can pin a single channel later.
- Add a one-line comment explaining "match reference's `ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK` behaviour; v2 SDK has no convenience macro for it".

**Acceptance criteria:**
1. Build succeeds.
2. Boot log shows `Primary channel mask: 0x07FFF800 (ch 11-26)`.
3. **Bench check:** On a Z2M coordinator pinned to **channel 20** (temporarily reconfigure Z2M, or use a sniffer to confirm), device pairs within 60 s. Today it does not pair at all unless Z2M is on ch11.

**Files:**
- EDIT `sensors/scd40-c6/main/main.c`
- EDIT `components/hivekit/Kconfig`

---

### Task 4 — Verify and fix BDB signal-status parsing

**Why:** Suspect J. Reference reads `signal_struct->esp_err_status` (top-level esp_err_t) and treats `ESP_OK` as success. HiveKit casts `ezb_app_signal_get_params(...)` to `ezb_bdb_comm_status_t *` and dereferences. **If the v2 SDK actually delivers the BDB status in the top-level `esp_err_status` field — and the params pointer is NULL or points to a different struct (e.g. `ezb_zdo_signal_device_authorized_params_t`) — our success branch never executes, factory-new devices never call `EZB_BDB_MODE_NETWORK_STEERING` and just sit in init forever.** This would explain the worst-case "never joins at all" reports.

**Change:**
- Grep the installed `managed_components/espressif__esp-zigbee-lib/include/ezbee/app_signals.h` for the actual signature/payload of `EZB_BDB_SIGNAL_DEVICE_FIRST_START` and `EZB_BDB_SIGNAL_STEERING`. Document findings in a comment block at the top of `hivekit_core.c`.
- Fix the success-check to match what the SDK actually delivers. Likely the same idiom as v1: use the `esp_err_status` field from the outer signal struct. The hivekit handler signature currently takes a `const ezb_app_signal_t *app_signal` — confirm there's an err-status accessor (`ezb_app_signal_get_status()` or similar). If not, change the signal-handler registration to whichever v2 API exposes the full struct.

**Acceptance criteria:**
1. Build succeeds.
2. With a factory-erased device, log line `[hivekit_core] Factory-new device — starting network steering` MUST appear within 1 s of `Zigbee stack ready — starting BDB init`. Today it sometimes never appears (this is the bug).
3. Re-flashing without erasing NVS: `[hivekit_core] Device reboot — rejoining previous network` appears instead.
4. Document the exact SDK header excerpt that justifies the fix in the commit message.

**Files:**
- EDIT `components/hivekit/hivekit_core.c`

---

### Task 5 — End-device keep-alive and post-join long-poll tuning

**Why:** Suspects B + C + D. With `keep_alive=4000` the ED polls its parent every 4 s and any miss starts an orphan-rejoin clock. With no `set_long_poll_interval` call after join, the SDK default short-poll runs forever, burning bandwidth on a slow router and giving the trust centre many opportunities to flag the device as misbehaving. Together they cause "joins fine on the bench, dies on the kitchen counter" behaviour — the reported "doesn't hop on other routers, just goes silent" symptom. A router with marginal RSSI cannot satisfy a 4 s keep-alive but easily satisfies a 180 s one.

**Change:**
- Bump `keep_alive` in `HIVEKIT_ZED_CONFIG()` from `4000` to `180000` (180 s, matching reference).
- Verify `EZB_NWK_ED_TIMEOUT_64MIN` is exactly 64 minutes (the v2 enum may have been renumbered). If unsure, hard-code the underlying integer to match `ESP_ZB_ED_AGING_TIMEOUT_64MIN` from v1 with a comment.
- In `hivekit_core.c`, on `EZB_BDB_SIGNAL_STEERING` SUCCESS branch, call the v2 equivalent of `zb_zdo_pim_set_long_poll_interval(180000)` (look for `ezb_zdo_pim_set_long_poll_interval` or `esp_zb_zdo_pim_set_long_poll_interval` in the installed SDK — symbol may have moved into `ezbee/zdo.h`). If the symbol doesn't exist in v2, document and leave a TODO; do not block the task on this sub-step.

**Acceptance criteria:**
1. Build succeeds.
2. Boot log after join shows `Long poll interval set to 180000 ms` (or the documented-fallback TODO).
3. **Bench check:** Place device 1 m from a Zigbee router (e.g. Aqara plug) and 10 m + 2 walls from the coordinator. Device must join via the router (Z2M shows `parent: <router IEEE>` not coordinator) and stay reporting for ≥30 minutes without going `Offline`. Today it either joins the coordinator only (and dies when moved) or never joins.

**Files:**
- EDIT `sensors/scd40-c6/main/main.c` (`HIVEKIT_ZED_CONFIG`)
- EDIT `components/hivekit/hivekit_core.c` (signal handler success branch)

---

### Task 6 — Coherent sleep/PM configuration (rip out the half-config)

**Why:** Suspect H. `CONFIG_IEEE802154_SLEEP_ENABLE=y` without `CONFIG_PM_ENABLE` and without `esp_zb_sleep_enable(true)` is an unsupported combination. Best case it is a no-op; worst case the MAC partially shuts down during scans (missed beacons) or between polls (missed router-issued route updates) — both of which match the reported symptoms.

**Change:**
- **Default branch (do this in Task 6):** remove `CONFIG_IEEE802154_SLEEP_ENABLE=y` and `CONFIG_ZB_ENABLE_ZGP=n` from `sdkconfig.defaults`. Add a comment block explaining we will revisit power management as a separate epic *after* pair-and-roam is solid.
- Leave `experiment/sleep-enable` branch alone for future PM work; do not delete it.

**Acceptance criteria:**
1. Build succeeds; `sdkconfig` confirms `CONFIG_IEEE802154_SLEEP_ENABLE` is not set.
2. **Bench check:** With Tasks 1–5 applied, device joins on first attempt AND stays online overnight (≥8 h, ≥48 successful 30-s reports). Today the half-sleep config causes intermittent dropouts even on the bench.
3. Power-consumption regression is **acceptable** for this task — sleep work is a separate epic.

**Files:**
- EDIT `sensors/scd40-c6/sdkconfig.defaults`

---

### Task 7 — Investigate (NOT yet fix) trust-centre link key handling

**Why:** Suspect I. The `4daa43a` "TC link key fix" was force-discarded on 2026-05-19 and we have no notes on whether it was actually necessary. v2 SDK does NOT auto-install the default ZB3 TC link key in every code path; some join scenarios (Z2M with default config) require an explicit install via `ezb_aps_*` APIs. Without it the device can complete association but fail the TC verification step → coordinator drops it and the device retries forever from scratch (presents identically to NETWORK_NOT_FOUND).

**Change (investigation only, no code change on first pass):**
- `git show 4daa43a` in the reflog (`git reflog --all | grep 4daa43a`) — if recoverable, capture the diff into `docs/research/tc-link-key-fix-4daa43a.patch`.
- Read `managed_components/espressif__esp-zigbee-lib/include/ezbee/aps.h` and find any `ezb_aps_secur_*_tc_link_key*` function. Document existence/signature in `docs/research/tc-link-key-v2-api.md`.
- Read `managed_components/espressif__esp-zigbee-lib/examples/` for any example that calls these APIs.
- Decide: does the v2 default code path install the standard ZigBeeAlliance09 TC link key, or do we need an explicit call before `esp_zigbee_start()`?
- Write findings into `docs/research/tc-link-key-v2-api.md` with a recommendation block. If a fix is needed, **spin out Task 7b** at the end of the doc with a specific code patch and acceptance criteria. If not, mark closed with evidence.

**Acceptance criteria:**
1. Two new docs (or one + recovered patch) exist under `docs/research/`.
2. Recommendation clearly states one of: "no change needed (evidence: ...)", "change needed — see Task 7b below", or "cannot determine, needs further investigation (questions: ...)".
3. Commit message: `docs(research): TC link key handling under esp-zigbee-sdk v2`.

**Files:**
- NEW `docs/research/tc-link-key-v2-api.md`
- NEW (conditional) `docs/research/tc-link-key-fix-4daa43a.patch`

---

## Suggested execution order

Test in this order — each task is independently flashable but earlier tasks are higher-value-per-flash:

1. **Task 1** (antenna) — biggest single suspect, smallest change, easiest revert
2. **Task 4** (BDB status parsing) — could be a "factory-new never steers" bug, would explain the worst cases
3. **Task 3** (full channel mask) — eliminates "joins only when on ch11" failure mode
4. **Task 2** (non-blocking retries) — quality-of-life, makes subsequent debugging clearer
5. **Task 5** (keep-alive + long-poll) — fixes router-roam survival
6. **Task 6** (sleep cleanup) — overnight-stability check
7. **Task 7** (TC link key investigation) — only act if 1–6 don't fully fix things

## Risks & unknowns

- **Risk:** Task 4 is theoretical — we don't yet have proof the BDB status cast is wrong. If the SDK header confirms the current cast is correct, the task becomes a no-op + documentation update. That's still useful (eliminates a suspect on paper), so the task stays as written.
- **Risk:** Task 5 references `zb_zdo_pim_set_long_poll_interval` from the v1 SDK. The v2 equivalent may have a different name or may have been folded into `esp_zb_*` instead of `ezb_*`. The task allows a documented-TODO fallback so it doesn't block the rest.
- **Risk:** Task 6 may surface an entirely separate power-related regression once the half-sleep config is removed (e.g. CPU pegged at 160 MHz, brown-out near 5 V supply). Power the bench device from a known-good USB source during this test.
- **Unknown:** Whether Z2M's coordinator firmware version matters. Out of scope for this plan, but if Tasks 1–7 do not produce reliable behaviour, the next investigation should compare Z2M coordinator FW versions used during the Monday-morning working build vs today.
- **Unknown:** Whether the SCD40-C6 board has an external antenna actually attached. If a U.FL antenna was fitted later, Task 1's default of "internal antenna" is wrong. The Kconfig flag handles this — confirm hardware state before flashing Task 1.

## Open questions

- None blocking. All assumptions above are documented; correct them when reviewing the plan.
