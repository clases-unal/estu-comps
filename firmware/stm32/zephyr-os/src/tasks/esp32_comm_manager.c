/*
 * esp32_comm_manager.c — Comunicación UART con el ESP32
 *
 * Protocolo: ver src/protocol/uart_packet.h
 *   Telemetría (0x01) cada 2s o si la temperatura cambia >0.5°C
 *   Configuración (0x02) solo cuando cambia (EVENT_CONFIG_UPDATED — pendiente)
 *   Diagnóstico (0x03) una sola vez al boot
 *
 * UART: USART1 en PA9(TX)/PA10(RX) a 115200 bps (ver overlay)
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <math.h>

#include "esp32_comm_manager.h"
#include "../protocol/uart_packet.h"
#include "../state/control_state.h"
#include "../state/config_state.h"
#include "../state/telemetry_state.h"
#include "../state/transmission_state.h"

LOG_MODULE_REGISTER(esp32_comm_manager, LOG_LEVEL_INF);

#define STACK_SIZE       2048    /* Mayor que otros: maneja buffers de trama */
#define THREAD_PRIORITY  6       /* Baja prioridad */

#define TELEMETRY_PERIOD_MS    2000
#define TEMP_DELTA_THRESHOLD   0.5f   /* °C — fuerza envío aunque no haya expirado el timer */

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1));

static uint8_t tx_buf[4 + sizeof(payload_telemetry_t) + 2 + 4];  /* trama máxima esperada */
static uint8_t seq_counter = 0;

static bool uart_ok = false;

static void send_bytes(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart_dev, data[i]);
	}
}

static void send_diagnostic(void)
{
	TelemetryState tel;
	telemetry_state_get(&tel);

	payload_diagnostic_t diag = {
		.boot_count       = tel.system_boot_count,
		.error_count_ntc  = tel.error_count[0],
		.error_count_oled = tel.error_count[1],
		.error_count_esp32 = tel.error_count[2],
	};

	size_t len = uart_packet_build(tx_buf, sizeof(tx_buf),
				       PACKET_TYPE_DIAGNOSTIC, seq_counter++,
				       &diag, sizeof(diag));
	if (len > 0) {
		send_bytes(tx_buf, len);
		LOG_INF("Diagnostico enviado (boot=%u)", tel.system_boot_count);
	}
}

static void send_telemetry(float temperature, uint8_t duty, uint8_t threshold,
			   uint8_t error_flags)
{
	payload_telemetry_t payload = {
		.temperature_cdeg = (int16_t)(temperature * 100.0f),
		.fan_duty_pct     = duty,
		.threshold_code   = threshold,
		.error_flags      = (uint8_t)error_flags,
	};

	size_t len = uart_packet_build(tx_buf, sizeof(tx_buf),
				       PACKET_TYPE_TELEMETRY, seq_counter++,
				       &payload, sizeof(payload));
	if (len > 0) {
		send_bytes(tx_buf, len);
		transmission_state_mark_sent(k_uptime_get());
	}
}

static void esp32_comm_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("esp32_comm_manager thread started");

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("USART1 no listo — verificar overlay (PA9/PA10) y CONFIG_SERIAL=y");
		uart_ok = false;
	} else {
		uart_ok = true;
		transmission_state_set_connected(true);
		LOG_INF("USART1 listo");
	}

	/* Dar tiempo a los otros hilos para que inicialicen su estado */
	k_sleep(K_MSEC(500));

	if (uart_ok) {
		send_diagnostic();
	}

	float last_temp_sent = -999.0f;
	int64_t last_send_ms = 0;

	while (1) {
		k_sleep(K_MSEC(200));  /* Checar condiciones cada 200ms */

		if (!uart_ok) {
			continue;
		}

		ControlState ctrl;
		TelemetryState tel;
		control_state_get(&ctrl);
		telemetry_state_get(&tel);

		int64_t now_ms = k_uptime_get();
		bool timer_expired   = (now_ms - last_send_ms) >= TELEMETRY_PERIOD_MS;
		bool delta_exceeded  = fabsf(ctrl.current_temperature - last_temp_sent)
				       >= TEMP_DELTA_THRESHOLD;

		if (timer_expired || delta_exceeded) {
			send_telemetry(ctrl.current_temperature,
				       ctrl.fan_pwm_duty_cycle,
				       (uint8_t)ctrl.current_threshold_code,
				       (uint8_t)(tel.error_log_flags & 0xFF));

			last_temp_sent = ctrl.current_temperature;
			last_send_ms   = now_ms;

			LOG_INF("Telemetria enviada: T=%.2f duty=%u umbral=%u",
				(double)ctrl.current_temperature,
				ctrl.fan_pwm_duty_cycle,
				ctrl.current_threshold_code);
		}
	}
}

K_THREAD_DEFINE(esp32_comm_tid, STACK_SIZE, esp32_comm_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
