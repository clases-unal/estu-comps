/*
 * ntc_sensor.c
 *
 * TODO: implementar lectura real vía driver ADC de Zephyr (zephyr/drivers/adc.h)
 * y la conversión Steinhart-Hart:
 *     1/T = A + B*ln(R) + C*(ln(R))^3
 * Las constantes A, B, C dependen del termistor específico — derivarlas del
 * datasheet o calcularlas con 3 puntos de calibración conocidos.
 *
 * Requiere que el canal ADC esté definido en el devicetree overlay de la placa
 * (boards/) antes de poder implementar esto — ver mapa de periféricos pendiente
 * en docs/02-firmware-architecture.md Sección 4.
 */

#include "ntc_sensor.h"

bool ntc_sensor_read_celsius(float *out_temperature)
{
	/* TODO: leer ADC real, convertir a resistencia, aplicar Steinhart-Hart */
	*out_temperature = 25.0f; /* valor placeholder */
	return true;
}
