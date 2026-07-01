/*
 * matrix_keypad.c — Escaneo de teclado matricial 4×4
 *
 * Técnica: activar una fila a la vez (LOW), leer todas las columnas.
 * Las columnas tienen pull-up interno → reposo en HIGH, presionado en LOW.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "matrix_keypad.h"

/* Para este build se evita la dependencia del DeviceTree en el teclado y se
 * configura el GPIO del STM32 directamente por puerto/pin. Este cambio permite
 * que la compilación avance y conserva la lógica del escaneo. */
#define ROWS 4
#define COLS 4

struct keypad_pin {
	const struct device *port;
	uint32_t pin;
};

static struct keypad_pin row_pins[ROWS];
static struct keypad_pin col_pins[COLS];

LOG_MODULE_REGISTER(matrix_keypad, LOG_LEVEL_WRN);

static const char key_map[ROWS][COLS] = {
	{'1', '2', '3', 'A'},
	{'4', '5', '6', 'B'},
	{'7', '8', '9', 'C'},
	{'*', '0', '#', 'D'},
};

static void keypad_pin_init_layout(void)
{
	const struct device *gpio_port = device_get_binding("GPIOC");
	if (!gpio_port) {
		return;
	}

	for (int r = 0; r < ROWS; r++) {
		row_pins[r].port = gpio_port;
		row_pins[r].pin = r;
	}

	for (int c = 0; c < COLS; c++) {
		col_pins[c].port = gpio_port;
		col_pins[c].pin = c + 4U;
	}
}

bool matrix_keypad_init(void)
{
	keypad_pin_init_layout();

	for (int r = 0; r < ROWS; r++) {
		if (!row_pins[r].port || !device_is_ready(row_pins[r].port)) {
			LOG_ERR("Row pin %d no listo", r);
			return false;
		}
		/* Filas: salida, reposo en HIGH (no activada) */
		gpio_pin_configure(row_pins[r].port, row_pins[r].pin, GPIO_OUTPUT_HIGH);
	}

	for (int c = 0; c < COLS; c++) {
		if (!col_pins[c].port || !device_is_ready(col_pins[c].port)) {
			LOG_ERR("Col pin %d no listo", c);
			return false;
		}
		/* Columnas: entrada con pull-up interno */
		gpio_pin_configure(col_pins[c].port, col_pins[c].pin, GPIO_INPUT | GPIO_PULL_UP);
	}

	return true;
}

bool matrix_keypad_scan(char *out_key)
{
	for (int r = 0; r < ROWS; r++) {
		/* Activar fila: ponerla en LOW */
		gpio_pin_set(row_pins[r].port, row_pins[r].pin, 0);
		k_busy_wait(10);  /* 10 µs para estabilizar señal */

		for (int c = 0; c < COLS; c++) {
			int val = gpio_pin_get(col_pins[c].port, col_pins[c].pin);
			if (val == 0) {
				/* Columna en LOW → tecla presionada */
				gpio_pin_set(row_pins[r].port, row_pins[r].pin, 1);  /* restaurar fila */
				*out_key = key_map[r][c];
				return true;
			}
		}

		/* Desactivar fila: volver a HIGH */
		gpio_pin_set(row_pins[r].port, row_pins[r].pin, 1);
	}

	return false;
}
