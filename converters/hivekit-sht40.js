/**
 * HiveKit SHT40-C6 — Zigbee2MQTT external converter
 *
 * Exposes temperature (°C) and humidity (%) from the HiveKit SHT40 firmware
 * running on XIAO ESP32-C6.
 *
 * ⚠️  UNTESTED — community verification welcome.
 *     Flash and report results: https://github.com/petarivanov-msft/hivekit/issues
 *
 * Install: copy to <z2m-data>/external_converters/hivekit-sht40.js
 * Reference in configuration.yaml:
 *   external_converters:
 *     - hivekit-sht40.js
 *
 * Firmware identifiers (Basic cluster):
 *   manufacturerName = "HiveKit"
 *   modelIdentifier  = "hk-sht40-c6"
 *
 * Clusters:
 *   0x0402 msTemperatureMeasurement  (int16, ×100 °C)
 *   0x0405 msRelativeHumidity        (uint16, ×100 %)
 *
 * Pattern mirrors hivekit-scd40.js minus CO2.
 * Reference: zigbee-herdsman-converters/lib/modernExtend.ts
 */

const m = require('zigbee-herdsman-converters/lib/modernExtend');

const definition = {
    zigbeeModel: ['hk-sht40-c6'],
    vendor: 'HiveKit',
    model: 'hk-sht40-c6',
    description: 'Temperature + Humidity (SHT40, ESP32-C6) ☆ Untested',
    fingerprint: [
        {manufacturerName: 'HiveKit', modelID: 'hk-sht40-c6'},
    ],
    extend: [
        m.temperature(),
        m.humidity(),
    ],
};

module.exports = definition;
