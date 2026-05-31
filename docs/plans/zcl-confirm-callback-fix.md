# Plan: ZCL report-attr confirm path — make TX counters reflect reality

Branch: `fix/zcl-confirm-callback` (off `feat/tx-debug-counters`)
Lands after PR #12 (debug counters) merges to `main`.

## Goal
Wire up a confirm callback that **actually fires** for outgoing ZCL Report Attributes commands, so `s_tx_confirmed` / `s_tx_failed` track reality and the TX heartbeat shows movement on a healthy device. Acceptance: within ~2 minutes of joining a Z2M coordinator on a `dev` build, `confirmed > 0` (or `failed > 0` with explicit cluster + status logged), and `queued ≈ confirmed + failed` ± small in-flight drift.

## Out of scope
- Bug 2: sensor task hang at uptime ~1026 s (queued counter freezes). Tracked as a follow-up plan.
- Changes to reporting interval, attribute set, role (still Router), or sensor task logic.
- Persistent NVS counters / OTA reporting of counters.
- Any new Kconfig options.

## Assumptions (verified by reading SDK headers under `/tmp/ezsdk/components/esp-zigbee-lib/include/ezbee/`)

A1. The current code uses `ezb_*` symbols which are the **native v2 esp-zigbee-lib API** (per `components/hivekit/idf_component.yml` → `espressif/esp-zigbee-lib ~2.0.0`). The `compat/esp_zb_*` headers are a thin legacy shim. There is **no separate `esp_zb_apsde_data_confirm_handler_register`** — the brief's "wrapper vs real API" hypothesis does not hold.

A2. `ezbee/aps.h` documents `ezb_apsde_data_confirm_handler_register()` with a meaningful caveat:
> "If the callback is registered by the application, the application is responsible for handling APSDE confirm."
This strongly implies registering the user handler **replaces** (rather than augments) the SDK's internal APSDE-confirm consumer. Doing so likely breaks any upper-stack flow (incl. ZCL command confirm tracking) that relies on the default handler. This fits the observed symptom: counters stuck at `confirmed=0 failed=0` for 30+ minutes despite a successful join and a live mesh.

A3. `ezbee/zcl/zcl_common.h` defines `ezb_zcl_cmd_ctrl_t` with `cnf_ctx` of type `ezb_zcl_cmd_cnf_ctx_t` (= `ezb_af_user_cnf_ctx_t`), containing `cb` (`ezb_af_user_cnf_callback_t`) and `user_ctx`. The callback receives `ezb_af_user_cnf_t` carrying `status, tsn, dst_addr, src_ep, dst_ep, cluster_id, profile_id` — exactly the per-command confirm we need. This is plumbed for `ezb_zcl_report_attr_cmd_req()` (per its docstring at `zcl_general_cmd.h:528`).

A4. The current sensor report path (`hivekit_report_scd40/_sht40/_bme280` in `hivekit_core.c`) calls **only** `ezb_zcl_set_attr_value()`. It does **not** call `ezb_zcl_report_attr_cmd_req()`. Outgoing reports therefore depend entirely on the SDK's automatic reporting (intervals configured per attribute). That auto-report path emits APS frames that *could* be observed via APSDE-confirm, but per A2 our handler registration may have neutralised it.

A5. The `s_tx_queued` semantics from PR #12 are "one increment per `hivekit_report_*()` call" (one per sensor cycle, irrespective of attribute count). To restore the heartbeat invariant `confirmed + failed ≈ queued`, the queued counter must be incremented at the **same granularity** as the new confirm callback fires.

A6. Petar reports Z2M "renames trigger immediate update" and `last_seen` ticks. That confirms the radio is alive on receive and a routing/MAC-level frame path exists, but does **not** prove ZCL Report frames are leaving the chip — it could just be NWK-layer router beacons / link-status. So the fix has to cover both possibilities (reports may not be flying at all OR they may be flying but their confirm path is broken).

A7. `ezb_af_user_cnf_t.status` follows the standard Zigbee status set (0 = success, non-zero = failure). The callback also exposes `cluster_id`, satisfying Petar's request to log cluster + status on failure.

## Tasks

### T1 — Verify confirm-handler interaction (read-only spike)
- **Where**: `components/hivekit/hivekit_core.c`, plus headers under `/tmp/ezsdk/.../ezbee/` and the built `managed_components/` after a fresh `idf.py reconfigure` for one sensor target (e.g. `sensors/sht40-c6`).
- **Do**:
  - Confirm `idf_component.yml` resolves to `esp-zigbee-lib` 2.0.x (capture exact version from `dependencies.lock` or `managed_components/`).
  - Grep the resolved SDK source / headers for any default APSDE-confirm consumer to validate Assumption A2 (does registering our handler suppress it?). If a definitive answer isn't visible from headers + public docs, document the uncertainty in commit message and lean on the empirical test in T6.
  - Cross-check by searching the public `espressif/esp-zigbee-sdk` GitHub for any sample using `ezb_zcl_report_attr_cmd_req` + `cnf_ctx`.
