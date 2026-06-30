/*
 * main.c — Sistema de Control Térmico de Alta Disponibilidad
 *
 * Responsabilidad de este archivo: ÚNICAMENTE inicializar las 5 estructuras de
 * estado global (y sus mutex) antes de que cualquier hilo intente usarlas.
 *
 * Los 7 hilos del sistema NO se crean aquí explícitamente: cada uno se registra
 * a sí mismo de forma estática con K_THREAD_DEFINE() en su propio archivo
 * (tasks/*.c) y arranca automáticamente en el boot de Zephyr. Esto significa que
 * main() termina su ejecución y los hilos siguen corriendo — es el comportamiento
 * normal y esperado, main() no necesita un while(1) propio.
 *
 * Orden de inicialización: los *_state_init() deben ejecutarse ANTES de que
 * cualquier hilo intente leer/escribir su estructura. Como K_THREAD_DEFINE crea
 * los hilos en boot (antes de main()), existe una condición de carrera teórica:
 * un hilo de alta prioridad podría ejecutar antes que main() llame a los _init().
 *
 * TODO (decisión pendiente de validar en implementación): evaluar si se requiere
 * usar K_THREAD_DEFINE con un delay de arranque (último parámetro) o cambiar a
 * k_thread_create() llamado explícitamente desde aquí, DESPUÉS de los _init(),
 * para eliminar esta condición de carrera de forma determinista.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "state/control_state.h"
#include "state/transmission_state.h"
#include "state/config_state.h"
#include "state/telemetry_state.h"
#include "state/system_state.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Sistema de Control Termico de Alta Disponibilidad — boot");

	control_state_init();
	transmission_state_init();
	config_state_init();
	telemetry_state_init();
	system_state_init();

	telemetry_state_increment_boot_count();

	LOG_INF("Estado global inicializado. Hilos de tasks/ deberian estar activos.");

	// Al final de tu main(), reemplaza el return 0; por esto:
    while (1) {
        k_sleep(K_MSEC(1000));
    }
}
