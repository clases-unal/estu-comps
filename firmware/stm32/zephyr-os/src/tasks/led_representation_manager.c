/*
 * led_representation_manager.c — Hilo visualizador del estado del sistema.
 *
 * Qué hace:
 * - Representa el nivel térmico actual mediante una barra de LEDs.
 * - Indica si el sistema está habilitado o no mediante parpadeo global.
 * - Destaca un error del sensor NTC encendiendo/parpadeando un LED especial.
 *
 * Cómo lo hace:
 * - Lee ControlState para saber el umbral activo.
 * - Usa un mapeo fijo entre umbral y cantidad de LEDs encendidos.
 * - Mantiene un tick interno para generar el parpadeo de los LEDs cuando el
 *   sistema está deshabilitado o hay un error del NTC.
 *
 * Qué recibe / qué entrega:
 * - Recibe el estado del sistema, la temperatura/umbral actual y flags de error.
 * - Entrega una salida visual a través de GPIOs conectados a LEDs.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "led_representation_manager.h"
#include "../state/control_state.h"
#include "../state/system_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(led_representation_manager, LOG_LEVEL_WRN);

#define STACK_SIZE      1024
#define THREAD_PRIORITY 4       /* Media prioridad */
#define PERIOD_MS       100     /* Actualización a 10Hz — suficiente para parpadeo 2Hz */

static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led1), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led2), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led3), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led4), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led5), gpios),
};

#define NUM_LEDS ARRAY_SIZE(leds)

/* Cuántos LEDs encender por umbral */
static const uint8_t leds_for_threshold[] = {0, 2, 4, 6};

static bool led_init_ok = false;

static void leds_init(void)
{
	for (int i = 0; i < NUM_LEDS; i++) {
		if (!gpio_is_ready_dt(&leds[i])) {
			LOG_ERR("LED %d no listo", i);
			return;
		}
		gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
	}
	led_init_ok = true;
}

static void set_led(int idx, bool on)
{
	if (idx < NUM_LEDS) {
		gpio_pin_set_dt(&leds[idx], on ? 1 : 0);
	}
}

static void all_leds(bool on)
{
	for (int i = 0; i < NUM_LEDS; i++) {
		set_led(i, on);
	}
}

static void led_representation_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("led_representation_manager thread started");
	leds_init();

	uint32_t tick = 0;

	while (1) {
		if (!led_init_ok) {
			k_sleep(K_MSEC(PERIOD_MS));
			continue;
		}

		SystemState sys;
		ControlState ctrl;
		TelemetryState tel;

		system_state_get(&sys);
		control_state_get(&ctrl);
		telemetry_state_get(&tel);

		bool ntc_error = (tel.error_log_flags & ERROR_FLAG_NTC_SENSOR) != 0;

		if (!sys.system_enabled) {
			/* Sistema deshabilitado: parpadeo global a 2Hz
			 * PERIOD_MS=100ms → 5 ticks por semi-ciclo */
			bool blink_on = (tick % 10) < 5;
			all_leds(blink_on);
		} else {
			/* Sistema habilitado: barra según umbral */
			uint8_t leds_on = leds_for_threshold[ctrl.current_threshold_code];
			for (int i = 0; i < NUM_LEDS; i++) {
				set_led(i, i < leds_on);
			}

			/* LED5 parpadea si NTC en falla, independientemente del umbral */
			if (ntc_error) {
				bool blink_on = (tick % 10) < 5;
				set_led(5, blink_on);
			}
		}

		tick++;
		k_sleep(K_MSEC(PERIOD_MS));
	}
}

K_THREAD_DEFINE(led_manager_tid, STACK_SIZE, led_representation_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