- **Acceptance**: a one-paragraph note appended to this plan under a `## Investigation log` section recording: SDK version, presence/absence of a default APSDE consumer, any sample code references. No code changes.

### T2 — Switch confirm tracking from APSDE-DATA to per-command ZCL `cnf_ctx`
- **Where**: `components/hivekit/hivekit_core.c`.
- **Do**:
  - Add a new file-scope confirm callback `hivekit_zcl_cmd_confirm_cb(ezb_af_user_cnf_t *cnf, void *user_ctx)`:
    - On `cnf->status == 0`: `s_tx_confirmed++`; `ESP_LOGI(TAG, "ZCL TX ok: cluster=0x%04x ep=%u->%u tsn=%u")`.
    - Otherwise: `s_tx_failed++`; `ESP_LOGW(TAG, "ZCL TX failed: cluster=0x%04x ep=%u->%u tsn=%u status=0x%02x")` — satisfies Petar's "log cluster + status" ask.
  - **Remove** the call to `ezb_apsde_data_confirm_handler_register(hivekit_aps_data_confirm_cb)` and the `hivekit_aps_data_confirm_cb` function itself (and its commentary block). Rationale per A2.
  - Leave `s_tx_queued / s_tx_confirmed / s_tx_failed` declarations and the heartbeat task untouched.
- **Acceptance**: builds clean for all three sensor targets; `grep -n "apsde_data_confirm" components/hivekit/` returns 0 hits; new callback compiles and is referenced (even if not yet wired — wiring happens in T3).

### T3 — Wire `cnf_ctx.cb` on each report-attribute command
- **Where**: `components/hivekit/hivekit_core.c` (the three `hivekit_report_*` functions) and `components/hivekit/hivekit_reporting.c` (`hivekit_force_report`).
- **Do**:
  - For each attribute the sensor reports (temp / humidity / co2 / pressure depending on device), after `ezb_zcl_set_attr_value(...)`, build a `ezb_zcl_report_attr_cmd_t` and call `ezb_zcl_report_attr_cmd_req(&cmd)`. Set:
    - `cmd.cmd_ctrl.cluster_id` = the cluster being reported
    - `cmd.cmd_ctrl.src_ep` = the sensor endpoint (1)
    - `cmd.cmd_ctrl.dst_addr.addr_mode = EZB_ADDR_MODE_NONE` (so the SDK uses bindings / coordinator route — same pattern as existing `hivekit_force_report`)
    - `cmd.cmd_ctrl.fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI`
    - `cmd.cmd_ctrl.cnf_ctx.cb = hivekit_zcl_cmd_confirm_cb`
    - `cmd.cmd_ctrl.cnf_ctx.user_ctx = NULL`
    - `cmd.payload.attr_id` = the attribute id
  - Increment `s_tx_queued` **once per `report_attr_cmd_req` call** (so once per attribute, not once per sensor cycle). Update the existing `s_tx_queued++` placement at the end of each `hivekit_report_*` accordingly — remove the single per-cycle increment and rely on per-attribute increments.
  - Preserve the existing `esp_zigbee_lock_acquire / release` brackets around the new calls.
  - Update `hivekit_force_report` to also set `cnf_ctx.cb = hivekit_zcl_cmd_confirm_cb` so manual force-reports are counted on the same path.
  - Keep the existing aggregate `ESP_LOGI(TAG, "ZCL report results: ...")` line at info — it now reports **set-attr** statuses; rename in the format string to `"ZCL set+req queued: ..."` to avoid implying delivery. Format & values otherwise unchanged.
- **Acceptance**:
  - All three sensor report functions emit one `ezb_zcl_report_attr_cmd_req` per attribute with `cnf_ctx.cb` set.
  - `s_tx_queued` increments once per attribute (so SCD40: 3/cycle, SHT40: 2/cycle, BME280: 3/cycle).
  - `hivekit_force_report` also wires `cnf_ctx.cb`.
  - `grep "ezb_zcl_report_attr_cmd_req" components/hivekit/` shows the new call sites.

### T4 — Update CHANGELOG and code comments
- **Where**: `CHANGELOG.md` (Unreleased → Changed/Fixed), `hivekit_core.c` API-VERIFICATION block.
- **Do**:
  - Add Unreleased entry: `Fixed: ZCL TX counters now use per-command confirm callback (cnf_ctx) instead of APSDE-DATA confirm handler. Heartbeat now reflects actual delivery success/failure with cluster + ZCL status code.`
  - Update the API-VERIFICATION comment in `hivekit_core.c` to drop `ezb_apsde_data_confirm_handler_register` and add `ezb_af_user_cnf_callback_t`, `ezb_zcl_cmd_ctrl_t.cnf_ctx`, citing source headers (`ezbee/af.h`, `ezbee/zcl/zcl_common.h`).
  - Bump no version tag (Petar tags after empirical validation).
