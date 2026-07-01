/*
 * cooling_manager.c — Hilo de decisión térmica y control de ventilador.
 *
 * Qué hace:
 * - Lee la temperatura actual desde ControlState.
 * - Compara esa temperatura contra los umbrales configurados en ConfigState.
 * - Decide el nivel térmico activo (FRÍO, BAJO, MEDIO, ALTO).
 * - Convierte ese nivel en un duty cycle y lo aplica al ventilador mediante PWM.
 * - En caso de fallo del sensor NTC, entra en failsafe y fuerza el ventilador a
 *   máxima velocidad.
 *
 * Cómo lo hace:
 * - compute_threshold() convierte temperatura + umbrales en un threshold_code_t.
 * - duty_for_threshold() transforma ese código en un porcentaje de trabajo.
 * - apply_duty_cycle() escribe el valor de PWM en el pin configurado en el overlay.
 *
 * Qué recibe / qué entrega:
 * - Recibe la temperatura medida y la configuración de umbrales desde los
 *   estados compartidos.
 * - Entrega el umbral activo y el duty cycle aplicable a ControlState para que
 *   otros módulos puedan reflejarlo en pantalla o en telemetría.
 * - Entrega la señal de PWM al ventilador real.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "cooling_manager.h"
#include "../state/control_state.h"
#include "../state/config_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(cooling_manager, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define THREAD_PRIORITY 2       /* Alta prioridad — ver docs/02-firmware-architecture.md */
#define PERIOD_MS 1000

/* Mapeo umbral -> duty cycle (%). TODO: ajustar estos valores contra el
 * comportamiento esperado real (discussion.md no fija porcentajes exactos). */
#define DUTY_COLD    0
#define DUTY_LOW    30
#define DUTY_MEDIUM 60
#define DUTY_HIGH  100

static const struct pwm_dt_spec fan_pwm = PWM_DT_SPEC_GET(DT_PATH(zephyr_user));

bool cooling_manager_init(void)
{
	if (!pwm_is_ready_dt(&fan_pwm)) {
		LOG_ERR("PWM del ventilador no listo — revisar nucleo_l476rg.overlay");
		return false;
	}
	return true;
}

static threshold_code_t compute_threshold(float temperature, const ConfigState *cfg)
{
	if (temperature >= cfg->threshold_high) {
		return THRESHOLD_HIGH;
	} else if (temperature >= cfg->threshold_medium) {
		return THRESHOLD_MEDIUM;
	} else if (temperature >= cfg->threshold_low) {
		return THRESHOLD_LOW;
	}
	return THRESHOLD_COLD;
}

static uint8_t duty_for_threshold(threshold_code_t code)
{
	switch (code) {
	case THRESHOLD_HIGH:   return DUTY_HIGH;
	case THRESHOLD_MEDIUM: return DUTY_MEDIUM;
	case THRESHOLD_LOW:    return DUTY_LOW;
	case THRESHOLD_COLD:
	default:               return DUTY_COLD;
	}
}

static void apply_duty_cycle(uint8_t duty_percent)
{
	uint32_t pulse_ns = (fan_pwm.period * duty_percent) / 100;

	int err = pwm_set_pulse_dt(&fan_pwm, pulse_ns);
	if (err != 0) {
		LOG_ERR("pwm_set_pulse_dt fallo: %d", err);
	}
}

static void cooling_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("cooling_manager thread started");

	bool pwm_ok = cooling_manager_init();
	if (!pwm_ok) {
		LOG_ERR("cooling_manager continuara sin control de PWM funcional");
	}

	while (1) {
		ControlState control;
		ConfigState config;
		TelemetryState telemetry;

		control_state_get(&control);
		config_state_get(&config);
		telemetry_state_get(&telemetry);

		bool ntc_failed = (telemetry.error_log_flags & ERROR_FLAG_NTC_SENSOR) != 0;

		uint8_t duty;
		threshold_code_t threshold;

		if (ntc_failed) {
			/* Failsafe: NTC no confiable -> ventilador a maxima velocidad */
			duty = DUTY_HIGH;
			threshold = THRESHOLD_HIGH;
			LOG_WRN("Failsafe activo (NTC en falla): ventilador forzado a %u%%",
				duty);
		} else {
			threshold = compute_threshold(control.current_temperature, &config);
			duty = duty_for_threshold(threshold);
		}

		control_state_set_threshold(threshold);
		control_state_set_fan_duty(duty);

		if (pwm_ok) {
			apply_duty_cycle(duty);
		}

		LOG_INF("T=%.2fC umbral=%d duty=%u%%", (double)control.current_temperature,
			threshold, duty);

		k_sleep(K_MSEC(PERIOD_MS));
	}
}

K_THREAD_DEFINE(cooling_manager_tid, STACK_SIZE, cooling_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
