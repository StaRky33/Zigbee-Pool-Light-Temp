#include "ntc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "NTC";

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t         s_cali_handle;
static bool                      s_cali_ok = false;

bool ntc_init(void) {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,   // range 0–3.1V
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        s_adc_handle, NTC_ADC_CHANNEL, &chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = NTC_ADC_CHANNEL,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(
        &cali_cfg, &s_cali_handle);
    s_cali_ok = (ret == ESP_OK);
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "ADC calibration unavailable, using raw mode");
    }
    return true;
}

bool ntc_read(int16_t *temp_hundredths) {
    /* Average 8 samples to reduce ADC noise */
    int32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(
            s_adc_handle, NTC_ADC_CHANNEL, &raw));
        sum += raw;
    }
    int raw_avg = (int)(sum / 8);

    float voltage_mv;
    if (s_cali_ok) {
        int mv;
        adc_cali_raw_to_voltage(s_cali_handle, raw_avg, &mv);
        voltage_mv = (float)mv;
    } else {
        voltage_mv = (raw_avg / NTC_ADC_MAX) * (NTC_VCC * 1000.0f);
    }

    float vcc_mv = NTC_VCC * 1000.0f;
    if (voltage_mv <= 0.0f || voltage_mv >= vcc_mv) {
        ESP_LOGW(TAG, "Voltage out of range: %.1f mV", voltage_mv);
        return false;
    }

    /*
     * Voltage divider: Vout = VCC * R_NTC / (R_REF + R_NTC)
     * → R_NTC = R_REF * Vout / (VCC - Vout)
     */
    float r_ntc = NTC_R_REF * voltage_mv / (vcc_mv - voltage_mv);

    /*
     * Beta form of the Steinhart-Hart equation:
     * 1/T = 1/T0 + (1/Beta) * ln(R/R0)   (T in Kelvin)
     */
    float t0_k   = NTC_T_NOMINAL + 273.15f;
    float ratio  = r_ntc / NTC_R_NOMINAL;
    float temp_k = 1.0f / (1.0f / t0_k + (1.0f / NTC_BETA) * logf(ratio));
    float temp_c = temp_k - 273.15f;

    if (temp_c < -40.0f || temp_c > 125.0f) {
        ESP_LOGW(TAG, "Temperature outside physical range: %.2f°C", temp_c);
        return false;
    }

    *temp_hundredths = (int16_t)(temp_c * 100.0f);
    ESP_LOGD(TAG, "R_NTC=%.0f Ω → %.2f°C", r_ntc, temp_c);
    return true;
}
