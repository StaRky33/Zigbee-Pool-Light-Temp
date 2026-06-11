/*
 * Pool Controller – ESP32-C6  [DEBUG VERSION]
 * API esp_zb_* v1.x — based on HA_on_off_light example
 *
 * EP 10 : On/Off  → GPIO4 relay
 * EP 11 : Temperature Measurement (NTC ADC)
 *         + custom offset attribute 0xFF00 (int16, hundredths of °C)
 *         + custom NTC beta attribute 0xFF01 (uint16, K)
 *
 * Device identity (must match poolLightTemp.mjs external converter):
 *   zigbeeModel : PoolLightTemp
 *   vendor      : STARKYDIY
 *
 * ── CHANGES vs previous debug version ──────────────────────
 *  - esp_zb_zcl_set_attribute_val() → esp_zb_zcl_report_attr_cmd_req()
 *    L'ESP envoie maintenant activement ses reports au coordinateur
 *    au lieu de juste mettre à jour l'attribut localement.
 *    Cela remplace le polling Z2M (configureReporting toutes les 10s)
 *    qui conflictait et causait le freeze après ~2h.
 *  - Version firmware exposée via ZCL Basic cluster (SWBuildID + AppVersion)
 *  - Tous les logs debug de la version précédente conservés
 * ────────────────────────────────────────────────────────────
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_zb_pool.h"
#include "relay.h"
#include "ntc.h"
#include "version.h"

static const char *TAG = "POOL_CTRL";

/* Temperature report interval (minutes) — shared with zb_attribute_handler */
static uint8_t s_report_interval_min = 5;  /* Default 5 minutes */
static portMUX_TYPE s_interval_mux = portMUX_INITIALIZER_UNLOCKED;

/* Spinlock pour protéger s_temp_update_requested entre tâches */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_temp_update_requested = false;

/* Temperature offset writable via Zigbee (hundredths of °C) */
static int16_t s_temp_offset = 0;

/* NTC Beta writable via Zigbee (K) — shared with ntc.c */
float g_ntc_beta = NTC_BETA_DEFAULT;

/* Cluster IDs */
#define ZCL_CLUSTER_TEMP_MEASUREMENT    ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT
#define ZCL_CLUSTER_ON_OFF              ESP_ZB_ZCL_CLUSTER_ID_ON_OFF

/* ── Helper : uptime en secondes ──────────────────────────── */
static inline uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

/* ── Envoi actif du report température au coordinateur ───────
 *
 * On met d'abord à jour l'attribut local (set_attribute_val),
 * puis on demande à la stack d'envoyer un ZCL attribute report
 * spontané vers le coordinateur (0x0000).
 *
 * C'est ce mécanisme qui remplace le configureReporting Z2M :
 * l'ESP décide seul du timing, Z2M se contente d'écouter.
 */
static void send_temperature_report(int16_t corrected)
{
    /* 1. Mise à jour locale de l'attribut */
    esp_err_t ret = esp_zb_zcl_set_attribute_val(
        HA_TEMP_ENDPOINT,
        ZCL_CLUSTER_TEMP_MEASUREMENT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
        &corrected, false);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[TEMP] set_attribute_val FAILED: %s", esp_err_to_name(ret));
        return;
    }

    /* 2. Envoi actif du report via la binding table (pas d'adresse explicite) */
    esp_zb_zcl_report_attr_cmd_t cmd = { 0 };
    cmd.zcl_basic_cmd.src_endpoint = HA_TEMP_ENDPOINT;
    cmd.address_mode               = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
    cmd.clusterID                  = ZCL_CLUSTER_TEMP_MEASUREMENT;
    cmd.attributeID                = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID;
    cmd.direction                  = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;

    ret = esp_zb_zcl_report_attr_cmd_req(&cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[TEMP] report_attr_cmd_req FAILED: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "[TEMP] Zigbee report sent → %.2f°C", corrected / 100.0f);
    }
}

