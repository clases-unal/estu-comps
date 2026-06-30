/*
 * led_representation_manager.h — LED Manager: 6 GPIOs directos
 *
 * Cada módulo de tasks/ expone únicamente una función de inicialización (si la
 * necesita); el hilo en sí se registra de forma estática en el .c con
 * K_THREAD_DEFINE, por lo que normalmente no hay más API pública que exponer aquí.
 */

#ifndef LED_REPRESENTATION_MANAGER_H
#define LED_REPRESENTATION_MANAGER_H

void led_representation_manager_init(void);

#endif /* LED_REPRESENTATION_MANAGER_H */
