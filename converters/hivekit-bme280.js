/**
 * HiveKit BME280-C6 — Zigbee2MQTT external converter
 *
 * Exposes temperature (°C), humidity (%), and pressure (hPa) from the HiveKit
 * BME280 firmware running on XIAO ESP32-C6.
 *
 * ⚠️  UNTESTED — community verification welcome.
 *     Flash and report results: https://github.com/petarivanov-msft/hivekit/issues
 *
 * Install: copy to <z2m-data>/external_converters/hivekit-bme280.js
 * Reference in configuration.yaml:
 *   external_converters:
 *     - hivekit-bme280.js
 *
 * Firmware identifiers (Basic cluster):
 *   manufacturerName = "HiveKit"
 *   modelIdentifier  = "hk-bme280-c6"
 *
 * Clusters:
 *   0x0402 msTemperatureMeasurement  (int16, ×100 °C)
 *   0x0405 msRelativeHumidity        (uint16, ×100 %)
 *   0x0403 msPressureMeasurement     (int16, hPa integer)
 *
 * Pattern mirrors hivekit-scd40.js with pressure added instead of CO2.
 * Reference for Z2M modernExtend pressure(): zigbee-herdsman-converters/lib/modernExtend.ts
 * Known BME280 Z2M devices with pressure: iCasa IQKL-3210-GLR02, Develco SMSZB-120
 */

const m = require('zigbee-herdsman-converters/lib/modernExtend');

const definition = {
    zigbeeModel: ['hk-bme280-c6'],
    vendor: 'HiveKit',
    model: 'hk-bme280-c6',
    description: 'Temperature + Humidity + Pressure (BME280, ESP32-C6) ☆ Untested',
    fingerprint: [
        {manufacturerName: 'HiveKit', modelID: 'hk-bme280-c6'},
    ],
    extend: [
        m.temperature(),
        m.humidity(),
        m.pressure(),
    ],
};

module.exports = definition;
