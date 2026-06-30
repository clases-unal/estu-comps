/*
 * matrix_keypad.c
 *
 * TODO: implementar escaneo real de filas (salidas) / columnas (entradas con
 * pull-up/pull-down) vía GPIO de Zephyr. Pendiente de mapa de pines (ver
 * docs/02-firmware-architecture.md Sección 4).
 */

#include <zephyr/kernel.h>
#include "matrix_keypad.h"

bool matrix_keypad_scan(char *out_key)
{
	/* TODO: implementar escaneo real de filas/columnas */
	ARG_UNUSED(out_key);
	return false;
}
