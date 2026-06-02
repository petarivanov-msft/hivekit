# Changelog

All notable changes to this project will be documented in this file.

## v0.3.7 - 2026-06-05

### Fixed
- **SCD40-C6 cold-boot silent init failure** (closes #19): On a genuine cold
  boot (VDD ramp from 0 V), the SCD40's I²C engine could ACK
  `start_periodic_measurement` while the measurement subsystem was still
  initialising — `scd40_init()` returned `ESP_OK` but the sensor never
  produced data, leaving the device silent for the whole session.
  Adds a retry loop (5 × 200 ms back-off) on `start_periodic_measurement`,
  extends the post-stop settle from 500 ms to 1000 ms (per Sensirion SCD4x
  datasheet §3.1), introduces stall detection (3 consecutive
  `data_ready_status=0` cycles trigger `scd40_reinit()` recovery), promotes
  terminal init failure to `esp_restart()` after 5 s instead of silent task
  deletion, and fixes an I²C handle leak on exhausted retries. Validated
  on a real cold-boot ESP32-C6 unit, running cleanly for several days.
- **SCD40-C6 sustained Zigbee reporting**: CO₂, temperature, and humidity
  reports now arrive at Zigbee2MQTT continuously without requiring a manual
  refresh of the device page. Restores explicit
  `ezb_zcl_report_attr_cmd_req()` calls with `EZB_ADDR_MODE_NONE` (binding
  table) in all three sensor report paths plus the per-ZCL confirm callback,
  paired with `ezb_zcl_set_attr_value()` so attribute state stays in sync.
  Validated 40+ minutes of sustained reports on `dev-5a6f75f`.

### Added
- **APS confirm logged at INFO**: every over-the-air TX outcome is now
  visible in the serial log without enabling verbose logging
  (`ESP_LOGI`, was `ESP_LOGD`).
- **TX debug counters**: lifetime counters `s_tx_queued`, `s_tx_confirmed`,
  `s_tx_failed` track report submissions and APS outcomes since boot.
  No NVS persistence; reset on reboot.
- **Heartbeat task**: `tx_heartbeat_task` logs
  `TX heartbeat: queued=N confirmed=M failed=K uptime=Ts` at INFO every
  5 minutes, surfacing TX health during quiet periods.
- **Freeze diagnostics**: pre-lock / locked / released timing logs and
  per-iteration heap counters in the SCD40 sensor task to investigate
  the long-tail full-system freeze observed on dev-87853ff.
