#include "system_state.h"

static SystemState state;
static struct k_mutex sys_mutex;

void system_state_init(void)
{
	k_mutex_init(&sys_mutex);
	state.system_enabled = true;
	state.shutdown_requested = false;
}

void system_state_get(SystemState *out)
{
	k_mutex_lock(&sys_mutex, K_FOREVER);
	*out = state;
	k_mutex_unlock(&sys_mutex);
}

void system_state_set_enabled(bool enabled)
{
	k_mutex_lock(&sys_mutex, K_FOREVER);
	state.system_enabled = enabled;
	k_mutex_unlock(&sys_mutex);
}

void system_state_request_shutdown(void)
{
	k_mutex_lock(&sys_mutex, K_FOREVER);
	state.shutdown_requested = true;
	k_mutex_unlock(&sys_mutex);
}