/*
 * power_status_manager.h — Power & Status Manager: pulsador (ISR+debounce), SHUTDOWN
 *
 * Cada módulo de tasks/ expone únicamente una función de inicialización (si la
 * necesita); el hilo en sí se registra de forma estática en el .c con
 * K_THREAD_DEFINE, por lo que normalmente no hay más API pública que exponer aquí.
 */

#ifndef POWER_STATUS_MANAGER_H
#define POWER_STATUS_MANAGER_H

void power_status_manager_init(void);

#endif /* POWER_STATUS_MANAGER_H */