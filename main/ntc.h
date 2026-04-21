#pragma once
#include <stdbool.h>
#include <stdint.h>

/*
 * NTC 035HP05202 – 10kΩ @ 25°C
 *
 * Beta parameter: typical value 3950 K for a 10kΩ NTC.
 * Refine this value using your probe's datasheet or a field calibration
 * (ice water 0°C / boiling water 100°C).
 *
 * Voltage divider (stable 3.3V supply):
 *   3.3V → R_REF (10kΩ 1%) → GPIO1 (ADC) → NTC → GND
 */
#define NTC_ADC_CHANNEL     ADC_CHANNEL_0   // GPIO1 on ESP32-C6
#define NTC_R_REF           10000.0f        // reference resistor (Ω)
#define NTC_R_NOMINAL       10000.0f        // NTC resistance @ T_NOMINAL
#define NTC_T_NOMINAL       25.0f           // °C
#define NTC_BETA            3950.0f         // K — verify against datasheet
#define NTC_VCC             3.3f
#define NTC_ADC_MAX         4095.0f         // 12-bit resolution

bool ntc_init(void);

/**
 * Read temperature in hundredths of degrees Celsius.
 * e.g. 2547 = 25.47°C  /  -312 = -3.12°C
 * Returns false if the reading is outside the physical range.
 */
bool ntc_read(int16_t *temp_hundredths);
