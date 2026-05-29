/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * hivekit_core.c — Zigbee stack init and signal handler
 *
 * API VERIFICATION (all calls checked against SDK main branch 2026-05-17):
 *   esp_zigbee_init()                       → esp_zigbee.h
 *   esp_zigbee_start()                      → esp_zigbee.h
 *   esp_zigbee_launch_mainloop()            → esp_zigbee.h
 *   esp_zigbee_lock_acquire()               → esp_zigbee.h
 *   esp_zigbee_lock_release()               → esp_zigbee.h
 *   esp_zigbee_factory_reset()              → esp_zigbee.h
 *   ezb_bdb_start_top_level_commissioning() → ezbee/bdb.h
 *   ezb_bdb_is_factory_new()                → ezbee/bdb.h
 *   ezb_bdb_dev_joined()                    → ezbee/bdb.h
 *   ezb_bdb_reset_via_local_action()        → ezbee/bdb.h
 *   ezb_bdb_set_primary_channel_set()       → ezbee/bdb.h
 *   ezb_app_signal_add_handler()            → ezbee/app_signals.h
 *   ezb_app_signal_get_type()               → ezbee/app_signals.h
 *   ezb_app_signal_get_params()             → ezbee/app_signals.h
 *   ezb_app_signal_to_string()              → ezbee/app_signals.h
 *   ezb_aps_secur_enable_distributed_security() → ezbee/aps.h
 *   ezb_nwk_get_panid()                     → ezbee/nwk.h
 *   ezb_nwk_get_current_channel()           → ezbee/nwk.h
 *   ezb_nwk_get_short_address()             → ezbee/nwk.h
 *   ezb_nwk_get_extended_panid()            → ezbee/nwk.h
 *   EZB_BDB_MODE_INITIALIZATION             → ezbee/bdb.h
 *   EZB_BDB_MODE_NETWORK_STEERING           → ezbee/bdb.h
 *   EZB_BDB_STATUS_SUCCESS                  → ezbee/bdb.h
 *   EZB_ZDO_SIGNAL_SKIP_STARTUP             → ezbee/app_signals.h
 *   EZB_BDB_SIGNAL_DEVICE_FIRST_START       → ezbee/app_signals.h
 *   EZB_BDB_SIGNAL_DEVICE_REBOOT            → ezbee/app_signals.h
 *   EZB_BDB_SIGNAL_STEERING                 → ezbee/app_signals.h
 *   EZB_ZDO_SIGNAL_LEAVE                    → ezbee/app_signals.h
 *   EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS       → ezbee/app_signals.h
 *   ESP_ZIGBEE_DEFAULT_CONFIG()             → esp_zigbee.h (macro, see example usage)
 *   ESP_ZIGBEE_STORAGE_PARTITION_NAME       → esp_zigbee.h / sdkconfig
 *   esp_zb_scheduler_alarm()               → ezbee/scheduler.h (v2 public API; v2 SDK kept
 *                                             esp_zb_* prefix for scheduler. If the build
 *                                             reports "undeclared", try including esp_zigbee.h
 *                                             instead or use the ezb_scheduler_alarm() variant.)
 *   esp_restart()                          → esp_system.h
 */

#include "hivekit.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"

#include "esp_zigbee.h"
#include "ezbee/scheduler.h"
#include "ezbee/bdb.h"
#include "ezbee/app_signals.h"
#include "ezbee/nwk.h"
#include "ezbee/aps.h"
#include "ezbee/zha.h"
#include "ezbee/zcl.h"
#include "ezbee/zcl/zcl_common.h"
#include "ezbee/zcl/cluster/basic.h"
#include "ezbee/zcl/cluster/carbon_dioxide_measurement_desc.h"
#include "ezbee/zcl/cluster/temperature_measurement_desc.h"
#include "ezbee/zcl/cluster/rel_humidity_measurement_desc.h"

static const char *TAG = "hivekit_core";

/* ── Internal state ───────────────────────────────────────────────────────── */

static const hivekit_config_t *s_cfg = NULL;
static void (*s_signal_cb)(uint16_t signal_type, const void *params) = NULL;

/* ── Scheduler-alarm callbacks ───────────────────────────────────────────── */

/* Fired by esp_zb_scheduler_alarm() ~3 s after network steering fails.
 * Runs on the Zigbee main task — no locking required for BDB calls. */
static void retry_network_steering_cb(uint8_t param)
{
    (void)param;
    ESP_LOGI(TAG, "Retry timer fired — starting BDB network steering");
    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
}

