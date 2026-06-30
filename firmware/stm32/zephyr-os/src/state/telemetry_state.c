/*
 * telemetry_state.c
 *
 * NOTA IMPORTANTE: los campos system_boot_count, error_count[] y
 * ntc_consecutive_failures deben persistir entre reinicios (NVS), según
 * discussion.md Sección 8. Este stub los inicializa en RAM con valor 0/cargados
 * en el futuro desde NVS — la integración real con CONFIG_NVS queda pendiente
 * de implementar (ver TODO en telemetry_state_init).
 */

#include "telemetry_state.h"

static TelemetryState state;
static struct k_mutex telemetry_mutex;

void telemetry_state_init(void)
{
	k_mutex_init(&telemetry_mutex);
	state.error_log_flags = 0;
	state.system_boot_count = 0;   /* TODO: cargar desde NVS en vez de 0 */
	for (int i = 0; i < 4; i++) {
		state.error_count[i] = 0; /* TODO: cargar desde NVS */
	}
	state.ntc_consecutive_failures = 0; /* TODO: cargar desde NVS */
}

void telemetry_state_get(TelemetryState *out)
{
	k_mutex_lock(&telemetry_mutex, K_FOREVER);
	*out = state;
	k_mutex_unlock(&telemetry_mutex);
}

void telemetry_state_set_error_flag(uint32_t flag, bool active)
{
	k_mutex_lock(&telemetry_mutex, K_FOREVER);
	if (active) {
		state.error_log_flags |= flag;
	} else {
		state.error_log_flags &= ~flag;
	}
	k_mutex_unlock(&telemetry_mutex);
}

void telemetry_state_increment_boot_count(void)
{
	k_mutex_lock(&telemetry_mutex, K_FOREVER);
	state.system_boot_count++;
	/* TODO: persistir en NVS inmediatamente tras incrementar */
	k_mutex_unlock(&telemetry_mutex);
}