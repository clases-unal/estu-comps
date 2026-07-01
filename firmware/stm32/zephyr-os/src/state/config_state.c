#include "config_state.h"

/* TODO: confirmar valores por defecto reales contra el comportamiento esperado
 * de la maqueta física. Estos son placeholders razonables. */
#define DEFAULT_THRESHOLD_LOW    30.0f
#define DEFAULT_THRESHOLD_MEDIUM 45.0f
#define DEFAULT_THRESHOLD_HIGH   60.0f

static ConfigState state;
static struct k_mutex config_mutex;

void config_state_init(void)
{
	k_mutex_init(&config_mutex);
	state.threshold_low = DEFAULT_THRESHOLD_LOW;
	state.threshold_medium = DEFAULT_THRESHOLD_MEDIUM;
	state.threshold_high = DEFAULT_THRESHOLD_HIGH;
}

void config_state_get(ConfigState *out)
{
	k_mutex_lock(&config_mutex, K_FOREVER);
	*out = state;
	k_mutex_unlock(&config_mutex);
}

void config_state_set_thresholds(float low, float medium, float high)
{
	k_mutex_lock(&config_mutex, K_FOREVER);
	state.threshold_low = low;
	state.threshold_medium = medium;
	state.threshold_high = high;
	k_mutex_unlock(&config_mutex);
}
