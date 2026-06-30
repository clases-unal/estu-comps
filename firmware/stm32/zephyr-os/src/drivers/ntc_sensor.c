/*
 * ntc_sensor.c
 *
 * Conversión usada: ecuación Beta (más simple que Steinhart-Hart de 3 constantes;
 * suficiente para el rango de operación de este proyecto, ver discussion.md).
 *   1/T = 1/T0 + (1/B) * ln(R/R0)
 * T0 = 298.15 K (25°C), R0 = 10000 Ω, B = 3470 K
 *
 * SUPUESTO A VERIFICAR FÍSICAMENTE (ver overlay): divisor con NTC entre VDD y el
 * nodo ADC, resistencia fija entre el nodo ADC y GND:
 *
 *     VDD(3.3V) ---[NTC]---o(PA0/ADC)---[R_fija 10k]--- GND
 *
 * Con esa topología: V_adc sube cuando el NTC se calienta (su resistencia baja).
 * Formula:  R_ntc = R_fija * (VDD / V_adc - 1)
 *
 * Si tu cableado real tiene el NTC abajo (junto a GND) y la resistencia fija
 * arriba (junto a VDD), la fórmula correcta es la inversa:
 *     R_ntc = R_fija * (V_adc / (VDD - V_adc))
 * En ese caso, comenta la línea marcada "TOPOLOGÍA A" y descomenta "TOPOLOGÍA B"
 * más abajo.
 */

#include <math.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include "ntc_sensor.h"

LOG_MODULE_REGISTER(ntc_sensor, LOG_LEVEL_INF);

/* Referencia al canal ADC declarado en el devicetree overlay (nodo zephyr,user) */
static const struct adc_dt_spec adc_channel =
	ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static int16_t sample_buffer;
static struct adc_sequence sequence = {
	.buffer = &sample_buffer,
	.buffer_size = sizeof(sample_buffer),
};

#define NTC_R0_OHMS     10000.0f
#define NTC_R_FIXED_OHMS 10000.0f
#define NTC_B_COEFF     3470.0f
#define NTC_T0_KELVIN   298.15f
#define VDD_VOLTS       3.3f

/* Rango físico válido — fuera de esto se asume sensor en falla (corto/abierto).
 * TODO: ajustar estos límites tras observar lecturas reales con el sensor sano. */
#define VALID_TEMP_MIN_C  -10.0f
#define VALID_TEMP_MAX_C  120.0f

bool ntc_sensor_init(void)
{
	if (!adc_is_ready_dt(&adc_channel)) {
		LOG_ERR("ADC channel not ready — revisar overlay nucleo_l476rg.overlay");
		return false;
	}

	int err = adc_channel_setup_dt(&adc_channel);
	if (err != 0) {
		LOG_ERR("adc_channel_setup_dt failed: %d", err);
		return false;
	}

	/*
	 * adc_sequence_init_dt() es la forma recomendada por Zephyr de poblar
	 * adc_sequence a partir de un adc_dt_spec — configura .channels,
	 * .resolution y .oversampling automáticamente, evitando errores manuales
	 * como dejar .channels en 0 (causa de "No channels selected").
	 */
	err = adc_sequence_init_dt(&adc_channel, &sequence);
	if (err != 0) {
		LOG_ERR("adc_sequence_init_dt failed: %d", err);
		return false;
	}

	LOG_INF("ADC listo: channel_id=%d resolution=%d sequence.channels=0x%x",
		adc_channel.channel_id, adc_channel.resolution, sequence.channels);

	return true;
}

bool ntc_sensor_read_celsius(float *out_temperature)
{
	if (sequence.channels == 0) {
		LOG_ERR("ntc_sensor_read_celsius llamado sin ntc_sensor_init() exitoso previo");
		return false;
	}

	int err = adc_read_dt(&adc_channel, &sequence);
	if (err != 0) {
		LOG_ERR("adc_read_dt failed: %d", err);
		return false;
	}

	int32_t raw = sample_buffer;
	int32_t mv = raw;

	err = adc_raw_to_millivolts_dt(&adc_channel, &mv);
	if (err != 0) {
		LOG_ERR("adc_raw_to_millivolts_dt failed: %d", err);
		return false;
	}

	float v_adc = mv / 1000.0f;

	/* Evitar división por cero / valores absurdos si el ADC satura */
	if (v_adc <= 0.01f || v_adc >= (VDD_VOLTS - 0.01f)) {
		LOG_WRN("Lectura ADC fuera de rango fisico (%.3f V) — posible falla NTC", v_adc);
		return false;
	}

	/* TOPOLOGÍA A (NTC arriba, junto a VDD) — supuesto por defecto */
	float r_ntc = NTC_R_FIXED_OHMS * (VDD_VOLTS / v_adc - 1.0f);

	/* TOPOLOGÍA B (NTC abajo, junto a GND) — descomentar si tu cableado es así
	 * y comentar la línea de arriba:
	 * float r_ntc = NTC_R_FIXED_OHMS * (v_adc / (VDD_VOLTS - v_adc));
	 */

	if (r_ntc <= 0.0f) {
		LOG_WRN("Resistencia NTC calculada invalida (%.1f ohm)", (double)r_ntc);
		return false;
	}

	/* Ecuación Beta */
	float inv_t = (1.0f / NTC_T0_KELVIN) +
		      (1.0f / NTC_B_COEFF) * logf(r_ntc / NTC_R0_OHMS);
	float temperature_kelvin = 1.0f / inv_t;
	float temperature_celsius = temperature_kelvin - 273.15f;

	if (temperature_celsius < VALID_TEMP_MIN_C || temperature_celsius > VALID_TEMP_MAX_C) {
		LOG_WRN("Temperatura fuera de rango valido: %.1f C", (double)temperature_celsius);
		return false;
	}

	*out_temperature = temperature_celsius;
	return true;
}