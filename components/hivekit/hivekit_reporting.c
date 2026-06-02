/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * hivekit_reporting.c — Zigbee attribute reporting helpers
 *
 * Phase 1: basic attribute set + report.
 * Phase 2: add configurable min/max reporting intervals + threshold reporting.
 *
 * API VERIFICATION (2026-05-17):
 *   ezb_zcl_set_attr_value()     → ezbee/zcl/zcl_core.h (included via ezbee/zcl.h)
 *   ezb_zcl_report_attr_cmd_req()→ ezbee/zcl/zcl_reporting.h
 *   EZB_ZCL_CLUSTER_SERVER       → ezbee/zcl/zcl_type.h
 *   EZB_ZCL_STD_MANUF_CODE       → ezbee/zcl/zcl_type.h
 *   EZB_ZCL_CMD_DIRECTION_TO_CLI → ezbee/zcl/zcl_type.h
 *   EZB_ADDR_MODE_NONE           → ezbee/core_types.h
 *   esp_zigbee_lock_acquire()    → esp_zigbee.h
 *   esp_zigbee_lock_release()    → esp_zigbee.h
 *
 * NOTE: The attribute reporting trigger after set is automatic if reporting is
 * configured in the ZCL attribute descriptor (via idf_component.yml + sdkconfig).
 * The explicit ezb_zcl_report_attr_cmd_req() call here allows forced reports
 * (e.g. on button press). It is NOT required for normal periodic reporting.
 */

#include "hivekit.h"
#include "esp_log.h"
#include "esp_zigbee.h"
#include "ezbee/zcl.h"
#include "ezbee/zcl/zcl_common.h"
#include "ezbee/zcl/zcl_reporting.h"

static const char *TAG = "hivekit_reporting";

/**
 * @brief Force-send a ZCL attribute report for a given attribute.
 *
 * Optional. Normal Zigbee reporting happens automatically once attribute
 * reporting is configured (min/max interval + change threshold).
 *
 * Call this to immediately push a value to the coordinator (e.g. on button press).
 *
 * @param ep_id      Source endpoint
 * @param cluster_id ZCL cluster ID
 * @param attr_id    Attribute ID to report
 * @return ESP_OK on success
 */
esp_err_t hivekit_force_report(uint8_t ep_id, uint16_t cluster_id, uint16_t attr_id)
{
    /* SOURCE: ezbee/zcl/zcl_reporting.h — ezb_zcl_report_attr_cmd_t */
    ezb_zcl_report_attr_cmd_t cmd = {
        .cmd_ctrl = {
            .fc.direction       = EZB_ZCL_CMD_DIRECTION_TO_CLI,
            .dst_addr.addr_mode = EZB_ADDR_MODE_NONE, /* broadcast / binding */
            .src_ep             = ep_id,
            .cluster_id         = cluster_id,
        },
        .payload = {
            .attr_id = attr_id,
        },
    };

    esp_zigbee_lock_acquire(portMAX_DELAY);
    /* SOURCE: ezbee/zcl/zcl_reporting.h — ezb_zcl_report_attr_cmd_req() */
    ezb_err_t ret = ezb_zcl_report_attr_cmd_req(&cmd);
    esp_zigbee_lock_release();

    if (ret != EZB_ERR_NONE) {
        ESP_LOGW(TAG, "Report attr failed: cluster=0x%04x attr=0x%04x err=0x%04x",
                 cluster_id, attr_id, ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}
