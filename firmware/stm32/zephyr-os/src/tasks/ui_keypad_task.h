/*
 * ui_keypad_task.h — Unified UI & Keypad: OLED + teclado, timeout 30s
 *
 * Cada módulo de tasks/ expone únicamente una función de inicialización (si la
 * necesita); el hilo en sí se registra de forma estática en el .c con
 * K_THREAD_DEFINE, por lo que normalmente no hay más API pública que exponer aquí.
 */

#ifndef UI_KEYPAD_TASK_H
#define UI_KEYPAD_TASK_H

void ui_keypad_task_init(void);

#endif /* UI_KEYPAD_TASK_H */