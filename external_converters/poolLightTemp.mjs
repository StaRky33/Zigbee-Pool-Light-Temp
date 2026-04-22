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
                result.temperature = parseFloat((msg.data['measuredValue'] / 100).toFixed(2));
            }
            const val = (msg.data || {})[0xFF00];
            if (val !== undefined) {
                result.temperature_offset = parseFloat((val / 100).toFixed(2));
            }
            return result;
        },
    },
    on_off: {
        cluster: 'genOnOff',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.endpoint.ID === 10) {
                return {state: msg.data['onOff'] === 1 ? 'ON' : 'OFF'};
            }
        },
    },
};

const tzLocal = {
    on_off: {
        key: ['state'],
        convertSet: async (entity, key, value, meta) => {
            const endpoint = meta.device.getEndpoint(10);
            await endpoint.command('genOnOff', value.toLowerCase(), {});
            return {state: {state: value.toUpperCase()}};
        },
        convertGet: async (entity, key, meta) => {
            const endpoint = meta.device.getEndpoint(10);
            await endpoint.read('genOnOff', ['onOff']);
        },
    },
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
    temperature_get: {
        key: ['temperature'],
        convertGet: async (entity, key, meta) => {
            const endpoint = meta.device.getEndpoint(11);
            await endpoint.read('msTemperatureMeasurement', ['measuredValue']);
        },
    },
};

export default {
    zigbeeModel: ['PoolLightTemp'],
    model: 'PoolLightTemp',
    vendor: 'STARKYDIY',
    description: 'Pool controller — pump relay (EP10) + NTC temperature sensor with offset (EP11)',

    fromZigbee: [fzLocal.on_off, fzLocal.temp_and_offset],
    toZigbee:   [tzLocal.on_off, tzLocal.temperature_get, tzLocal.temp_offset],

    exposes: [
        e.switch(),
        e.temperature().withAccess(5),
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
        const ep10 = device.getEndpoint(10);
        const ep11 = device.getEndpoint(11);
        await ep10.bind('genOnOff', coordinatorEndpoint);
        await ep11.bind('msTemperatureMeasurement', coordinatorEndpoint);
        await ep11.configureReporting('msTemperatureMeasurement', [
            {attribute: 'measuredValue', minimumReportInterval: 10, maximumReportInterval: 1800, reportableChange: 10},
        ]);
        await ep11.read('msTemperatureMeasurement', [0xFF00]);
    },
};