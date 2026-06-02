# Changelog

All notable changes to this project will be documented in this file.

## Unreleased

### Reverted
- **PR #13 (ZCL per-command confirm callback) reverted**: the explicit
  `ezb_zcl_report_attr_cmd_req()` calls added in `2fe0fb7` failed synchronously on
  every report (observed on `dev-87853ff` flash 2026-06-02) because the call requires
  a resolved destination, but `dst_addr.addr_mode = EZB_ADDR_MODE_NONE` relies on the
  binding table which is not yet populated at the time periodic reports start firing.
  Net result: nothing reached the APS layer post-pair, only MAC/LQI traffic. Restored
  the v0.3.6 pattern: `ezb_zcl_set_attr_value()` only, letting the SDK auto-reporting
  (configured by ZHA via `Configure Reporting` after interview) drive the air traffic,
  with the APSDE-DATA confirm handler tracking outcomes. `g_last_ezb_ok_ms` from PR #15
  is now updated from the APSDE confirm success path so the freeze heartbeat keeps working.
  Reverts `2fe0fb7`, `01ff4c0`, `df4f80e`. PR #12 (TX counters + heartbeat) and PR #15
  (freeze diagnostic counters) remain intact.

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
