import * as m from 'zigbee-herdsman-converters/lib/modernExtend';
import {presets as e} from 'zigbee-herdsman-converters/lib/exposes';
import * as fz from 'zigbee-herdsman-converters/converters/fromZigbee';
import * as tz from 'zigbee-herdsman-converters/converters/toZigbee';

const fzLocal = {
    temp_and_offset: {
        cluster: 'msTemperatureMeasurement',
        type: ['readResponse', 'attributeReport'],
        convert: (model, msg, publish, options, meta) => {
            const result = {};
            if (msg.data.hasOwnProperty('measuredValue')) {
                result.temperature_11 = parseFloat((msg.data['measuredValue'] / 100).toFixed(2));
            }
            const val = (msg.data || {})[0xFF00];
            if (val !== undefined) {
                result.temperature_offset = parseFloat((val / 100).toFixed(2));
            }
            return result;
        },
    },
};

const tzLocal = {
    temp_offset: {
        key: ['temperature_offset'],
        convertSet: async (entity, key, value, meta) => {
            const endpoint = meta.device.getEndpoint(11);
            const v = Math.round(parseFloat(value) * 100);
            await endpoint.write(
                'msTemperatureMeasurement',
                {0xFF00: {value: v, type: 0x29}},
            );
            return {state: {temperature_offset: parseFloat(value)}};
        },
        convertGet: async (entity, key, meta) => {
            const endpoint = meta.device.getEndpoint(11);
            await endpoint.read('msTemperatureMeasurement', [0xFF00]);
        },
    },
};

export default {
    zigbeeModel: ['PoolLightTemp'],
    model: 'PoolLightTemp',
    vendor: 'STARKYDIY',
    description: 'Pool controller — pump relay (EP10) + NTC temperature sensor with offset (EP11)',

    extend: [
        m.deviceEndpoints({endpoints: {'1': 1, '10': 10, '11': 11}}),
    ],

    fromZigbee: [fz.on_off, fzLocal.temp_and_offset],
    toZigbee: [tz.on_off, tz.temperature, tzLocal.temp_offset],

    exposes: [
        e.switch().withEndpoint('10'),
        e.temperature().withEndpoint('11'),
        {
            type: 'numeric',
            name: 'temperature_offset',
            label: 'Temperature offset',
            property: 'temperature_offset',
            access: 7,
            category: 'config',
            unit: '°C',
            description: 'Temperature calibration offset (±10°C, step 0.01°C)',
            value_min: -10,
            value_max: 10,
            value_step: 0.01,
        },
    ],

    configure: async (device, coordinatorEndpoint) => {
        const endpoint = device.getEndpoint(11);
        await endpoint.read('msTemperatureMeasurement', [0xFF00]);
    },

    ota: true,
};