/* Fired ~2 s after a DEVICE_FIRST_START / DEVICE_REBOOT BDB-init failure.
 * esp_restart() is safe here; the process tears down completely. */
static void restart_device_cb(uint8_t param)
{
    (void)param;
    ESP_LOGE(TAG, "Restart timer fired — rebooting now");
    esp_restart();
}

/* Fired ~2 s after a successful network join to turn the LED off. */
static void led_off_cb(uint8_t param)
{
    (void)param;
    hivekit_led_set_pattern(HIVEKIT_LED_OFF);
}

/* ── Signal handler ───────────────────────────────────────────────────────── */

static bool hivekit_app_signal_handler(const ezb_app_signal_t *app_signal)
{
    /* SOURCE: app_signals.h — ezb_app_signal_get_type, ezb_app_signal_get_params */
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);

    switch (signal_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        /* Stack framework ready; kick off BDB initialization.
         * Channel masks and distributed-security setting are configured
         * BEFORE esp_zigbee_start() in main.c — v2 SDK requires that order. */
        ESP_LOGI(TAG, "Zigbee stack ready — starting BDB init");
        /* SOURCE: bdb.h — ezb_bdb_start_top_level_commissioning */
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        /* SOURCE: bdb.h — ezb_bdb_comm_status_t, EZB_BDB_STATUS_SUCCESS */
        ezb_bdb_comm_status_t status =
            *((const ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            /* SOURCE: bdb.h — ezb_bdb_is_factory_new */
            if (ezb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Factory-new device — starting network steering");
                hivekit_led_set_pattern(HIVEKIT_LED_SLOW_BLINK);
                /* SOURCE: bdb.h — EZB_BDB_MODE_NETWORK_STEERING */
                ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device reboot — rejoining previous network");
                hivekit_led_set_pattern(HIVEKIT_LED_SOLID);
            }
        } else {
            ESP_LOGE(TAG, "BDB init failed (status=0x%02x) — restarting in 2s", status);
            /* Matches reference: esp_restart() after a short scheduler-alarm delay.
             * Using a scheduler alarm instead of vTaskDelay() so the Zigbee task
             * is not blocked and the MAC layer can remain responsive during the wait. */
            esp_zb_scheduler_alarm(restart_device_cb, 0, 2000);
        }
    } break;

    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status =
            *((const ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            /* SOURCE: nwk.h — ezb_nwk_get_panid, ezb_nwk_get_current_channel, ezb_nwk_get_short_address */
            ESP_LOGI(TAG, "Joined network: PAN 0x%04hx, ch %d, addr 0x%04hx",
                     ezb_nwk_get_panid(), ezb_nwk_get_current_channel(),
                     ezb_nwk_get_short_address());
            hivekit_led_set_pattern(HIVEKIT_LED_SOLID);
            /* Schedule LED-off 2 s from now via scheduler alarm so we do not
             * block the Zigbee task with vTaskDelay(). */
            esp_zb_scheduler_alarm(led_off_cb, 0, 2000);
        } else {
            ESP_LOGW(TAG, "Network steering failed (status=0x%02x), scheduling retry in 3s", status);
            hivekit_led_set_pattern(HIVEKIT_LED_SLOW_BLINK);
            /* Auto-retry via scheduler alarm instead of vTaskDelay().
             * Scheduling on the Zigbee task timer means the MAC layer stays
             * fully responsive during the 3 s window (can ACK beacons, send
             * poll requests, respond to the trust centre). */
            esp_zb_scheduler_alarm(retry_network_steering_cb, 0, 3000);
        }
    } break;

    case EZB_ZDO_SIGNAL_LEAVE: {
        const ezb_zdo_signal_leave_params_t *leave_params =
            (const ezb_zdo_signal_leave_params_t *)ezb_app_signal_get_params(app_signal);
        ESP_LOGI(TAG, "Left network (type=0x%02x)", leave_params->leave_type);
        hivekit_led_set_pattern(HIVEKIT_LED_OFF);
    } break;

    case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        uint8_t duration = *(const uint8_t *)ezb_app_signal_get_params(app_signal);
        ESP_LOGI(TAG, "Permit join %s (duration=%ds)",
                 duration ? "open" : "closed", duration);
    } break;

    default:
        ESP_LOGD(TAG, "Signal: %s (0x%02x)",
                 ezb_app_signal_to_string(signal_type), signal_type);
        break;
    }

    /* Forward to optional app callback */
    if (s_signal_cb) {
        s_signal_cb((uint16_t)signal_type, ezb_app_signal_get_params(app_signal));
    }

    return true; /* Handled */
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t hivekit_init(const hivekit_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->manufacturer_name && cfg->model_identifier,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid hivekit config");
    s_cfg = cfg;

    /* Register our signal handler.
     * SOURCE: app_signals.h — ezb_app_signal_add_handler */
    ESP_RETURN_ON_ERROR(ezb_app_signal_add_handler(hivekit_app_signal_handler),
                        TAG, "Failed to register signal handler");

    /* TODO (Phase 1): init LED driver here using espressif/led_indicator */
    /* For now, LED ops are stubs (see hivekit_led.c) */

    return ESP_OK;
}

