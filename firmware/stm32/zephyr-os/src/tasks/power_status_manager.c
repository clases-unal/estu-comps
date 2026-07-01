/*
 * power_status_manager.c — Pulsador físico (ISR + debounce), gestión de SHUTDOWN
 *
 * Pin: PC13 (botón azul de usuario de la Nucleo-L476RG).
 * Si tu diseño usa otro pin, cambia la declaración en el overlay y el
 * alias "sw0" o el nodo gpio_keys en el overlay.
 *
 * Comportamiento:
 *  - Detecta flanco de bajada (botón presionado) vía ISR de GPIO.
 *  - Aplica debounce por software: ignora flancos adicionales durante
 *    DEBOUNCE_MS tras el primero.
 *  - Pulsación corta (< LONG_PRESS_MS): toggle system_enabled.
 *  - Pulsación larga (>= LONG_PRESS_MS): solicita SHUTDOWN ordenado.
 *
 * Comunicación con el resto del sistema: solo escribe SystemState.
 * Otros módulos leen SystemState para reaccionar al shutdown.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "power_status_manager.h"
#include "../state/system_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(power_status_manager, LOG_LEVEL_INF);

#define STACK_SIZE      1024
#define THREAD_PRIORITY 2       /* Alta prioridad — ver docs/02-firmware-architecture.md */

#define DEBOUNCE_MS    50
#define LONG_PRESS_MS 3000

/* Se evita el uso del nodo DT del botón porque en esta toolchain provoca un
 * enlace incompleto. Para que el sistema compile, se resuelve el GPIO vía
 * dispositivo GPIOC de forma explícita y se conserva la lógica del debounce. */
static const struct device *button_port;
static struct gpio_callback button_cb;
static uint32_t button_pin = 13U;

/* Semáforo que la ISR señala cuando detecta un flanco válido */
static K_SEM_DEFINE(button_sem, 0, 1);

/* Timestamp del flanco de bajada, para medir duración de pulsación */
static int64_t press_timestamp_ms;

static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* Guardar timestamp y señalar al hilo — no hacer lógica pesada en ISR */
	press_timestamp_ms = k_uptime_get();
	k_sem_give(&button_sem);
}

bool power_status_manager_init(void)
{
	button_port = device_get_binding("GPIOC");
	if (!button_port || !device_is_ready(button_port)) {
		LOG_ERR("GPIO del pulsador no listo — dispositivo GPIOC no disponible");
		return false;
	}

	int err = gpio_pin_configure(button_port, button_pin, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("gpio_pin_configure fallo: %d", err);
		return false;
	}

	err = gpio_pin_interrupt_configure(button_port, button_pin, GPIO_INT_EDGE_TO_ACTIVE);
	if (err != 0) {
		LOG_ERR("gpio_pin_interrupt_configure fallo: %d", err);
		return false;
	}

	gpio_init_callback(&button_cb, button_isr, BIT(button_pin));
	gpio_add_callback(button_port, &button_cb);

	LOG_INF("Pulsador listo en GPIOC pin %u", button_pin);
	return true;
}

static void power_status_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("power_status_manager thread started");

	bool ok = power_status_manager_init();
	if (!ok) {
		LOG_ERR("power_status_manager: init fallo, hilo desactivado");
		return;
	}

	while (1) {
		/* Esperar señal de la ISR — este hilo no consume CPU mientras espera */
		k_sem_take(&button_sem, K_FOREVER);

		/* Debounce: esperar y descartar flancos espurios */
		k_msleep(DEBOUNCE_MS);
		k_sem_reset(&button_sem);

		/* Medir cuánto tiempo estuvo presionado */
		int64_t now_ms = k_uptime_get();
		int64_t duration_ms = now_ms - press_timestamp_ms;

		if (duration_ms >= LONG_PRESS_MS) {
			LOG_WRN("Pulsacion larga detectada (%lld ms) — solicitando SHUTDOWN",
				duration_ms);
			system_state_request_shutdown();
			telemetry_state_increment_boot_count();
		} else {
			SystemState sys;
			system_state_get(&sys);
			bool new_state = !sys.system_enabled;
			system_state_set_enabled(new_state);
			LOG_INF("Pulsacion corta (%lld ms) — system_enabled=%d",
				duration_ms, new_state);
		}
	}
}

K_THREAD_DEFINE(power_status_manager_tid, STACK_SIZE, power_status_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
