/*
 * heater_simulation_task.h — Heater Simulation: keep-alive del proceso externo
 *
 * Cada módulo de tasks/ expone únicamente una función de inicialización (si la
 * necesita); el hilo en sí se registra de forma estática en el .c con
 * K_THREAD_DEFINE, por lo que normalmente no hay más API pública que exponer aquí.
 */

#ifndef HEATER_SIMULATION_TASK_H
#define HEATER_SIMULATION_TASK_H

void heater_simulation_task_init(void);

#endif /* HEATER_SIMULATION_TASK_H */