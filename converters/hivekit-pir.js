/**
 * HiveKit PIR-C6 — Zigbee2MQTT external converter
 *
 * Exposes occupancy (motion detected / unoccupied) from the HiveKit PIR firmware
 * running on XIAO ESP32-C6.
 *
 * ⚠️  UNTESTED — community verification welcome.
 *     Flash and report results: https://github.com/petarivanov-msft/hivekit/issues
 *
 * Install: copy to <z2m-data>/external_converters/hivekit-pir.js
 * Reference in configuration.yaml:
 *   external_converters:
 *     - hivekit-pir.js
 *
 * Firmware identifiers (Basic cluster):
 *   manufacturerName = "HiveKit"
 *   modelIdentifier  = "hk-pir-c6"
 *
 * Clusters:
 *   0x0406 Occupancy Sensing (bitmap8 bit 0: 1=occupied, 0=unoccupied)
 *
 * Z2M expose:
 *   occupancy: binary (true = motion detected, false = no motion)
 *
 * Pattern: modernExtend m.occupancy() handles ZCL cluster 0x0406 attribute 0x0000.
 * Reference: zigbee-herdsman-converters/lib/modernExtend.ts
 * URL: https://github.com/Koenkk/zigbee-herdsman-converters/blob/master/src/lib/modernExtend.ts
 */

const m = require('zigbee-herdsman-converters/lib/modernExtend');

const definition = {
    zigbeeModel: ['hk-pir-c6'],
    vendor: 'HiveKit',
    model: 'hk-pir-c6',
    description: 'Motion sensor (PIR, ESP32-C6) ☆ Untested',
    fingerprint: [
        {manufacturerName: 'HiveKit', modelID: 'hk-pir-c6'},
    ],
    extend: [
        m.occupancy(),
    ],
};

module.exports = definition;
