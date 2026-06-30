/*
 * matrix_keypad.h — Escaneo no bloqueante de teclado matricial 4x4
 */

#ifndef MATRIX_KEYPAD_H
#define MATRIX_KEYPAD_H

#include <stdbool.h>

/*
 * Escanea el teclado una vez (no bloqueante). Si hay una tecla presionada,
 * la escribe en *out_key y retorna true. Si no hay tecla, retorna false.
 * Debe llamarse periódicamente desde ui_keypad_task.
 */
bool matrix_keypad_scan(char *out_key);

#endif /* MATRIX_KEYPAD_H */