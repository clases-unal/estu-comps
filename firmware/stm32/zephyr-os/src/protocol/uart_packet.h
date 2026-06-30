/*
 * uart_packet.h — Construcción y parseo de paquetes UART hacia el ESP32
 *
 * Tres tipos de paquete (ver docs/02-firmware-architecture.md Sección 5):
 *   0x01 Telemetría dinámica · 0x02 Configuración · 0x03 Diagnóstico
 * Todos incluyen CRC16 y número de secuencia.
 */

#ifndef UART_PACKET_H
#define UART_PACKET_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
	PACKET_TYPE_TELEMETRY = 0x01,
	PACKET_TYPE_CONFIG = 0x02,
	PACKET_TYPE_DIAGNOSTIC = 0x03,
} packet_type_t;

/*
 * Calcula el CRC16 de un buffer. Algoritmo y polinomio específico TODO: definir
 * (CRC16-CCITT es la opción estándar más simple de implementar sin tabla).
 */
uint16_t uart_packet_crc16(const uint8_t *data, size_t len);

/*
 * TODO: definir el formato exacto de framing (byte de inicio, longitud,
 * payload, CRC) una vez se implemente este módulo. El formato de bytes no
 * está cerrado todavía — ver discussion.md Sección 6.
 */

#endif /* UART_PACKET_H */