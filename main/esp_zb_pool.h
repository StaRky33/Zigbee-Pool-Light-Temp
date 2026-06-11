#pragma once

/* ── Device identity (must match the Zigbee2MQTT external converter) ── */
#define ZB_DEVICE_MODEL         "\x0D""PoolLightTemp"; /* Model identifier */
#define ZB_DEVICE_VENDOR        "\x09""STARKYDIY"    /* Custom manufacturer name */

/* ── Zigbee endpoints ── */
#define HA_RELAY_ENDPOINT       10   // On/Off → GPIO4 relay
#define HA_TEMP_ENDPOINT        11   // Temperature Measurement

/*
 * Manufacturer-specific attribute on the Temperature Measurement cluster.
 * Type : int16, unit : hundredths of °C (e.g. 50 = +0.50°C)
 * Access : read/write via Zigbee2MQTT set command
 */
#define ATTR_TEMP_OFFSET        0xFF00

/* Value depending on temperature sensor attributes. This should work for NTC 10kΩ 035HP05202 */
#define ATTR_NTC_BETA  0xFF01
#define NTC_BETA_DEFAULT 3950


/*
 * Manufacturer-specific attribute for temperature reporting interval.
 * Type: uint8, unit: minutes
 * Access: read/write via Zigbee2MQTT set command
 * Range: 1-60 minutes
 */
#define ATTR_REPORT_INTERVAL    0xFF02