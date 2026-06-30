/*
 * control_state.h — Estado de control térmico (ControlState)
 * Protegido por control_mutex. Ver docs/02-firmware-architecture.md Sección 2.
 *
 * Dominio: lo relacionado al lazo de control térmico en sí (temperatura medida,
 * salida PWM hacia el ventilador, umbral activo). NO incluye configuración editable
 * por el usuario (eso es ConfigState) ni banderas de diagnóstico (eso es TelemetryState).
 */

#ifndef CONTROL_STATE_H
#define CONTROL_STATE_H

#include <zephyr/kernel.h>
#include <stdint.h>

/* Códigos de umbral térmico. El orden importa: se usa para comparaciones >=. */
typedef enum {
	THRESHOLD_COLD = 0,
	THRESHOLD_LOW,
	THRESHOLD_MEDIUM,
	THRESHOLD_HIGH,
} threshold_code_t;

typedef struct {
	float current_temperature;       /* °C, ya filtrada (promedio móvil) */
	uint8_t fan_pwm_duty_cycle;       /* 0-100 */
	threshold_code_t current_threshold_code;
} ControlState;

/*
 * Inicializa la estructura y su mutex. Debe llamarse una única vez desde main()
 * antes de crear cualquier hilo que la use.
 */
void control_state_init(void);

/* Lectura segura: copia el estado actual en *out bajo el mutex. */
void control_state_get(ControlState *out);

/* Escritura segura de temperatura (llamada típicamente desde temperature_manager). */
void control_state_set_temperature(float temperature_celsius);

/* Escritura segura de duty cycle (llamada típicamente desde cooling_manager). */
void control_state_set_fan_duty(uint8_t duty_cycle);

/* Escritura segura de umbral activo. */
void control_state_set_threshold(threshold_code_t code);

#endif /* CONTROL_STATE_H */