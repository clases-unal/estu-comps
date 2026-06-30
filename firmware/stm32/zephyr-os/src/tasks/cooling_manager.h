/*
 * cooling_manager.h — Cooling Manager: PWM del ventilador, failsafe
 *
 * Cada módulo de tasks/ expone únicamente una función de inicialización (si la
 * necesita); el hilo en sí se registra de forma estática en el .c con
 * K_THREAD_DEFINE, por lo que normalmente no hay más API pública que exponer aquí.
 */

#ifndef COOLING_MANAGER_H
#define COOLING_MANAGER_H

void cooling_manager_init(void);

#endif /* COOLING_MANAGER_H */
