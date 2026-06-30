/*
 * heater_simulation_task.c — Heater Simulation: keep-alive del proceso externo
 *
 * TODO: implementar la lógica real de este hilo. Ver docs/03-state-machines.md
 * para el comportamiento esperado y docs/02-firmware-architecture.md Sección 1
 * para la responsabilidad y prioridad de este hilo.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "heater_simulation_task.h"

LOG_MODULE_REGISTER(heater_simulation_task, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define THREAD_PRIORITY 5

void heater_simulation_task_init(void)
{
	/* TODO: inicialización de periféricos/recursos previos al arranque del hilo */
}

static void heater_simulation_task_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("heater_simulation_task thread started");

	while (1) {
		/* TODO: implementar lógica del hilo */
		k_sleep(K_MSEC(1000));
	}
}

K_THREAD_DEFINE(heater_simulation_task_tid, STACK_SIZE, heater_simulation_task_thread, NULL, NULL, NULL,
		 THREAD_PRIORITY, 0, 0);
