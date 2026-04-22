/*
 * Pool Controller – ESP32-C6
 * API esp_zb_* v1.x — based on HA_on_off_light example
 *
 * EP 10 : On/Off  → GPIO4 relay
 * EP 11 : Temperature Measurement (NTC ADC)
 *         + custom offset attribute 0xFF00 (int16, hundredths of °C)
 *
 * Device identity (must match poolLight.mjs external converter):
 *   zigbeeModel : PoolLightTemp
 *   vendor      : STARKYDIY
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_zb_pool.h"
#include "relay.h"
#include "ntc.h"

static const char *TAG = "POOL_CTRL";
static volatile bool s_temp_update_requested = false;

/* Temperature offset writable via Zigbee (hundredths of °C) */
static int16_t s_temp_offset = 0;

/* Cluster IDs */
#define ZCL_CLUSTER_TEMP_MEASUREMENT    ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT
#define ZCL_CLUSTER_ON_OFF              ESP_ZB_ZCL_CLUSTER_ID_ON_OFF

/* ── Attribute callback ─────────────────────────────────── */
static esp_err_t zb_attribute_handler(
        const esp_zb_zcl_set_attr_value_message_t *message)
{
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "empty message");
    ESP_RETURN_ON_FALSE(
        message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS,
        ESP_ERR_INVALID_ARG, TAG, "ZCL status failed");

    ESP_LOGI(TAG, "attr write — ep:%d cluster:0x%04x attr:0x%04x type:0x%02x",
             message->info.dst_endpoint,
             message->info.cluster,
             message->attribute.id,
             message->attribute.data.type);

    /* EP 10 : relay On/Off */
    if (message->info.dst_endpoint == HA_RELAY_ENDPOINT &&
        message->info.cluster == ZCL_CLUSTER_ON_OFF) {
        bool on = *(bool *)message->attribute.data.value;
        relay_set(on);
        ESP_LOGI(TAG, "Relay → %s", on ? "ON" : "OFF");
    }

    /* EP 11 : custom temperature offset */
    if (message->info.dst_endpoint == HA_TEMP_ENDPOINT &&
        message->info.cluster == ZCL_CLUSTER_TEMP_MEASUREMENT &&
        message->attribute.id == ATTR_TEMP_OFFSET) {
        s_temp_offset = *(int16_t *)message->attribute.data.value;
        ESP_LOGI(TAG, "Temperature offset → %+.2f°C", s_temp_offset / 100.0f);
        s_temp_update_requested = true;  // force immediate update
    }

    return ESP_OK;
}

static esp_err_t zb_action_handler(
        esp_zb_core_action_callback_id_t callback_id,
        const void *message)
{
    if (callback_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        return zb_attribute_handler(
            (esp_zb_zcl_set_attr_value_message_t *)message);
    } else if (callback_id == ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID) {
        /* Default Response from coordinator — normal ZCL behaviour, ignore */
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Unhandled action: 0x%x", callback_id);
        return ESP_OK;
    }
}

/* ── Signal handler (mirrors HA_on_off_light example) ─── */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p = signal_struct->p_app_signal;
    esp_err_t  err = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialising Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(
            ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err == ESP_OK) {
            relay_init();
            ntc_init();
            ESP_LOGI(TAG, "Device started (%s)",
                     esp_zb_bdb_is_factory_new()
                     ? "factory-reset" : "reboot");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Starting network steering...");
                esp_zb_bdb_start_top_level_commissioning(
                    ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
        } else {
            ESP_LOGW(TAG, "Stack init failed: %s", esp_err_to_name(err));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err == ESP_OK) {
            ESP_LOGI(TAG,
                "Network joined — PAN:0x%04hx Ch:%d Addr:0x%04hx",
                esp_zb_get_pan_id(),
                esp_zb_get_current_channel(),
                esp_zb_get_short_address());
        } else {
            ESP_LOGW(TAG, "Steering failed, retrying...");
            esp_zb_bdb_start_top_level_commissioning(
                ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        break;

    default:
        /* Only warn on actual errors, log info otherwise */
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ZDO signal: 0x%x status: %s",
                     sig_type, esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "ZDO signal: 0x%x status: %s",
                     sig_type, esp_err_to_name(err));
        }
        break;
    }
}

