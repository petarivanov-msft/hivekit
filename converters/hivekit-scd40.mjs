/**
 * HiveKit SCD40-C6 — Zigbee2MQTT external converter (Z2M v2, ESM)
 *
 * Install: copy to <z2m-data>/external_converters/hivekit-scd40.mjs
 *
 * Firmware identifiers (Basic cluster):
 *   manufacturerName = "HiveKit"
 *   modelIdentifier  = "hk-scd40-c6"
 */

import {co2, temperature, humidity} from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['hk-scd40-c6'],
    vendor: 'HiveKit',
    model: 'hk-scd40-c6',
    description: 'CO2 + Temperature + Humidity (SCD40/SCD41, ESP32-C6)',
    icon: 'https://raw.githubusercontent.com/petarivanov-msft/hivekit/main/converters/hivekit-scd40-c6.svg',
    fingerprint: [
        {manufacturerName: 'HiveKit', modelID: 'hk-scd40-c6'},
    ],
    extend: [co2(), temperature(), humidity()],
};
