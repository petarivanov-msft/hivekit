/**
 * HiveKit SCD40-C6 — Zigbee2MQTT external converter
 *
 * @description
 * External converter for the HiveKit SCD40 firmware running on XIAO ESP32-C6.
 * Exposes CO2 (ppm), temperature (°C), and relative humidity (%).
 *
 * @installation
 * 1. Copy this file to your Z2M `external_converters/` directory:
 *      cp hivekit-scd40.js /config/zigbee2mqtt/external_converters/
 * 2. Restart Zigbee2MQTT (or it may auto-load if watch is enabled).
 * 3. Enable "Permit join" and power on / re-pair the device.
 *
 * Z2M docs: https://www.zigbee2mqtt.io/advanced/more/external_converters.html
 *
 * @zigbeeModel
 * Firmware sets:
 *   Basic cluster manufacturerName = "HiveKit"
 *   Basic cluster modelIdentifier  = "hk-scd40-c6"
 * Z2M matches this file via the `zigbeeModel` array below.
 *
 * @clusters
 *   0x0402  Temperature Measurement — MeasuredValue: int16, ×100 (°C)
 *   0x0405  Relative Humidity       — MeasuredValue: uint16, ×100 (%)
 *   0x040d  CO2 Measurement         — MeasuredValue: single float, ppm/1e6
 *
 * @co2_note
 * ZCL CO2 cluster (0x040d) uses a `single` float where 1.0 = 1,000,000 ppm.
 * Z2M's modernExtend.co2() / fromZigbee.msCO2 multiplies by 1,000,000 to get ppm.
 * Source: zigbee-herdsman cluster definition for msCO2.
 *
 * @converter_names_verified
 * The modernExtend names below are confirmed against:
 *   https://github.com/Koenkk/zigbee-herdsman-converters/blob/master/src/devices/efekta.ts
 *   (co2, temperature, humidity all verified to use m.co2(), m.temperature(), m.humidity())
 *
 * @license Apache-2.0
 */

const {modernExtend} = require('zigbee-herdsman-converters/lib/modernExtend');

const definition = {
    /**
     * zigbeeModel must match the modelIdentifier set in the firmware's Basic cluster.
     * Firmware sets: modelIdentifier = "hk-scd40-c6"
     */
    zigbeeModel: ['hk-scd40-c6'],

    /**
     * Z2M display fields — shown in the device list and entity name.
     */
    vendor: 'HiveKit',
    model: 'hk-scd40-c6',
    description: 'CO2 + Temperature + Humidity (SCD40/SCD41, ESP32-C6)',

    /**
     * Fingerprint allows Z2M to match the device even if modelID hasn't been
     * read yet (e.g. during interview failure recovery).
     *
     * manufacturerName = "HiveKit" (set in firmware Basic cluster)
     */
    fingerprint: [
        {manufacturerName: 'HiveKit', modelID: 'hk-scd40-c6'},
    ],

    /**
     * modernExtend handles all the fromZigbee + toZigbee + exposes boilerplate.
     *
     * m.co2()         → cluster msCO2 (0x040d), attribute measuredValue (float → ppm)
     * m.temperature() → cluster msTemperatureMeasurement (0x0402), int16/100
     * m.humidity()    → cluster msRelativeHumidity (0x0405), uint16/100
     *
     * Source: zigbee-herdsman-converters/lib/modernExtend
     * Verified pattern from: efekta.ts, and other CO2 sensor converters
     */
    extend: [
        modernExtend.co2(),
        modernExtend.temperature(),
        modernExtend.humidity(),
    ],
};

module.exports = definition;
