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

/* Interval between two temperature reads/reports (ms) */
#define TEMP_REPORT_INTERVAL_MS 30000

/* ── OTA identity ── */
#define OTA_ENDPOINT          1
#define OTA_MANUFACTURER_CODE 0x1234   // arbitrary — must match .ota file
#define OTA_IMAGE_TYPE        0x0000
#define OTA_FILE_VERSION      0x01000000  // increment for each new release