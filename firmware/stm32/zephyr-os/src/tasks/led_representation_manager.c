/*
 * led_representation_manager.c — LED Manager: 6 GPIOs directos
 *
 * TODO: implementar la lógica real de este hilo. Ver docs/03-state-machines.md
 * para el comportamiento esperado y docs/02-firmware-architecture.md Sección 1
 * para la responsabilidad y prioridad de este hilo.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "led_representation_manager.h"

LOG_MODULE_REGISTER(led_representation_manager, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define THREAD_PRIORITY 5

void led_representation_manager_init(void)
{
	/* TODO: inicialización de periféricos/recursos previos al arranque del hilo */
}

static void led_representation_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("led_representation_manager thread started");

	while (1) {
		/* TODO: implementar lógica del hilo */
		k_sleep(K_MSEC(1000));
	}
}

K_THREAD_DEFINE(led_representation_manager_tid, STACK_SIZE, led_representation_manager_thread, NULL, NULL, NULL,
		 THREAD_PRIORITY, 0, 0);
