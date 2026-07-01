/*
 * uart_packet.h — Protocolo UART STM32 → ESP32
 *
 * Formato de trama:
 *   [SOF 1B][TYPE 1B][SEQ 1B][LEN 1B][PAYLOAD nB][CRC16 2B]
 *   SOF = 0xAA
 *   CRC16-CCITT sobre TYPE+SEQ+LEN+PAYLOAD (no incluye SOF ni el CRC mismo)
 */

#ifndef UART_PACKET_H
#define UART_PACKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define UART_PACKET_SOF       0xAAu
#define UART_PACKET_MAX_PAYLOAD 64u

typedef enum {
	PACKET_TYPE_TELEMETRY  = 0x01,  /* temperatura, duty, umbral — cada 2s */
	PACKET_TYPE_CONFIG     = 0x02,  /* umbrales configurados por HMI */
	PACKET_TYPE_DIAGNOSTIC = 0x03,  /* boot count, error flags — solo en boot */
} packet_type_t;

/* Payload de telemetría dinámica (tipo 0x01) */
typedef struct __attribute__((packed)) {
	int16_t  temperature_cdeg;  /* temperatura × 100, ej. 2530 = 25.30°C */
	uint8_t  fan_duty_pct;
	uint8_t  threshold_code;
	uint8_t  error_flags;
} payload_telemetry_t;

/* Payload de configuración (tipo 0x02) */
typedef struct __attribute__((packed)) {
	int16_t  threshold_low_cdeg;
	int16_t  threshold_medium_cdeg;
	int16_t  threshold_high_cdeg;
} payload_config_t;

/* Payload de diagnóstico (tipo 0x03) */
typedef struct __attribute__((packed)) {
	uint32_t boot_count;
	uint32_t error_count_ntc;
	uint32_t error_count_oled;
	uint32_t error_count_esp32;
} payload_diagnostic_t;

/* Calcula CRC16-CCITT (polinomio 0x1021, init 0xFFFF) */
uint16_t uart_packet_crc16(const uint8_t *data, size_t len);

/*
 * Construye una trama completa en buf[].
 * Retorna la longitud total de la trama, o 0 si payload_len > MAX_PAYLOAD.
 */
size_t uart_packet_build(uint8_t *buf, size_t buf_size,
			 packet_type_t type, uint8_t seq,
			 const void *payload, uint8_t payload_len);

#endif /* UART_PACKET_H */