- **Acceptance**: CHANGELOG diff shows the Fixed line; comment block in `hivekit_core.c` no longer references the removed APS API.

### T5 — Local build matrix
- **Where**: each of `sensors/scd40-c6`, `sensors/sht40-c6`, `sensors/bme280-c6`.
- **Do**: `idf.py build` (or rely on CI `build.yml`) per target.
- **Acceptance**: all three build clean (no warnings introduced by this change); binary size delta within ±2 KB of baseline.

### T6 — Empirical validation on real hardware (post-merge, dev channel)
- **Where**: One flashed `dev` build joined to a Z2M coordinator.
- **Do**:
  - Capture serial log for ≥ 10 minutes after join.
  - Verify: at least one `ZCL TX ok` line within 2 minutes of join; first heartbeat shows `confirmed > 0` (or `failed > 0` with non-zero status); subsequent heartbeats show `confirmed + failed ≈ queued` (drift ≤ a few in-flight frames).
  - Cross-check Z2M: temp/humidity/co2 values updating in HA dashboard (not stuck at 0).
- **Acceptance**: log + Z2M screenshot attached to the eventual PR. If `failed > 0` consistently, dump the cluster+status code into the PR description for follow-up triage (separate issue).

## Risks

R1. **`cnf_ctx` may not be honoured for `ezb_zcl_report_attr_cmd_req`.** Doc says it is, but unverified empirically. Mitigation: T6 will tell us within minutes; if it fires for `set_attr_value`-driven auto-reports this is moot, but if not, fallback is to revert the APSDE handler and re-test with the per-command callback as the primary signal — the sensor cycles now explicitly call `report_attr_cmd_req`, so the per-command path *will* fire even if APSDE doesn't.

R2. **Switching from auto-reporting to explicit `report_attr_cmd_req` per attribute could double-send** (auto-report still fires its own copy). Mitigation: SDK auto-report only fires when configured min-interval has elapsed AND value has changed by `delta`. Explicit `report_attr_cmd_req` issues an immediate report regardless. Worst case is moderate extra TX, harmless on a Router. If excessive, adjust by removing the auto-reporting configuration via `ezb_zcl_reporting_info_remove` in a follow-up.

R3. **Removing `ezb_apsde_data_confirm_handler_register` may not be safe** — it could expose stack assertions if the SDK genuinely required *some* consumer. Mitigation: keep T1's investigation as the primary check; if uncertain, register a no-op stub instead of removing entirely (one-line tweak in T2). Default plan is to remove; fall back to no-op stub if T6 shows assertion failures.

R4. **`cnf_ctx` cb runs in Zigbee-task context** — must not block. Mitigation: callback only does `++` and one log line, both non-blocking.

R5. **Counter increment ordering** — `s_tx_queued` is bumped after `report_attr_cmd_req` returns, but the cnf cb may already have fired by then on a fast/local path. Race is benign for a debug counter (heartbeat is 5 min apart) but could cause a momentary `confirmed > queued` blip in the log. Acceptable.

## Files expected to change
- `components/hivekit/hivekit_core.c` (T2, T3, T4)
- `components/hivekit/hivekit_reporting.c` (T3)
- `CHANGELOG.md` (T4)
- This plan file (T1 investigation log appendix)

## Investigation log

**SDK version:** `espressif/esp-zigbee-lib` 2.0.1 (from `/tmp/ezsdk/components/esp-zigbee-lib/idf_component.yml`;
component manifest pins `~2.0.0` so 2.0.x patch versions are accepted).

**APSDE confirm consumer behaviour:** Header `ezbee/aps.h` carries the note
`"If the callback is registered by the application, the application is responsible for handling APSDE confirm."` —
consistent with a replacement (not augment) semantic. No separate default internal consumer is exported from the header;
the SDK does not expose a `ezb_apsde_data_confirm_handler_get_default()` or similar. Removing the registration
leaves APSDE confirm unhandled by user code, returning to whatever the stack's own upper-layer flow does (ZCL stack).
This is safe for our purpose because the per-command `cnf_ctx.cb` path operates above APSDE and fires independently.

**Per-command confirm path:** `ezbee/af.h` defines `ezb_af_user_cnf_t` (status, tsn, dst_addr, src_ep, dst_ep,
cluster_id, profile_id) and `ezb_af_user_cnf_callback_t`. `ezbee/zcl/zcl_common.h` typedefs
`ezb_zcl_cmd_cnf_ctx_t = ezb_af_user_cnf_ctx_t` and `ezb_zcl_cmd_ctrl_t` includes `cnf_ctx` as its last field.
`ezbee/zcl/zcl_general_cmd.h` documents `ezb_zcl_report_attr_cmd_req()` with the docstring
`"The response will be delivered via the callback specified in cmd_req->cmd_ctrl.cnf_ctx"` —
confirming `cnf_ctx.cb` is honoured for report-attr. No sample code found in the ezsdk tree for this specific
combination; doc string evidence is sufficient. Empirical proof deferred to T6 (hardware validation).
