/*
 * system_state.h — Coordinación global de alta prioridad (SystemState)
 * Protegido por sys_mutex. Este es el ÚLTIMO mutex en el orden de adquisición
 * (ver docs/02-firmware-architecture.md Sección 2) — si un hilo necesita sys_mutex
 * junto con otro, sys_mutex se adquiere al final.
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <zephyr/kernel.h>
#include <stdbool.h>

typedef struct {
	bool system_enabled;        /* false = Alarma Permanente, ver discussion.md 4.5 */
	bool shutdown_requested;    /* true = se solicitó apagado, guardar NVS antes de cortar */
} SystemState;

void system_state_init(void);
void system_state_get(SystemState *out);
void system_state_set_enabled(bool enabled);
void system_state_request_shutdown(void);

#endif /* SYSTEM_STATE_H */