/* ── Zigbee task ────────────────────────────────────────── */
static void esp_zb_task(void *pvParameters)
{
    /* Stack init — End Device role */
    esp_zb_cfg_t zb_nwk_cfg = {
        .esp_zb_role         = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = 8,      // ~64 minutes
            .keep_alive = 3000,   // ms
        },
    };
    esp_zb_init(&zb_nwk_cfg);

    /* ── Basic cluster ──────────────────────────────────── */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE,
    };
    esp_zb_attribute_list_t *basic_attrs =
        esp_zb_basic_cluster_create(&basic_cfg);

    static const char s_model[]  = ZB_DEVICE_MODEL;
    static const char s_vendor[] = ZB_DEVICE_VENDOR;

    
    esp_zb_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_CLUSTER_ID_BASIC,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
        (void *)s_model);

    esp_zb_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_CLUSTER_ID_BASIC,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
        (void *)s_vendor);

    /* ── EP 10 : On/Off relay ───────────────────────────── */
    esp_zb_on_off_light_cfg_t relay_cfg =
        ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
    esp_zb_endpoint_config_t ep10_cfg = {
        .endpoint           = HA_RELAY_ENDPOINT,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(ep_list,
        esp_zb_on_off_light_clusters_create(&relay_cfg),
        ep10_cfg);

    /* ── EP 11 : Temperature Measurement ───────────────── */
    esp_zb_temperature_meas_cluster_cfg_t temp_cfg = {
        .measured_value = 0,
        .min_value      = -4000,  // -40.00°C
        .max_value      =  8500,  // +85.00°C
    };
    esp_zb_attribute_list_t *temp_attrs =
        esp_zb_temperature_meas_cluster_create(&temp_cfg);

    /* Custom offset attribute: int16, read/write, default 0 */
    int16_t offset_default = 0;
    esp_zb_custom_cluster_add_custom_attr(
        temp_attrs,
        ATTR_TEMP_OFFSET,
        ESP_ZB_ZCL_ATTR_TYPE_S16,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &offset_default);

    esp_zb_cluster_list_t *temp_clusters =
        esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(temp_clusters, basic_attrs,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_temperature_meas_cluster(
        temp_clusters, temp_attrs,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t ep11_cfg = {
        .endpoint           = HA_TEMP_ENDPOINT,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, temp_clusters, ep11_cfg);

    /* ── Register + start ───────────────────────────────── */
    esp_zb_device_register(ep_list);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(0x07FFF800);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();   // blocking
}

/* ── NTC read task → Zigbee attribute report ────────────── */
static void temp_report_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        int16_t raw;
        if (ntc_read(&raw)) {
            int16_t corrected = raw + s_temp_offset;

            esp_zb_zcl_set_attribute_val(
                HA_TEMP_ENDPOINT,
                ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
                &corrected, false);

            ESP_LOGI(TAG, "Temp: %.2f°C (offset %+.2f°C)",
                     corrected / 100.0f,
                     s_temp_offset / 100.0f);
        }
        s_temp_update_requested = false;

        /* Attendre soit l'intervalle normal soit une demande de mise à jour */
        for (int i = 0; i < (TEMP_REPORT_INTERVAL_MS / 1000); i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (s_temp_update_requested) break;
        }
    }
}

/* ── app_main ───────────────────────────────────────────── */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(esp_zb_task,      "esp_zb_task", 8192, NULL, 5, NULL);
    xTaskCreate(temp_report_task, "temp_task",   4096, NULL, 4, NULL);
}
