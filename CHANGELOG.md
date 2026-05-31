# Changelog

All notable changes to this project will be documented in this file.

## Unreleased

### Fixed
- **ZCL TX counters now use per-command confirm callback**: `s_tx_confirmed` / `s_tx_failed` are
  now incremented via a `cnf_ctx.cb` (`ezb_af_user_cnf_callback_t`) wired on each
  `ezb_zcl_report_attr_cmd_req()` call, replacing the APSDE-DATA confirm handler that suppressed
  the SDK’s internal APSDE consumer and caused counters to remain stuck at zero. On failure the
  callback logs ZCL cluster ID and status code. Heartbeat now reflects actual delivery
  success/failure with cluster + ZCL status code.

### Added
- **TX debug counters**: Three module-level `uint32_t` counters (`s_tx_queued`, `s_tx_confirmed`,
  `s_tx_failed`) track report submissions and APS outcomes across the lifetime of a firmware run.
  Counters reset on reboot; no NVS persistence.
- **Heartbeat task (`tx_heartbeat_task`)**: FreeRTOS task started from `hivekit_init()` that logs
  `TX heartbeat: queued=N confirmed=M failed=K uptime=Ts` at INFO every 5 minutes, making TX
  health observable from the serial log even during quiet periods.
