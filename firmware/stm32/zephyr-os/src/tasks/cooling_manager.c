/*
 * cooling_manager.c — Cooling Manager: PWM del ventilador, failsafe
 *
 * TODO: implementar la lógica real de este hilo. Ver docs/03-state-machines.md
 * para el comportamiento esperado y docs/02-firmware-architecture.md Sección 1
 * para la responsabilidad y prioridad de este hilo.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "cooling_manager.h"

LOG_MODULE_REGISTER(cooling_manager, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define THREAD_PRIORITY 5

void cooling_manager_init(void)
{
	/* TODO: inicialización de periféricos/recursos previos al arranque del hilo */
}

static void cooling_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("cooling_manager thread started");

	while (1) {
		/* TODO: implementar lógica del hilo */
		k_sleep(K_MSEC(1000));
	}
}

K_THREAD_DEFINE(cooling_manager_tid, STACK_SIZE, cooling_manager_thread, NULL, NULL, NULL,
		 THREAD_PRIORITY, 0, 0);
