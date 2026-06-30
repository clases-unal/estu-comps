# Arquitectura de Firmware

**Sistema de Control Térmico de Alta Disponibilidad**
**Plataforma:** STM32L476RG (Nucleo-L476RG) + Zephyr OS

> Este documento describe **cómo** se implementa lo especificado en `01-system-specification.md`.
> No contiene justificaciones de diseño (ver `04-design-decisions.md`) ni el detalle de
> comportamiento temporal de cada subsistema (ver `03-state-machines.md`).
>
> **Estado:** Esqueleto inicial. Se completa progresivamente a medida que cada módulo se
> implementa y se valida contra hardware real.

---

## 1. Modelo de concurrencia

El sistema se implementa como **7 hilos (threads) de Zephyr OS**, cada uno con una única
responsabilidad funcional. Ningún hilo accede directamente al estado de otro: toda
comunicación entre hilos ocurre a través de las 5 estructuras de estado protegidas (Sección 2).

| # | Hilo (archivo) | Responsabilidad | Prioridad propuesta* |
|---|---|---|---|
| 1 | `power_status_manager` | Monitoreo del pulsador (ISR + debounce), gestión de SHUTDOWN | Alta |
| 2 | `temperature_manager` | Lectura ADC, conversión Steinhart-Hart, filtrado | Alta |
| 3 | `cooling_manager` | Control PWM del ventilador, failsafe | Alta |
| 4 | `ui_keypad_task` | Pantalla OLED + teclado matricial, timeout 30s | Media |
| 5 | `led_representation_manager` | Control de 6 GPIOs directos según estado global | Media |
| 6 | `heater_simulation_task` | Keep-alive del proceso externo simulado | Baja |
| 7 | `esp32_comm_manager` | Protocolo UART con CRC16 y número de secuencia hacia ESP32 | Baja |

\* **Pendiente de validar durante implementación.** Las prioridades reales dependen de qué
tan crítico es el tiempo de respuesta de cada hilo; se confirman cuando exista carga real
del sistema y se puedan medir tiempos con `k_uptime_get()` o el subsistema de tracing de Zephyr.

> Nota de implementación: en Zephyr, los hilos se definen típicamente con
> `K_THREAD_DEFINE()` (estático, se crea en boot) o con `k_thread_create()` (dinámico).
> Para este proyecto se recomienda `K_THREAD_DEFINE()` ya que los 7 hilos son fijos y
> conocidos en tiempo de compilación — evita gestión manual de stacks.

---

## 2. Estado global y mutexes

Cinco estructuras de datos independientes, cada una protegida por su propio `k_mutex`.
Ningún hilo debe acceder a los campos internos de una estructura sin adquirir su mutex
correspondiente primero.

| Mutex | Estructura | Archivo | Dominio |
|---|---|---|---|
| `control_mutex` | `ControlState` | `state/control_state.c/.h` | Temperatura actual, duty cycle del PWM, código de umbral activo |
| `transmission_mutex` | `TransmissionState` | `state/transmission_state.c/.h` | Estado de conexión ESP32, timestamps, banderas de reenvío |
| `config_mutex` | `ConfigState` | `state/config_state.c/.h` | Umbrales editables por HMI, modo de operación |
| `telemetry_mutex` | `TelemetryState` | `state/telemetry_state.c/.h` | Diagnóstico, histórico de fallas, `error_log_flags` |
| `sys_mutex` | `SystemState` | `state/system_state.c/.h` | `system_enabled`, `shutdown_requested` |

### Regla de adquisición ordenada (prevención de deadlocks)

Si un hilo necesita más de un mutex simultáneamente, debe adquirirlos siempre en el orden
de la tabla anterior (`control_mutex` → `transmission_mutex` → `config_mutex` →
`telemetry_mutex` → `sys_mutex`). Esta regla es la única forma de garantizar ausencia de
deadlocks sin usar mecanismos adicionales de detección.

**Pendiente:** auditar, módulo por módulo, qué combinaciones de mutex usa cada hilo
realmente, una vez exista código funcional.

---

## 3. Estructura de directorios (`firmware/stm32/`)