/* ── Attribute callback ───────────────────────────────────── */
static esp_err_t zb_attribute_handler(
        const esp_zb_zcl_set_attr_value_message_t *message)
{
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "empty message");
    ESP_RETURN_ON_FALSE(
        message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS,
        ESP_ERR_INVALID_ARG, TAG, "ZCL status failed");

    ESP_LOGI(TAG, "[ZCL] attr write — ep:%d cluster:0x%04x attr:0x%04x type:0x%02x",
             message->info.dst_endpoint,
             message->info.cluster,
             message->attribute.id,
             message->attribute.data.type);

    /* EP 10 : relay On/Off */
    if (message->info.dst_endpoint == HA_RELAY_ENDPOINT &&
        message->info.cluster == ZCL_CLUSTER_ON_OFF) {
        bool on = *(bool *)message->attribute.data.value;
        relay_set(on);
        ESP_LOGI(TAG, "[RELAY] → %s  (uptime %lus)", on ? "ON" : "OFF", uptime_s());
    }

    /* EP 11 : custom temperature offset */
    if (message->info.dst_endpoint == HA_TEMP_ENDPOINT &&
        message->info.cluster == ZCL_CLUSTER_TEMP_MEASUREMENT &&
        message->attribute.id == ATTR_TEMP_OFFSET) {
        s_temp_offset = *(int16_t *)message->attribute.data.value;
        ESP_LOGI(TAG, "[OFFSET] Temperature offset → %+.2f°C", s_temp_offset / 100.0f);
        taskENTER_CRITICAL(&s_mux);
        s_temp_update_requested = true;
        taskEXIT_CRITICAL(&s_mux);
    }

    /* EP 11 : custom NTC beta */
    if (message->info.dst_endpoint == HA_TEMP_ENDPOINT &&
        message->info.cluster == ZCL_CLUSTER_TEMP_MEASUREMENT &&
        message->attribute.id == ATTR_NTC_BETA) {
        g_ntc_beta = (float)(*(uint16_t *)message->attribute.data.value);
        ESP_LOGI(TAG, "[NTC] Beta → %.0f K", g_ntc_beta);
        taskENTER_CRITICAL(&s_mux);
        s_temp_update_requested = true;
        taskEXIT_CRITICAL(&s_mux);
    }

    /* EP 11 : custom report interval */
    if (message->info.dst_endpoint == HA_TEMP_ENDPOINT &&
        message->info.cluster == ZCL_CLUSTER_TEMP_MEASUREMENT &&
        message->attribute.id == ATTR_REPORT_INTERVAL) {
        uint8_t interval = *(uint8_t *)message->attribute.data.value;
        if (interval >= 1 && interval <= 60) {
            taskENTER_CRITICAL(&s_mux);
            s_temp_update_requested = true;
            taskEXIT_CRITICAL(&s_mux);

            taskENTER_CRITICAL(&s_interval_mux);
            s_report_interval_min = interval;
            taskEXIT_CRITICAL(&s_interval_mux);

            ESP_LOGI(TAG, "[INTERVAL] Report interval set to %umin", interval);
        } else {
            ESP_LOGE(TAG, "[INTERVAL] Invalid interval %u (must be 1-60)", interval);
        }
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
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "[ACTION] Unhandled action: 0x%x", callback_id);
        return ESP_OK;
    }
}

/* ── Signal handler ───────────────────────────────────────── */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p = signal_struct->p_app_signal;
    esp_err_t  err = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "[ZB] Initialising Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(
            ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err == ESP_OK) {
            relay_init();
            ntc_init();
            ESP_LOGI(TAG, "[ZB] Device started (%s)",
                     esp_zb_bdb_is_factory_new()
                     ? "factory-reset" : "reboot");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "[ZB] Starting network steering...");
                esp_zb_bdb_start_top_level_commissioning(
                    ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
        } else {
            ESP_LOGW(TAG, "[ZB] Stack init failed: %s", esp_err_to_name(err));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err == ESP_OK) {
            ESP_LOGI(TAG,
                "[ZB] Network joined — PAN:0x%04hx Ch:%d Addr:0x%04hx",
                esp_zb_get_pan_id(),
                esp_zb_get_current_channel(),
                esp_zb_get_short_address());
        } else {
            ESP_LOGW(TAG, "[ZB] Steering failed, retrying...");
            esp_zb_bdb_start_top_level_commissioning(
                ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        break;

    default:
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "[ZB] ZDO signal: 0x%x status: %s",
                     sig_type, esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "[ZB] ZDO signal: 0x%x status: %s",
                     sig_type, esp_err_to_name(err));
        }
        break;
    }
}

/* ── Zigbee task ──────────────────────────────────────────── */
static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = {
        .esp_zb_role         = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = 8,
            .keep_alive = 3000,
        },
    };
    esp_zb_init(&zb_nwk_cfg);

    /* ── Basic cluster ────────────────────────────────────── */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE,
    };
    esp_zb_attribute_list_t *basic_attrs =
        esp_zb_basic_cluster_create(&basic_cfg);

    static const char s_model[]  = ZB_DEVICE_MODEL;
    static const char s_vendor[] = ZB_DEVICE_VENDOR;
    static const char s_sw_ver[] = FW_VERSION_ZCL_STR;
    static uint8_t    s_app_ver  = FW_VERSION_ZCL;


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

    /* ApplicationVersion (0x0001) : octet encodé (MAJOR<<4)|MINOR */
    esp_zb_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_CLUSTER_ID_BASIC,
        ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
        &s_app_ver);

    /* SWBuildID (0x4000) : "1.0.0" — visible dans HA et Z2M */
    esp_zb_cluster_add_attr(basic_attrs,
        ESP_ZB_ZCL_CLUSTER_ID_BASIC,
        ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID,
        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
        (void *)s_sw_ver);

    /* ── EP 10 : On/Off relay ─────────────────────────────── */
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

    /* ── EP 11 : Temperature Measurement ─────────────────── */
    esp_zb_temperature_meas_cluster_cfg_t temp_cfg = {
        .measured_value = 0,
        .min_value      = -4000,
        .max_value      =  8500,
    };
    esp_zb_attribute_list_t *temp_attrs =
        esp_zb_temperature_meas_cluster_create(&temp_cfg);

    /* Add custom attributes to the temperature measurement cluster */
    int16_t offset_default = 0;
    esp_zb_cluster_add_attr(temp_attrs,
        ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        ATTR_TEMP_OFFSET,
        ESP_ZB_ZCL_ATTR_TYPE_S16,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &offset_default);

    uint16_t beta_default = NTC_BETA_DEFAULT;
    esp_zb_cluster_add_attr(temp_attrs,
        ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        ATTR_NTC_BETA,
        ESP_ZB_ZCL_ATTR_TYPE_U16,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &beta_default);

    uint8_t interval_default = 5;  /* Default 5 minutes */
    esp_zb_cluster_add_attr(temp_attrs,
        ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        ATTR_REPORT_INTERVAL,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &interval_default);

    /* Create cluster list with the modified attributes */
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

