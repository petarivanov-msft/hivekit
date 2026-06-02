# Changelog

All notable changes to this project will be documented in this file.

## Unreleased

### Added
- **TX debug counters**: Three module-level `uint32_t` counters (`s_tx_queued`, `s_tx_confirmed`,
  `s_tx_failed`) track report submissions and APS outcomes across the lifetime of a firmware run.
  Counters reset on reboot; no NVS persistence.
- **Heartbeat task (`tx_heartbeat_task`)**: FreeRTOS task started from `hivekit_init()` that logs
  `TX heartbeat: queued=N confirmed=M failed=K uptime=Ts` at INFO every 5 minutes, making TX
  health observable from the serial log even during quiet periods.
