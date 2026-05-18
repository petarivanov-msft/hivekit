/**
 * HiveKit SCD40-C6 — Zigbee2MQTT external converter
 *
 * Exposes CO2 (ppm), temperature (°C), and humidity (%) from the HiveKit
 * SCD40 firmware running on XIAO ESP32-C6.
 *
 * Install: copy to <z2m-data>/external_converters/hivekit-scd40.js
 * Reference in configuration.yaml:
 *   external_converters:
 *     - hivekit-scd40.js
 *
 * Firmware identifiers (Basic cluster):
 *   manufacturerName = "HiveKit"
 *   modelIdentifier  = "hk-scd40-c6"
 *
 * Clusters:
 *   0x0402 msTemperatureMeasurement  (int16, ×100 °C)
 *   0x0405 msRelativeHumidity        (uint16, ×100 %)
 *   0x040d msCO2                     (single float, 1.0 = 1,000,000 ppm)
 */

const m = require('zigbee-herdsman-converters/lib/modernExtend');

const definition = {
    zigbeeModel: ['hk-scd40-c6'],
    vendor: 'HiveKit',
    model: 'hk-scd40-c6',
    description: 'CO2 + Temperature + Humidity (SCD40/SCD41, ESP32-C6)',
    fingerprint: [
        {manufacturerName: 'HiveKit', modelID: 'hk-scd40-c6'},
    ],
    extend: [
        m.co2(),
        m.temperature(),
        m.humidity(),
    ],
};

module.exports = definition;
