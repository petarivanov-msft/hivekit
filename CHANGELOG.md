# Changelog

All notable changes to this project will be documented in this file.

## Unreleased

### Fixed
- **SCD40 cold-boot silent init failure** (closes #19):
  On a genuine cold boot (VDD ramp from 0 V), the SCD40's I²C engine could ACK
  `start_periodic_measurement` while the measurement subsystem was still
  initialising — `scd40_init()` returned `ESP_OK` but the sensor never produced
  data. All subsequent `data_ready_status` polls returned `0x0000`, logged only
  at `LOGD` and silently `continue`d, leaving the device with no sensor output
  for the entire session.
  - **Retry loop**: `start_periodic_measurement` retried up to 5 × with 200 ms
    back-off on any I²C failure.
  - **Post-stop settle**: 500 ms → 1000 ms (Sensirion SCD4x datasheet §3.1
    mandates ≥1000 ms after VDD ramp before I²C commands).
  - **Stall detection**: after 3 consecutive `data_ready_status=0` cycles,
    `scd40_read_measurement()` returns `ESP_ERR_TIMEOUT` to trigger the existing
    `scd40_reinit()` recovery path in `sensor_task`.
  - **Init failure → reboot**: terminal `scd40_init()` failure now logs a loud
    error banner and calls `esp_restart()` after 5 s instead of silently deleting
    the sensor task.
  - **I²C handle leak fixed**: on exhausted retries, I²C bus/device handles are
    torn down before returning the error to prevent leaks on reinit callers.

### Restored
- **PR #13 re-applied with freeze diagnostics**: PR #16 (revert of PR #13) was a misdiagnosis.
  PR #13's explicit `ezb_zcl_report_attr_cmd_req()` calls with `EZB_ADDR_MODE_NONE` (binding table)
  were the only path actually delivering reports over the air; SDK auto-reporting was not active.
  Restored the PR #13 code in all three sensor report functions plus the per-ZCL confirm callback,
  and added pre-lock/locked/released timing logs and per-iteration heap logs to pinpoint the
  ~5-min freeze observed on dev-87853ff (full system freeze: sensor task + TX both die).
  Supersedes the PR #16 revert (kept in git history for traceability; not a behaviour change
  versus PR #13 itself other than the new diagnostic logs and heap counters).

### Added
- **APS confirm success promoted to INFO**: Successful APS data-confirm callbacks now log at
  `ESP_LOGI` (was `ESP_LOGD`/silent) so every over-the-air TX outcome is visible in the serial
  log without enabling verbose logging.
- **TX debug counters**: Three module-level `uint32_t` counters (`s_tx_queued`, `s_tx_confirmed`,
  `s_tx_failed`) track report submissions and APS outcomes across the lifetime of a firmware run.
  Counters reset on reboot; no NVS persistence.
- **Heartbeat task (`tx_heartbeat_task`)**: FreeRTOS task started from `hivekit_init()` that logs
  `TX heartbeat: queued=N confirmed=M failed=K uptime=Ts` at INFO every 5 minutes, making TX
  health observable from the serial log even during quiet periods.
