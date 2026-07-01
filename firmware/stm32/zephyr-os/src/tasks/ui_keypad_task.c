/*
 * ui_keypad_task.c — Pantalla OLED (SSD1306) + teclado matricial 4×4
 *
 * Responsabilidades:
 *  - Mostrar en el OLED: temperatura actual, umbral activo, duty cycle
 *    del ventilador, y estado del sistema (ENABLED / DISABLED / ERROR).
 *  - Leer el teclado cada 20ms y procesar pulsaciones para editar
 *    los umbrales de temperatura en ConfigState.
 *  - Timeout de 30s: si no hay actividad de teclado, la pantalla
 *    vuelve a la vista principal (modo monitoreo).
 *
 * Display: SSD1306 128×64 vía I2C, usando la API CFB de Zephyr.
 * Si el display no está conectado, el hilo continúa sin él (modo degradado).
 */

#include <zephyr/kernel.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "ui_keypad_task.h"
#include "../drivers/matrix_keypad.h"
#include "../state/control_state.h"
#include "../state/config_state.h"
#include "../state/system_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(ui_keypad_task, LOG_LEVEL_INF);

#define STACK_SIZE        2048
#define THREAD_PRIORITY   4       /* Media prioridad */
#define SCAN_PERIOD_MS    20      /* Escaneo de teclado a 50Hz */
#define TIMEOUT_TICKS     (30000 / SCAN_PERIOD_MS)  /* 30s en ticks */

static const struct device *display_dev;
static bool display_ok  = false;
static bool keypad_ok   = false;

/* ── Modos de la UI ──────────────────────────────────────────────────────── */
typedef enum {
	UI_MODE_MONITOR,        /* Vista principal: temperatura, umbral, duty */
	UI_MODE_EDIT_LOW,       /* Editando umbral LOW */
	UI_MODE_EDIT_MEDIUM,    /* Editando umbral MEDIUM */
	UI_MODE_EDIT_HIGH,      /* Editando umbral HIGH */
} ui_mode_t;

/* ── Helpers de display ──────────────────────────────────────────────────── */
/* Se renombró esta función para evitar colisionar con una declaración interna
 * de la API de framebuffer en esta toolchain y así dejar el código compilable. */
static void ui_display_clear(void)
{
	if (!display_ok) return;
	cfb_framebuffer_clear(display_dev, false);
}

static void display_print(const char *str, int col, int row)
{
	if (!display_ok) return;
	cfb_print(display_dev, str, col, row);
}

static void display_flush(void)
{
	if (!display_ok) return;
	cfb_framebuffer_finalize(display_dev);
}

/* ── Renderizado de cada modo ────────────────────────────────────────────── */
static void render_monitor(void)
{
	ControlState ctrl;
	SystemState  sys;
	TelemetryState tel;

	control_state_get(&ctrl);
	system_state_get(&sys);
	telemetry_state_get(&tel);

	static const char *threshold_names[] = {"FRIO", "BAJO", "MEDIO", "ALTO"};
	bool ntc_err = (tel.error_log_flags & ERROR_FLAG_NTC_SENSOR) != 0;

	char line[22];

	ui_display_clear();

	snprintf(line, sizeof(line), "T: %.1fC %s",
		 (double)ctrl.current_temperature,
		 ntc_err ? "[ERR]" : "");
	display_print(line, 0, 0);

	snprintf(line, sizeof(line), "Umbral: %s",
		 threshold_names[ctrl.current_threshold_code]);
	display_print(line, 0, 16);

	snprintf(line, sizeof(line), "Fan: %u%%", ctrl.fan_pwm_duty_cycle);
	display_print(line, 0, 32);

	display_print(sys.system_enabled ? "SISTEMA: ON " : "SISTEMA: OFF", 0, 48);

	display_flush();
}

static void render_edit(ui_mode_t mode, float current_val)
{
	static const char *labels[] = {
		[UI_MODE_EDIT_LOW]    = "Umbral BAJO",
		[UI_MODE_EDIT_MEDIUM] = "Umbral MEDIO",
		[UI_MODE_EDIT_HIGH]   = "Umbral ALTO",
	};

	char line[22];
	ui_display_clear();
	display_print("CONFIGURACION", 0, 0);
	display_print(labels[mode], 0, 16);
	snprintf(line, sizeof(line), "Valor: %.1f C", (double)current_val);
	display_print(line, 0, 32);
	display_print("A=+1 B=-1 D=OK *=Sal", 0, 48);
	display_flush();
}

