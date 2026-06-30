/*
 * uart_packet.c
 *
 * TODO: implementar CRC16-CCITT real y el framing de paquetes. Este stub
 * retorna 0 para permitir que el resto del sistema compile mientras se
 * implementa el protocolo.
 */

#include "uart_packet.h"

uint16_t uart_packet_crc16(const uint8_t *data, size_t len)
{
	(void)data;
	(void)len;
	/* TODO: implementar CRC16-CCITT real */
	return 0;
}
