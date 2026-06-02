# Changelog

All notable changes to this project will be documented in this file.

## Unreleased

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
