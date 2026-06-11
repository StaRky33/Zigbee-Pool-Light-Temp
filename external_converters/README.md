# PoolLightTemp — Zigbee2MQTT External Converter

![Version](https://img.shields.io/badge/version-1.0.1-blue)
![Zigbee2MQTT](https://img.shields.io/badge/Zigbee2MQTT-1.36+-green)

External converter for the Seeed Studio XIAO ESP32C6 pool controller running Zigbee firmware v1.0.1+.

## Exposed entities

| Entity | Type | Zigbee Cluster | Description |
|---|---|---|---|
| `state` | Switch | genOnOff (0x0006) | Pool light on/off control (Endpoint 10) |
| `temperature` | Temperature (°C) | msTemperatureMeasurement (0x0402) | NTC temperature reading (Endpoint 11) |
| `temperature_offset` | Numeric (±10°C) | msTemperatureMeasurement (0xFF00) | Temperature calibration offset |
| `ntc_beta` | Numeric (K) | msTemperatureMeasurement (0xFF01) | NTC thermistor beta coefficient |
| `report_interval` | Numeric (min) | msTemperatureMeasurement (0xFF02) | Temperature report interval |
| `fw_version` | Text | genBasic (0x0000) | Firmware version string |

## Custom attributes

The device implements three manufacturer-specific attributes on Endpoint 11:

| Attribute ID | Name | Type | Range | Default | Description |
|---|---|---|---|---|---|
| 0xFF00 | temperature_offset | S16 | -100 to +100 (×0.1°C) | 0 | Temperature calibration (±10°C) |
| 0xFF01 | ntc_beta | U16 | 3000-5000 | 3950 | NTC beta coefficient in Kelvin |
| 0xFF02 | report_interval | U8 | 1-60 | 5 | Reporting interval in minutes |

> **Note**: The `temperature_offset` value is stored in units of 0.1°C. A value of 50 represents +5.0°C.

## Installation

### 1. Copy the converter file

Copy `poolLightTemp.mjs` to your Zigbee2MQTT external converters directory:

```
/config/zigbee2mqtt/           ← Z2M config directory
└── external_converters/
    └── poolLightTemp.mjs      ← this file
```

### 2. Edit `configuration.yaml`

Add the external converter to your Zigbee2MQTT configuration:

```yaml
external_converters:
  - poolLightTemp.mjs
```

### 3. Restart Zigbee2MQTT

Restart the addon or the Zigbee2MQTT service:

```bash
# If running as Home Assistant addon
Restart the "Zigbee2MQTT" addon

# If running as standalone
sudo systemctl restart zigbee2mqtt
```

### 4. Re-pair the device

If the device was previously paired, you'll need to force remove it and re-pair:

1. Go to Zigbee2MQTT → Settings → Devices
2. Find your PoolLightTemp device
3. Click "Force Remove"
4. Put the device in pairing mode (reset or power cycle)
5. The device should be discovered with the new converter

---

## Configuration

### Setting temperature offset

Offset the temperature reading to compensate for sensor placement or calibration:

```yaml
# MQTT service call in Home Assistant
service: mqtt.publish
data:
  topic: zigbee2mqtt/PoolLightTemp/set
  payload: '{"temperature_offset": 0.50}'   # +0.50°C
```

```yaml
# Subtract 1°C
service: mqtt.publish
data:
  topic: zigbee2mqtt/PoolLightTemp/set
  payload: '{"temperature_offset": -10}'    # -1.0°C (stored as -10 = -1.0°C)
```

### Setting NTC beta coefficient

Update the beta coefficient if using a different NTC thermistor:

```yaml
service: mqtt.publish
data:
  topic: zigbee2mqtt/PoolLightTemp/set
  payload: '{"ntc_beta": 3950}'   # Default 10kΩ NTC beta value
```

Common beta values:
- 3950 — Most common 10kΩ NTC thermistors
- 3435 — General purpose 10kΩ NTC
- 4250 — High-temperature NTC sensors

### Setting report interval

Change how often the device reports temperature (1-60 minutes):

```yaml
service: mqtt.publish
data:
  topic: zigbee2mqtt/PoolLightTemp/set
  payload: '{"report_interval": 10}'   # Report every 10 minutes
```

> **Note**: Changing the report interval requires firmware v1.0.1 or newer.

### Reading current values

Query the current settings:

```yaml
# Get current temperature offset
service: mqtt.publish
data:
  topic: zigbee2mqtt/PoolLightTemp/get
  payload: '{"temperature_offset": ""}'
```

```yaml
# Get current NTC beta
service: mqtt.publish
data:
  topic: zigbee2mqtt/PoolLightTemp/get
  payload: '{"ntc_beta": ""}'
```

---

## Home Assistant integration

After pairing, the device will appear in Home Assistant with the following entities:

### Device info
- **Model**: PoolLightTemp
- **Manufacturer**: STARKYDIY
- **Firmware**: Displayed in entity state

### Entities

| Entity ID | Type | Description |
|---|---|---|
| `switch.poollighttemp` | Switch | Pool light on/off |
| `sensor.poollighttemp_temperature` | Temperature | Water temperature |
| `sensor.poollighttemp_fw_version` | Sensor | Firmware version |
| `number.poollighttemp_temperature_offset` | Number | Temperature offset |
| `number.poollighttemp_ntc_beta` | Number | NTC beta coefficient |

### Automation example

```yaml
automation:
  - alias: "Pool temperature alert"
    trigger:
      - platform: numeric_state
        entity_id: sensor.poollighttemp_temperature
        above: 30
    action:
      - service: notify.mobile_app
        data:
          message: "Pool water temperature above 30°C!"
```

---

## Troubleshooting

### Device not discovered with converter

If the device pairs but doesn't show the custom entities:

1. Check Zigbee2MQTT logs for converter loading errors
2. Ensure the file name matches exactly (case-sensitive)
3. Verify `external_converters` path in configuration.yaml

### Configuration fails with UNSUPPORTED_ATTRIBUTE

If you see errors reading custom attributes during configuration:

- This is normal on first pairing — the attributes will be created after the first write
- The converter handles this gracefully and won't fail device configuration

### Temperature readings seem off

1. Check NTC probe wiring and connections
2. Verify the 10kΩ reference resistor is correctly connected
3. Adjust `temperature_offset` to compensate for installation location
4. If using a different NTC probe, update `ntc_beta` to match its specifications

---

## Firmware version compatibility

| Converter Version | Firmware Version | Notes |
|---|---|---|
| 1.0.0 | 1.0.0 | Original release, temperature_offset & ntc_beta only |
| 1.0.1 | 1.0.1 | Added report_interval, improved error handling |

---

## Changelog

### v1.0.1
- Added `report_interval` attribute (0xFF02) for configurable temperature reporting
- Improved error handling for unsupported attributes during configuration
- Better attribute ID compatibility (checks both 0xFFxx and decimal formats)

### v1.0.0
- Initial release
- Temperature offset (0xFF00) and NTC beta (0xFF01) attributes
- Basic read/write support for all custom attributes