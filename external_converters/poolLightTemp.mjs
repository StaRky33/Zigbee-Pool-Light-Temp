// ── Firmware version — keep in sync with version.h ──────────
const FW_VERSION = '1.0.2';

import {presets as e} from 'zigbee-herdsman-converters/lib/exposes';

const fzLocal = {
    temp_and_offset: {
        cluster: 'msTemperatureMeasurement',
        type: ['readResponse', 'attributeReport'],
        convert: (model, msg, publish, options, meta) => {
            const result = {};
            if (msg.data.hasOwnProperty('measuredValue')) {
                result.temperature = parseFloat((msg.data['measuredValue'] / 100).toFixed(2));
            }
            const offset = (msg.data || {})[0xFF00];
            if (offset !== undefined) {
                result.temperature_offset = parseFloat((offset / 100).toFixed(2));
            }
            const beta = (msg.data || {})[0xFF01];
            if (beta !== undefined) {
                result.ntc_beta = beta;
            }
            const interval = (msg.data || {})[0xFF02];
            if (interval !== undefined) {
                result.report_interval = interval;
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
    basic_info: {
        cluster: 'genBasic',
        type: ['readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const result = {};
            if (msg.data.swBuildId) {
                result.fw_version = msg.data.swBuildId;
            }
            return result;
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
    ntc_beta: {
        key: ['ntc_beta'],
        convertSet: async (entity, key, value, meta) => {
            const endpoint = meta.device.getEndpoint(11);
            await endpoint.write(
                'msTemperatureMeasurement',
                {0xFF01: {value: Math.round(value), type: 0x21}},
            );
            return {state: {ntc_beta: Math.round(value)}};
        },
        convertGet: async (entity, key, meta) => {
            const endpoint = meta.device.getEndpoint(11);
            await endpoint.read('msTemperatureMeasurement', [0xFF01]);
        },
    },
    report_interval: {
        key: ['report_interval'],
        convertSet: async (entity, key, value, meta) => {
            const endpoint = meta.device.getEndpoint(11);
            const v = Math.min(60, Math.max(1, Math.round(value)));
            await endpoint.write(
                'msTemperatureMeasurement',
                {0xFF02: {value: v, type: 0x20}},  // uint8
            );
            return {state: {report_interval: v}};
        },
        convertGet: async (entity, key, meta) => {
            const endpoint = meta.device.getEndpoint(11);
            await endpoint.read('msTemperatureMeasurement', [0xFF02]);
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
    description: `Pool controller — pump relay (EP10) + NTC temperature sensor with offset (EP11) [fw:${FW_VERSION}]`,

    fromZigbee: [fzLocal.on_off, fzLocal.temp_and_offset, fzLocal.basic_info],
    toZigbee:   [tzLocal.on_off, tzLocal.temperature_get, tzLocal.temp_offset, tzLocal.ntc_beta, tzLocal.report_interval],

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
        {
            type: 'numeric',
            name: 'ntc_beta',
            label: 'NTC Beta',
            property: 'ntc_beta',
            access: 7,
            category: 'config',
            unit: 'K',
            description: 'Beta coefficient of the NTC thermistor. Formula: Beta = ln(R/R0) / (1/T - 1/T0) where R0=10kΩ at T0=25°C.',
            value_min: 3000,
            value_max: 5000,
            value_step: 1,
        },
        {
            type: 'numeric',
            name: 'report_interval',
            label: 'Report interval',
            property: 'report_interval',
            access: 7,
            category: 'config',
            unit: 'min',
            description: 'Temperature report interval (1–60 min). Change takes effect on next report cycle.',
            value_min: 1,
            value_max: 60,
            value_step: 1,
        },
        {
            type: 'text',
            name: 'fw_version',
            label: 'Firmware version',
            property: 'fw_version',
            access: 1,
            category: 'diagnostic',
            description: 'Firmware version of the device (from ZCL SWBuildID)',
        },
    ],

    configure: async (device, coordinatorEndpoint) => {
        const ep10 = device.getEndpoint(10);
        const ep11 = device.getEndpoint(11);

        // Bind pour recevoir les reports spontanés de l'ESP
        await ep10.bind('genOnOff', coordinatorEndpoint);
        await ep11.bind('msTemperatureMeasurement', coordinatorEndpoint);

        // Lire l'état initial du relay → corrige "state: null" au démarrage
        await ep10.read('genOnOff', ['onOff']);

        // Lire les attributs custom au démarrage
        await ep11.read('msTemperatureMeasurement', [0xFF00]);
        await ep11.read('msTemperatureMeasurement', [0xFF01]);
        await ep11.read('msTemperatureMeasurement', [0xFF02]);

        // Lire la version firmware depuis le cluster Basic de EP11
        const basic = await ep11.read('genBasic', ['swBuildId', 'appVersion']);
        if (basic && basic.swBuildId) {
            device.meta.fw_version = basic.swBuildId;
            device.save();
        }

        // PAS de configureReporting : l'ESP gère seul le timing via temp_report_task
    },
};