```
firmware/stm32/
├── platformio.ini               // configuración de PlatformIO (placa, framework)
├── zephyr/
│   ├── CMakeLists.txt
│   ├── prj.conf
│   └── boards/
│       └── (overlays de devicetree específicos de la placa, si se requieren)
├── src/
│   ├── main.c                       // Inicialización del kernel, creación de hilos
│   ├── state/
│   │   ├── control_state.c/.h
│   │   ├── transmission_state.c/.h
│   │   ├── config_state.c/.h
│   │   ├── telemetry_state.c/.h
│   │   └── system_state.c/.h
│   ├── tasks/
│   │   ├── power_status_manager.c/.h
│   │   ├── temperature_manager.c/.h
│   │   ├── cooling_manager.c/.h
│   │   ├── ui_keypad_task.c/.h
│   │   ├── led_representation_manager.c/.h
│   │   ├── heater_simulation_task.c/.h
│   │   └── esp32_comm_manager.c/.h
│   ├── drivers/
│   │   ├── ntc_sensor.c/.h          // Lectura ADC + Steinhart-Hart
│   │   └── matrix_keypad.c/.h       // Escaneo de teclado
│   └── protocol/
│       └── uart_packet.c/.h         // Construcción/parseo de paquetes, CRC16
```

> Nota respecto a `discussion.md`: el driver `shift_register.c/.h` se elimina de esta
> estructura por DEC-H-001 (ver `04-design-decisions.md`). El control de LEDs ahora vive
> enteramente en `tasks/led_representation_manager.c`, sin un driver SPI intermedio.

---

## 4. Mapa de periféricos del STM32L476RG

> **Pendiente — bloqueante.** Esta tabla debe completarse antes de escribir el devicetree
> overlay y antes de implementar cualquier driver. Ver Fase 0 en el procedimiento del proyecto.

| Periférico | Función | Pin(es) propuestos | Estado |
|---|---|---|---|
| ADC1 canal 5 | Lectura termistor NTC | PA0 | **Confirmado** — ver `zephyr/boards/nucleo_l476rg.overlay` |
| TIMx (PWM) | Control velocidad ventilador | — | Pendiente |
| USART (UART) | Comunicación con ESP32 | — | Pendiente |
| I2C1 | Pantalla OLED | PB6/PB7 (reservado, no implementado aún) | Pendiente |
| GPIO ISR | Pulsador físico | — | Pendiente |
| GPIO x6 | LEDs de estado (Bloque A + Bloque B) | — | Pendiente |
| GPIO matricial | Teclado 4x4 (4 filas + 4 columnas) | — | Pendiente |

> **Nota de circuito sin confirmar:** la conversión en `ntc_sensor.c` asume que el
> NTC está conectado entre VDD y el nodo ADC (PA0), con la resistencia fija de 10kΩ
> entre PA0 y GND. Si el cableado real es al revés, hay que invertir la fórmula —
> ver comentario "TOPOLOGÍA A / TOPOLOGÍA B" en ese archivo.

---

## 5. Protocolo UART (STM32 → ESP32)

| ID | Tipo | Condición de envío |
|---|---|---|
| `0x01` | Telemetría dinámica | Cada 2s o si ΔT > 0.5°C (con banda de silencio) |
| `0x02` | Configuración | Solo tras `EVENT_CONFIG_UPDATED` |
| `0x03` | Diagnóstico | En BOOT; incluye contadores de reinicio |

Todos los paquetes incluyen CRC16 y número de secuencia. El formato exacto de bytes
(framing, longitud de payload por tipo) se detalla cuando se implemente `protocol/uart_packet.c`.

---

## 6. Build system

El proyecto usa **PlatformIO** (extensión de VSCode) como entorno de desarrollo,
con `framework = zephyr` sobre la placa `nucleo_l476rg`. Esto difiere del flujo
`west` nativo de Zephyr en la organización de archivos (`CMakeLists.txt` y
`prj.conf` viven en `firmware/stm32/zephyr/`, no en la raíz del proyecto de
firmware) y en que no hay menús interactivos de Kconfig (`menuconfig`); toda
opción se escribe directamente en `prj.conf`. Ver
`firmware/stm32/README.md` para el detalle completo y el procedimiento de
primer build.

---

*Documento generado como esqueleto inicial. Se expande con cada módulo implementado.*