/* ── Register + start ────────────────────────────────── */
    esp_zb_device_register(ep_list);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(0x07FFF800);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

/* ── NTC read task → Zigbee report actif ─────────────────── */
static void temp_report_task(void *pvParameters)
{
    uint32_t cycle          = 0;
    uint32_t ntc_fail_count = 0;
    uint32_t zb_fail_count  = 0;

    ESP_LOGI(TAG, "[TEMP] Task started, waiting 5s before first read...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        cycle++;
        uint32_t t_start = uptime_s();

        ESP_LOGI(TAG, "[TEMP] ── Cycle #%lu  uptime:%lus ──────────────",
                 cycle, t_start);

        /* Calculate dynamic interval */
        uint32_t report_interval_ms;
        taskENTER_CRITICAL(&s_interval_mux);
        report_interval_ms = s_report_interval_min * 60 * 1000;
        taskEXIT_CRITICAL(&s_interval_mux);

        int16_t raw = 0;
        bool ok = ntc_read(&raw);

        if (ok) {
            int16_t corrected = raw + s_temp_offset;

            ESP_LOGI(TAG, "[TEMP] ntc_read OK — raw:%d (%.2f°C)  offset:%+.2f°C  corrected:%.2f°C",
                     raw, raw / 100.0f, s_temp_offset / 100.0f, corrected / 100.0f);

            /* Envoi actif du report Zigbee (remplace le polling Z2M) */
            send_temperature_report(corrected);

        } else {
            ntc_fail_count++;
            ESP_LOGE(TAG, "[TEMP] ntc_read() FAILED — raw:%d  (total failures: %lu)",
                     raw, ntc_fail_count);
        }

        taskENTER_CRITICAL(&s_mux);
        s_temp_update_requested = false;
        taskEXIT_CRITICAL(&s_mux);

        /* Stats système toutes les 10 itérations */
        if (cycle % 10 == 0) {
            UBaseType_t stack_wm = uxTaskGetStackHighWaterMark(NULL);
            size_t heap_free     = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
            size_t heap_min      = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);

            ESP_LOGW(TAG, "[SYS] === Stats cycle #%lu ===", cycle);
            ESP_LOGW(TAG, "[SYS]   Firmware version : " FW_VERSION_STR);
            ESP_LOGW(TAG, "[SYS]   Stack watermark  : %u words remaining", stack_wm);
            ESP_LOGW(TAG, "[SYS]   Heap free now    : %u bytes", heap_free);
            ESP_LOGW(TAG, "[SYS]   Heap min ever    : %u bytes", heap_min);
            ESP_LOGW(TAG, "[SYS]   ntc_read failures: %lu", ntc_fail_count);
            ESP_LOGW(TAG, "[SYS]   Zigbee failures  : %lu", zb_fail_count);
        }

        /* Attente avec sortie anticipée si update demandée */
        uint32_t waited_ms = 0;
        while (waited_ms < report_interval_ms) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            waited_ms += 1000;

            taskENTER_CRITICAL(&s_mux);
            bool update_requested = s_temp_update_requested;
            taskEXIT_CRITICAL(&s_mux);

            if (update_requested) {
                ESP_LOGI(TAG, "[TEMP] Early wake-up requested after %lums", waited_ms);
                break;
            }
        }

        /* Watchdog soft */
        uint32_t t_elapsed    = uptime_s() - t_start;
        uint32_t expected_max = (report_interval_ms / 1000) * 2;
        if (t_elapsed > expected_max) {
            ESP_LOGE(TAG, "[WDT] Cycle #%lu took %lus — expected max %lus! Task stalling?",
                     cycle, t_elapsed, expected_max);
        }
    }
}

/* ── app_main ─────────────────────────────────────────────── */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   Pool Controller  v" FW_VERSION_STR "            ║");
    ESP_LOGI(TAG, "║   STARKYDIY — ESP32-C6 Zigbee        ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");
    ESP_LOGI(TAG, "[BOOT] Free heap at boot : %u bytes",
             heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    xTaskCreate(esp_zb_task,      "esp_zb_task", 8192, NULL, 5, NULL);
    xTaskCreate(temp_report_task, "temp_task",   4096, NULL, 4, NULL);
}
