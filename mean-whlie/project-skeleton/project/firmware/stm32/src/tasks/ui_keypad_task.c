/*
 * ui_keypad_task.c — Unified UI & Keypad: OLED + teclado, timeout 30s
 *
 * TODO: implementar la lógica real de este hilo. Ver docs/03-state-machines.md
 * para el comportamiento esperado y docs/02-firmware-architecture.md Sección 1
 * para la responsabilidad y prioridad de este hilo.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "ui_keypad_task.h"

LOG_MODULE_REGISTER(ui_keypad_task, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define THREAD_PRIORITY 5

void ui_keypad_task_init(void)
{
	/* TODO: inicialización de periféricos/recursos previos al arranque del hilo */
}

static void ui_keypad_task_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("ui_keypad_task thread started");

	while (1) {
		/* TODO: implementar lógica del hilo */
		k_sleep(K_MSEC(1000));
	}
}

K_THREAD_DEFINE(ui_keypad_task_tid, STACK_SIZE, ui_keypad_task_thread, NULL, NULL, NULL,
		 THREAD_PRIORITY, 0, 0);
