/*
 * esp32_comm_manager.h — ESP32 Communication: protocolo UART, CRC16, secuencia
 *
 * Cada módulo de tasks/ expone únicamente una función de inicialización (si la
 * necesita); el hilo en sí se registra de forma estática en el .c con
 * K_THREAD_DEFINE, por lo que normalmente no hay más API pública que exponer aquí.
 */

#ifndef ESP32_COMM_MANAGER_H
#define ESP32_COMM_MANAGER_H

void esp32_comm_manager_init(void);

#endif /* ESP32_COMM_MANAGER_H */