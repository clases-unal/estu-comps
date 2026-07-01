/*
 * uart_packet.c — Construcción de tramas y CRC16-CCITT
 */

#include <string.h>
#include "uart_packet.h"

uint16_t uart_packet_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFFu;

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int bit = 0; bit < 8; bit++) {
			if (crc & 0x8000u) {
				crc = (crc << 1) ^ 0x1021u;
			} else {
				crc <<= 1;
			}
		}
	}
	return crc;
}

size_t uart_packet_build(uint8_t *buf, size_t buf_size,
			 packet_type_t type, uint8_t seq,
			 const void *payload, uint8_t payload_len)
{
	/* Tamaño total: SOF + TYPE + SEQ + LEN + payload + CRC(2) */
	size_t total = 4u + payload_len + 2u;

	if (payload_len > UART_PACKET_MAX_PAYLOAD || total > buf_size) {
		return 0;
	}

	buf[0] = UART_PACKET_SOF;
	buf[1] = (uint8_t)type;
	buf[2] = seq;
	buf[3] = payload_len;

	if (payload_len > 0 && payload != NULL) {
		memcpy(&buf[4], payload, payload_len);
	}

	/* CRC sobre TYPE + SEQ + LEN + PAYLOAD (índices 1 a 3+payload_len) */
	uint16_t crc = uart_packet_crc16(&buf[1], 3u + payload_len);
	buf[4 + payload_len]     = (uint8_t)(crc >> 8);
	buf[4 + payload_len + 1] = (uint8_t)(crc & 0xFFu);

	return total;
}
