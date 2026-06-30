/*
 * ntc_sensor.h — Driver de lectura del termistor NTC vía ADC + Steinhart-Hart
 *
 * NTC: Resistencia nominal 10kΩ @ 25°C, Coeficiente B25/50 = 3470K ±1%
 * (ver 00-project-decisions-and-procedure.md DEC-H-003)
 */

#ifndef NTC_SENSOR_H
#define NTC_SENSOR_H

#include <stdbool.h>

/*
 * Lee el ADC, aplica la ecuación de Steinhart-Hart y devuelve la temperatura en °C.
 * Retorna false si la lectura está fuera del rango físico esperado (sensor roto:
 * corto o cable abierto — ver discussion.md Sección 3.1, falla NTC).
 */
bool ntc_sensor_read_celsius(float *out_temperature);

#endif /* NTC_SENSOR_H */
