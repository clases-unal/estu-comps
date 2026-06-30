/*
 * config_state.h — Parámetros editables por el usuario vía HMI (ConfigState)
 * Protegido por config_mutex.
 */

#ifndef CONFIG_STATE_H
#define CONFIG_STATE_H

#include <zephyr/kernel.h>

typedef struct {
	float threshold_low;
	float threshold_medium;
	float threshold_high;
	/* TODO: modo de operación — definir enum cuando se cierre el diseño de UI/teclado */
} ConfigState;

void config_state_init(void);
void config_state_get(ConfigState *out);
void config_state_set_thresholds(float low, float medium, float high);

#endif /* CONFIG_STATE_H */