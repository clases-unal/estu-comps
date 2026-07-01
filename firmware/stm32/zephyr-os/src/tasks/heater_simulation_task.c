/*
 * heater_simulation_task.c — Hilo que simula una fuente de calor externa.
 *
 * Qué hace:
 * - Activa y desactiva un pin de GPIO con un patrón periódico para simular una
 *   resistencia de calentamiento.
 * - Solo emite calor cuando system_enabled está activo.
 *
 * Cómo lo hace:
 * - Usa un pulso periódico con tiempo HIGH y tiempo LOW fijos.
 * - El hilo consulta SystemState para verificar si el sistema está habilitado.
 * - Si está deshabilitado, mantiene el GPIO en estado inactivo.
 *
 * Qué recibe / qué entrega:
 * - Recibe el estado de habilitación del sistema desde SystemState.
 * - Entrega una señal de calor simulada al entorno físico (GPIO) y, vía la
 *   temperatura del NTC, afecta al comportamiento del ventilador.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "heater_simulation_task.h"
#include "../state/system_state.h"

LOG_MODULE_REGISTER(heater_simulation_task, LOG_LEVEL_WRN);

#define STACK_SIZE       1024
#define THREAD_PRIORITY  6       /* Baja prioridad */

#define PULSE_PERIOD_MS  500     /* Período del pulso de keep-alive */
#define PULSE_ON_MS      200     /* Duración del pulso HIGH (duty ~40%) */

static const struct gpio_dt_spec heater =
	GPIO_DT_SPEC_GET(DT_NODELABEL(heater_pin), gpios);

void heater_simulation_task_init(void)
{
	if (!gpio_is_ready_dt(&heater)) {
		LOG_ERR("Pin de heater no listo — verificar overlay (PA8)");
		return;
	}
	gpio_pin_configure_dt(&heater, GPIO_OUTPUT_INACTIVE);
}

static void heater_simulation_task_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("heater_simulation_task thread started");
	heater_simulation_task_init();

	while (1) {
		SystemState sys;
		system_state_get(&sys);

		if (sys.system_enabled) {
			gpio_pin_set_dt(&heater, 1);
			k_sleep(K_MSEC(PULSE_ON_MS));
			gpio_pin_set_dt(&heater, 0);
			k_sleep(K_MSEC(PULSE_PERIOD_MS - PULSE_ON_MS));
		} else {
			gpio_pin_set_dt(&heater, 0);
			k_sleep(K_MSEC(PULSE_PERIOD_MS));
		}
	}
}

K_THREAD_DEFINE(heater_sim_tid, STACK_SIZE, heater_simulation_task_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