void hivekit_set_signal_handler_cb(void (*cb)(uint16_t signal_type, const void *params))
{
    s_signal_cb = cb;
}

esp_err_t hivekit_create_scd40_device(void)
{
    ESP_RETURN_ON_FALSE(s_cfg, ESP_ERR_INVALID_STATE, TAG, "hivekit not initialised");

    /*
     * v2 SDK device construction pattern (from temperature_sensor.c example):
     *
     *   ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
     *   ezb_af_ep_desc_t ep_desc = ezb_zha_create_temperature_sensor(ep_id, &cfg);
     *   ... add extra clusters to ep_desc ...
     *   ezb_af_device_add_endpoint_desc(dev_desc, ep_desc);
     *   ezb_af_device_desc_register(dev_desc);
     *
     * SOURCE: ezbee/zha.h, ezbee/af.h
     *
     * For SCD40 we need a temperature sensor as the base (gives us Basic + Identify
     * + Temperature Measurement for free), then add Humidity and CO2 clusters manually.
     *
     * TODO (Phase 1): The exact function name for creating the humidity/CO2 cluster
     * descriptors must be verified against the actual SDK at build time.
     * Expected names based on the pattern in cluster desc headers:
     *   ezb_zcl_rel_humidity_measurement_create_cluster_desc()
     *   ezb_zcl_carbon_dioxide_measurement_create_cluster_desc()
     * SOURCE: ezbee/zcl/cluster/rel_humidity_measurement_desc.h
     *         ezbee/zcl/cluster/carbon_dioxide_measurement_desc.h
     */

    const uint8_t ep_id = 1;

    /* Base device: temperature sensor */
    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    ESP_RETURN_ON_FALSE(dev_desc, ESP_ERR_NO_MEM, TAG, "Failed to create device desc");

    ezb_zha_temperature_sensor_config_t ts_cfg = EZB_ZHA_TEMPERATURE_SENSOR_CONFIG();
    ts_cfg.temp_meas_cfg.min_measured_value = -1000; /* -10.00 °C */
    ts_cfg.temp_meas_cfg.max_measured_value =  8000; /* +80.00 °C */

    ezb_af_ep_desc_t ep_desc = ezb_zha_create_temperature_sensor(ep_id, &ts_cfg);
    ESP_RETURN_ON_FALSE(ep_desc, ESP_ERR_NO_MEM, TAG, "Failed to create endpoint desc");

    /* Patch Basic cluster: set manufacturer name + model ID */
    ezb_zcl_cluster_desc_t basic_desc =
        ezb_af_endpoint_get_cluster_desc(ep_desc,
                                         EZB_ZCL_CLUSTER_ID_BASIC,
                                         EZB_ZCL_CLUSTER_SERVER);
    /* SOURCE: ezbee/zcl/cluster/basic.h — ezb_zcl_basic_cluster_desc_add_attr */
    ezb_zcl_basic_cluster_desc_add_attr(basic_desc,
                                        EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                        (void *)s_cfg->manufacturer_name);
    ezb_zcl_basic_cluster_desc_add_attr(basic_desc,
                                        EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                        (void *)s_cfg->model_identifier);
    if (s_cfg->fw_version) {
        ezb_zcl_basic_cluster_desc_add_attr(basic_desc,
                                            EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID,
                                            (void *)s_cfg->fw_version);
    }

    /* Add Relative Humidity cluster (0x0405) */
    /* TODO: verify ezb_zcl_rel_humidity_measurement_create_cluster_desc exists in your SDK version */
    ezb_zcl_rel_humidity_measurement_cluster_server_config_t rh_cfg = {
        .measured_value     = 0,
        .min_measured_value = 0,
        .max_measured_value = 10000, /* 100.00% */
    };
    /* SOURCE: ezbee/zcl/cluster/rel_humidity_measurement_desc.h */
    ezb_zcl_cluster_desc_t rh_cluster =
        ezb_zcl_rel_humidity_measurement_create_cluster_desc(&rh_cfg,
                                                             EZB_ZCL_CLUSTER_SERVER);
    if (rh_cluster) {
        ezb_af_endpoint_add_cluster_desc(ep_desc, rh_cluster);
    } else {
        ESP_LOGW(TAG, "Failed to create humidity cluster (non-fatal, will be missing)");
    }

    /* Add CO2 Measurement cluster (0x040d) */
    /* NOTE: CO2 MeasuredValue in ZCL is a single-precision float in range [0.0, 1.0]
     * where 1.0 = 1,000,000 ppm. So 400 ppm = 0.0004.
     * SOURCE: ezbee/zcl/cluster/carbon_dioxide_measurement_desc.h */
    ezb_zcl_carbon_dioxide_measurement_cluster_server_config_t co2_cfg = {
        .measured_value     = 0.0f / 0.0f, /* NaN = unknown */
        .min_measured_value = 0.0f / 0.0f,
        .max_measured_value = 0.0f / 0.0f,
    };
    /* SOURCE: ezbee/zcl/cluster/carbon_dioxide_measurement_desc.h */
    ezb_zcl_cluster_desc_t co2_cluster =
        ezb_zcl_carbon_dioxide_measurement_create_cluster_desc(&co2_cfg,
                                                               EZB_ZCL_CLUSTER_SERVER);
    if (co2_cluster) {
        ezb_af_endpoint_add_cluster_desc(ep_desc, co2_cluster);
    } else {
        ESP_LOGW(TAG, "Failed to create CO2 cluster (non-fatal, will be missing)");
    }

    /* Register the device */
    ESP_RETURN_ON_ERROR(ezb_af_device_add_endpoint_desc(dev_desc, ep_desc),
                        TAG, "Failed to add endpoint");
    ESP_RETURN_ON_ERROR(ezb_af_device_desc_register(dev_desc),
                        TAG, "Failed to register device");

    return ESP_OK;
}

