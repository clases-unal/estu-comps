/*
 * matrix_keypad.h — Escaneo no bloqueante de teclado matricial 4×4
 *
 * Layout de teclas:
 *   Fila\Col  C0   C1   C2   C3
 *     R0      1    2    3    A
 *     R1      4    5    6    B
 *     R2      7    8    9    C
 *     R3      *    0    #    D
 *
 * Pines (ver overlay):
 *   Filas (salida): PC0, PC1, PC2, PC3
 *   Columnas (entrada pull-up): PC4, PC5, PC6, PC7
 */

#ifndef MATRIX_KEYPAD_H
#define MATRIX_KEYPAD_H

#include <stdbool.h>

/* Inicializa los GPIOs del teclado. Llamar una vez antes de scan. */
bool matrix_keypad_init(void);

/*
 * Escanea el teclado una vez (no bloqueante, ~1ms por llamada).
 * Si hay una tecla presionada, la escribe en *out_key y retorna true.
 * Si no hay tecla presionada, retorna false.
 * Llamar periódicamente desde ui_keypad_task (cada ~20ms es suficiente).
 */
bool matrix_keypad_scan(char *out_key);

#endif /* MATRIX_KEYPAD_H */