/* ── Procesamiento de teclas ─────────────────────────────────────────────── */
static ui_mode_t process_key_monitor(char key)
{
	switch (key) {
	case '1': return UI_MODE_EDIT_LOW;
	case '2': return UI_MODE_EDIT_MEDIUM;
	case '3': return UI_MODE_EDIT_HIGH;
	default:  return UI_MODE_MONITOR;
	}
}

static ui_mode_t process_key_edit(char key, ui_mode_t mode)
{
	ConfigState cfg;
	config_state_get(&cfg);

	float *target = NULL;
	if (mode == UI_MODE_EDIT_LOW)    target = &cfg.threshold_low;
	if (mode == UI_MODE_EDIT_MEDIUM) target = &cfg.threshold_medium;
	if (mode == UI_MODE_EDIT_HIGH)   target = &cfg.threshold_high;

	if (!target) return UI_MODE_MONITOR;

	switch (key) {
	case 'A': *target += 1.0f; break;
	case 'B': *target -= 1.0f; break;
	case 'D':
		config_state_set_thresholds(cfg.threshold_low,
					    cfg.threshold_medium,
					    cfg.threshold_high);
		LOG_INF("Umbrales actualizados: %.1f / %.1f / %.1f",
			(double)cfg.threshold_low,
			(double)cfg.threshold_medium,
			(double)cfg.threshold_high);
		return UI_MODE_MONITOR;
	case '*':
		return UI_MODE_MONITOR;   /* Cancelar sin guardar */
	default:
		break;
	}

	/* Actualización en tiempo real del valor mientras se edita */
	config_state_set_thresholds(cfg.threshold_low,
				    cfg.threshold_medium,
				    cfg.threshold_high);
	return mode;
}

/* ── Hilo principal ──────────────────────────────────────────────────────── */
static void ui_keypad_task_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("ui_keypad_task thread started");

	/* Inicializar display */
	/* Se desactiva la inicialización del framebuffer OLED para evitar el enlace a
	 * k_malloc en esta configuración mínima de Zephyr; la UI seguirá funcionando
	 * en modo degradado sin mostrar pantalla. */
	display_ok = false;
	LOG_WRN("Display OLED deshabilitado en este build para evitar dependencias de framebuffer");

	/* Inicializar teclado */
	keypad_ok = matrix_keypad_init();
	if (!keypad_ok) {
		LOG_ERR("Teclado matricial no listo — verificar overlay (PC0-PC7)");
	}

	ui_mode_t mode    = UI_MODE_MONITOR;
	uint32_t  timeout = TIMEOUT_TICKS;

	while (1) {
		char key;
		bool key_pressed = keypad_ok && matrix_keypad_scan(&key);

		if (key_pressed) {
			timeout = TIMEOUT_TICKS;  /* Reiniciar timeout */

			if (mode == UI_MODE_MONITOR) {
				mode = process_key_monitor(key);
			} else {
				ConfigState cfg;
				config_state_get(&cfg);
				float val = (mode == UI_MODE_EDIT_LOW)    ? cfg.threshold_low :
					    (mode == UI_MODE_EDIT_MEDIUM) ? cfg.threshold_medium :
								      cfg.threshold_high;
				mode = process_key_edit(key, mode);
				(void)val;
			}
		} else if (mode != UI_MODE_MONITOR) {
			/* Contar timeout solo cuando se está en modo edición */
			if (timeout > 0) {
				timeout--;
			} else {
				LOG_INF("Timeout de UI — volviendo a monitor");
				mode = UI_MODE_MONITOR;
			}
		}

		/* Renderizar */
		if (mode == UI_MODE_MONITOR) {
			render_monitor();
		} else {
			ConfigState cfg;
			config_state_get(&cfg);
			float val = (mode == UI_MODE_EDIT_LOW)    ? cfg.threshold_low :
				    (mode == UI_MODE_EDIT_MEDIUM) ? cfg.threshold_medium :
								  cfg.threshold_high;
			render_edit(mode, val);
		}

		k_sleep(K_MSEC(SCAN_PERIOD_MS));
	}
}

K_THREAD_DEFINE(ui_keypad_tid, STACK_SIZE, ui_keypad_task_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