esp_err_t hivekit_report_scd40(const hivekit_scd40_reading_t *reading)
{
    ESP_RETURN_ON_FALSE(reading, ESP_ERR_INVALID_ARG, TAG, "NULL reading");

    const uint8_t ep_id = 1;

    /* Temperature: int16 in units of 0.01 °C
     * e.g. 25.30 °C → 2530
     * SOURCE: ezbee/zcl/cluster/temperature_measurement_desc.h */
    int16_t temp_val = (int16_t)(reading->temperature_c * 100.0f);
    esp_zigbee_lock_acquire(portMAX_DELAY);
    esp_err_t t_err = ezb_zcl_set_attr_value(ep_id,
                           EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT,
                           EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID,
                           EZB_ZCL_STD_MANUF_CODE,
                           (uint8_t *)&temp_val,
                           false);

    /* Humidity: uint16 in units of 0.01 %
     * e.g. 55.00% → 5500
     * SOURCE: ezbee/zcl/cluster/rel_humidity_measurement_desc.h */
    uint16_t rh_val = (uint16_t)(reading->humidity_pct * 100.0f);
    esp_err_t h_err = ezb_zcl_set_attr_value(ep_id,
                           EZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
                           EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MEASURED_VALUE_ID,
                           EZB_ZCL_STD_MANUF_CODE,
                           (uint8_t *)&rh_val,
                           false);

    /* CO2: single float in [0.0, 1.0] where 1.0 = 1,000,000 ppm
     * e.g. 800 ppm → 0.0008f
     * SOURCE: ezbee/zcl/cluster/carbon_dioxide_measurement_desc.h */
    float co2_val = reading->co2_ppm / 1000000.0f;
    esp_err_t c_err = ezb_zcl_set_attr_value(ep_id,
                           EZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT,
                           EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_CARBON_DIOXIDE_MEASUREMENT_MEASURED_VALUE_ID,
                           EZB_ZCL_STD_MANUF_CODE,
                           (uint8_t *)&co2_val,
                           false);

    esp_zigbee_lock_release();

    ESP_LOGI(TAG, "ZCL report results: temp=0x%x humidity=0x%x co2=0x%x (co2_raw=%.6f from %.0f ppm)",
             t_err, h_err, c_err, co2_val, reading->co2_ppm);

    return ESP_OK;
}
