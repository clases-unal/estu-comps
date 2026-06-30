/*
 * temperature_manager.c — Lectura ADC, conversión, filtrado, publicación a ControlState
 *
 * Comportamiento:
 *  - Lee el NTC cada PERIOD_MS.
 *  - Aplica un promedio móvil simple de FILTER_WINDOW muestras para suavizar ruido.
 *  - Si la lectura falla MAX_CONSECUTIVE_FAILURES veces seguidas, marca
 *    ERROR_FLAG_NTC_SENSOR en TelemetryState (modo degradado — ver discussion.md
 *    Sección 3.1). No implementa todavía la lógica de qué hacer en modo degradado
 *    a nivel de sistema (eso depende de cooling_manager / power_status_manager,
 *    pendiente de implementar).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "temperature_manager.h"
#include "../drivers/ntc_sensor.h"
#include "../state/control_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(temperature_manager, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define THREAD_PRIORITY 5   /* TODO: ajustar según docs/02-firmware-architecture.md */

#define PERIOD_MS 500
#define FILTER_WINDOW 5
#define MAX_CONSECUTIVE_FAILURES 5

static float filter_buffer[FILTER_WINDOW];
static uint8_t filter_index;
static uint8_t filter_filled_count;

void temperature_manager_init(void)
{
	bool ok = ntc_sensor_init();
	if (!ok) {
		LOG_ERR("ntc_sensor_init fallo — temperature_manager funcionara en modo "
			"degradado desde el arranque");
	}

	for (int i = 0; i < FILTER_WINDOW; i++) {
		filter_buffer[i] = 0.0f;
	}
	filter_index = 0;
	filter_filled_count = 0;
}

static float apply_moving_average(float new_sample)
{
	filter_buffer[filter_index] = new_sample;
	filter_index = (filter_index + 1) % FILTER_WINDOW;
	if (filter_filled_count < FILTER_WINDOW) {
		filter_filled_count++;
	}

	float sum = 0.0f;
	for (int i = 0; i < filter_filled_count; i++) {
		sum += filter_buffer[i];
	}
	return sum / (float)filter_filled_count;
}

static void temperature_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("temperature_manager thread started");

	/*
	 * Inicialización propia del hilo (no se delega a main.c) para evitar la
	 * condición de carrera entre K_THREAD_DEFINE (arranca en boot) y main()
	 * (corre como otro hilo, orden relativo no garantizado). Ver TODO en main.c.
	 */
	temperature_manager_init();

	uint8_t consecutive_failures = 0;

	while (1) {
		float raw_temperature;
		bool ok = ntc_sensor_read_celsius(&raw_temperature);

		if (ok) {
			consecutive_failures = 0;
			telemetry_state_set_error_flag(ERROR_FLAG_NTC_SENSOR, false);

			float filtered = apply_moving_average(raw_temperature);
			control_state_set_temperature(filtered);

			LOG_INF("NTC: raw=%.2f filtered=%.2f", (double)raw_temperature,
				(double)filtered);
		} else {
			consecutive_failures++;
			LOG_WRN("Lectura NTC fallida (%u/%u consecutivas)",
				consecutive_failures, MAX_CONSECUTIVE_FAILURES);

			if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
				telemetry_state_set_error_flag(ERROR_FLAG_NTC_SENSOR, true);
				LOG_ERR("NTC declarado en falla tras %u lecturas fallidas "
					"consecutivas — TODO: definir comportamiento de "
					"modo degradado en cooling_manager",
					consecutive_failures);
			}
		}

		k_sleep(K_MSEC(PERIOD_MS));
	}
}

K_THREAD_DEFINE(temperature_manager_tid, STACK_SIZE, temperature_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);