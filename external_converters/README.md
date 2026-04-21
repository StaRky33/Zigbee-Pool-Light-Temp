# PoolLightTemp — Zigbee2MQTT External Converter

Converter for the ESP32-C6 pool controller.

## Exposed entities

| Entity | Type | Description |
|---|---|---|
| `state` (relay) | binary | ON/OFF — pump control via GPIO4 relay |
| `temperature` (sensor) | numeric | Water temperature in °C (NTC probe) |
| `temperature_offset` (sensor) | numeric | Calibration offset ±10°C, step 0.01°C |
| `linkquality` | numeric | Zigbee signal quality (0–255) |

## Installation

### 1. Copy the folder

Copy the `pool_controller/` folder into your Zigbee2MQTT configuration directory:

```
/config/zigbee2mqtt/           ← Z2M config directory
└── pool_controller/
    └── pool_controller.js     ← this file
```

### 2. Edit `configuration.yaml`

Add the following to your Zigbee2MQTT `configuration.yaml`:

```yaml
external_converters:
  - pool_controller/pool_controller.js
```

### 3. Restart Zigbee2MQTT

Restart the addon or the Zigbee2MQTT service.

### 4. Re-pair the device

If the device is already paired, remove it from Z2M and re-pair it so the new converter is applied.

## Using the offset

From Home Assistant or via MQTT:

```yaml
# MQTT service call in Home Assistant
service: mqtt.publish
data:
  topic: zigbee2mqtt/PoolLightTemp/set
  payload: '{"temperature_offset": 0.50}'
```

The offset is persistent on the ESP32 side (stored as a ZCL attribute).

## Firmware structure

- **Endpoint 10** — On/Off Cluster (0x0006) → GPIO4 relay
- **Endpoint 11** — Temperature Measurement Cluster (0x0402)
  - Standard attribute `measuredValue` (0x0000) — corrected temperature
  - Custom attribute `tempOffset` (0xFF00, int16) — offset in hundredths of °C
