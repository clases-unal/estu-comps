# Decisiones del Proyecto y Procedimiento a Seguir

**Sistema de Control Térmico de Alta Disponibilidad**
**Plataforma:** STM32L476RG + Zephyr OS + ESP32
**Documento:** Registro de decisiones de sesión inicial
**Fecha de sesión:** Junio 2026

---

## Propósito de este documento

Este archivo registra todas las decisiones tomadas durante la sesión de planificación inicial del proyecto, incluyendo las razones detrás de cada una, los enfoques descartados y el procedimiento concreto a seguir. Sirve como punto de partida y referencia mientras se desarrolla la documentación formal en `docs/`.

---

## 1. Contexto general del proyecto

### 1.1 Descripción

Sistema embebido industrial basado en STM32L476RG con Zephyr OS que gestiona la refrigeración activa de un proceso térmico. El sistema lee temperatura mediante un termistor NTC, clasifica la temperatura según umbrales configurables, controla un ventilador por PWM, muestra información en una pantalla OLED, indica estados mediante LEDs y transmite telemetría a un ESP32 que aloja un dashboard web.

### 1.2 Naturaleza del proyecto

El proyecto es académico con maqueta física funcional. El documento de especificación debe impresionar técnicamente y también servir como guía de implementación durante el desarrollo del firmware.

---

## 2. Decisiones de hardware

### DEC-H-001 — Eliminación del registro de desplazamiento SN74HC595N

**Decisión:** Usar GPIOs directos del STM32 para controlar los LEDs de estado, descartando el registro de desplazamiento SN74HC595N planificado inicialmente.

**Problema encontrado:** Al insertar el SN74HC595N en la protoboard ocupa filas a ambos lados, dejando disponible un único nodo entre la salida del registro y tierra. Esto impide conectar tanto la resistencia como el LED en serie de forma coherente sin modificar físicamente los componentes (soldando resistencias directamente a los LEDs), lo cual no es viable para una maqueta en protoboard.

**Alternativas evaluadas:**

| Alternativa | Problema |
|---|---|
| Continuar intentando con el registro | Bloquea el avance del proyecto sin certeza de solución |
| Soldar resistencias a los LEDs | No viable en contexto de protoboard académica |
| GPIOs directos con menos LEDs | Viable, simple, avance inmediato |

**Consecuencias:** Se reducen de 15 LEDs a 6 LEDs. Se pierde la escalabilidad del registro de desplazamiento. Se gana simplicidad de hardware y desbloqueo del progreso.

**Nota de seguimiento:** Si en el futuro se resuelve el problema de la protoboard, el módulo LED puede refactorizarse para usar el registro nuevamente sin afectar la lógica de la FSM principal.

---
### DEC-H-002 — Esquema unificado y lógica de control para los LEDs de estado

**Decisión:** Implementar un esquema de visualización dividido en dos bloques de información claramente diferenciados (Salud del Sistema/Comunicaciones y Barra Térmica Acumulativa) utilizando GPIOs directos operados bajo lógica binaria pura (encendido, apagado y parpadeos a frecuencias fijas). Se descarta explícitamente el uso de PWM en los indicadores visuales para efectos de atenuación progresiva (*fading*).

**Problema o justificación:**
- **Descarte de PWM:** Configurar PWM para los 6 LEDs consumiría múltiples canales de temporizadores de hardware (`TIMx`), los cuales son recursos críticos reservados para el control de velocidad del ventilador. Además, añadiría complejidad innecesaria en el *Devicetree* de Zephyr sin aportar valor operativo real.
- **Diferenciación en Modo Degradado:** El operador de la maqueta debe ser capaz de diagnosticar de manera 100% visual si un fallo es local (caída de la interfaz de la pantalla OLED) o remoto (caída de la telemetría del ESP32), garantizando la operabilidad de la máquina incluso a ciegas.
- **Consistencia de la Interfaz Física:** En un entorno industrial, las luces fijas representan estabilidad de estado, mientras que las intermitencias denotan transiciones, advertencias o fallas. Hacer parpadear un LED verde en condiciones normales (`COLD`) transmite una sensación falsa de error.

**Matriz de Comportamiento de LEDs:**

#### Bloque A: Salud del Sistema y Conectividad
Este bloque informa sobre la integridad del kernel y la comunicación con periféricos.

| LED | Patrón | Significado Técnico / Estado del Sistema |
| :--- | :--- | :--- |
| **Blanco** | Parpadeo constante (1000 ms) | **Heartbeat del Kernel:** Indica que el planificador y el bucle principal de Zephyr OS se ejecutan con normalidad. |
| **Azul** | Encendido Fijo | **Estado Nominal:** Pantalla OLED activa, teclado matricial respondiendo y enlace UART con el ESP32 operativo. |
| **Azul** | Parpadeo Lento (500 ms) | **Modo Degradado Tipo A:** Pérdida de comunicación UART con el ESP32. El control de lazo térmico local sigue activo pero aislado. |
| **Azul** | Parpadeo Rápido (200 ms) | **Modo Degradado Tipo B:** Falla crítica en la interfaz local (Error en el bus I2C de la pantalla OLED o teclado bloqueado). |

#### Bloque B: Barra Térmica Acumulativa (El "Termómetro")
Este bloque actúa de manera acumulativa y refleja exclusivamente el comportamiento de la planta térmica y los fallos críticos del lazo.

| LED | Patrón | Significado Técnico / Estado del Sistema |
| :--- | :--- | :--- |
| **Verde** | Encendido Fijo | Temperatura en rangos seguros (`COLD` / `LOW`). El ventilador se encuentra apagado o en ciclo mínimo de PWM. |
| **Amarillo** | Encendido Fijo | Temperatura supera el umbral medio (`MEDIUM`). Alerta leve (LEDs Verde y Amarillo permanecen encendidos fijos). |
| **Naranja** | Parpadeo Intermitente (500 ms) | Estado de alerta alta (`HIGH`). Temperatura supera umbral alto; el ventilador es forzado al 100% de PWM para mitigar el calor. |
| **Rojo** | Parpadeo Lento (1000 ms) | **Falla Crítica por Sobre-temperatura:** Expiró el timeout acumulado en el estado `HIGH`. **Toda la barra térmica anterior (Verde, Amarillo, Naranja) se bloquea en encendido fijo** para reflejar el desborde térmico. |
| **Rojo** | Parpadeo Ráfaga (200 ms) | **Falla Crítica por Sensor NTC Roto:** Lecturas del ADC fuera del rango físico (corto o cable abierto). El sistema queda ciego; **toda la barra térmica anterior se apaga por completo** para evitar falsas lecturas. |

**Comportamientos Especiales:**
- **Estado de Apagado (SHUTDOWN):** Al emitirse el evento de apagado para escribir datos en la memoria flash (NVS), todos los LEDs térmicos y el latido blanco se apagan inmediatamente. Únicamente el **LED Rojo parpadea de manera muy lenta (2000 ms)** para alertar al operador de que no debe retirar la alimentación hasta que finalice el guardado.

**Consecuencias:**
- Se optimiza el uso de hardware liberando recursos de temporizadores en el STM32.
- El hilo `led_representation_manager.c` adquiere una lógica determinista, centralizada y directamente enlazada a las variables de estado protegidas por los mutexes.
- El operador cuenta con una interfaz visual intuitiva capaz de reportar cualquier tipo de falla remota, de interfaz o de planta, cumpliendo el pilar de Alta Disponibilidad.

### DEC-H-003 — Arquitectura de hardware mantenida del diseño previo

Los siguientes elementos de hardware se mantienen tal como estaban especificados en `discussion.md`:

- **Sensor de temperatura:** Sonda con termistor NTC (Resistencia nominal: 10 kΩ a 25 ºC, Coeficiente B25/50: 3470 K ±1%, Rango de medición: -30 °C a +120 °C, encapsulado de acero inoxidable, cable 60 cm). La lectura se realiza mediante el ADC del STM32 y la conversión a temperatura real utilizando el algoritmo de Steinhart-Hart.
- **Control de ventilador:** PWM dedicado.
- **Pantalla OLED:** Una pantalla vía I2C (se reduce de dos a una; pendiente de confirmar).
- **Teclado matricial 4x4:** Escaneo no bloqueante.
- **Comunicación ESP32:** UART punto a punto.

> **Nota:** La reducción de dos pantallas OLED a una debe confirmarse explícitamente en la sesión siguiente.

---

## 3. Decisiones de firmware

### DEC-F-001 — Arquitectura de 7 tareas con Zephyr OS

**Decisión:** Mantener la arquitectura de 7 tareas definida en `discussion.md`. La eliminación del SN74HC595N no cambia la arquitectura de tareas, solo simplifica la Tarea 5 (LED Manager).

| Tarea | Nombre | Responsabilidad principal |
|---|---|---|
| 1 | Power & Status Manager | Monitoreo del pulsador, gestión del SHUTDOWN |
| 2 | Temperature Manager | Lectura ADC, conversión Steinhart-Hart, filtrado |
| 3 | Cooling Manager | Control PWM del ventilador, failsafe |
| 4 | Unified UI & Keypad | Pantalla OLED + teclado, timeout 30s |
| 5 | LED Manager | Control de 6 GPIOs directos según estado global |
| 6 | Heater Simulation | Keep-alive del proceso externo simulado |
| 7 | ESP32 Communication | Protocolo UART con CRC16 y número de secuencia |

### DEC-F-002 — Estructura global protegida con 5 mutexes independientes

**Decisión:** Formalizar la división del estado global en 5 estructuras de datos especializadas, protegidas individualmente por su propio mutex (`k_mutex`) en Zephyr OS, tal como se plantea en `discussion.md`. Se descarta explícitamente el uso de un único mutex global para todo el sistema.

**Problema o justificación:** El uso de un único mutex global para proteger la totalidad del estado provocaría una severa contención y bloqueos innecesarios entre los 7 hilos concurrentes del sistema, degradando el determinismo y el paralelismo en tiempo real que ofrece Zephyr OS. Al separar el estado en dominios funcionales independientes, aquellas tareas que no compiten por los mismos datos (por ejemplo, el escaneo del teclado y el registro de telemetría) pueden ejecutarse simultáneamente de forma segura.

**Estructuras y Mutexes definidos:**

| Mutex | Estructura Protegida | Uso Principal y Variables Críticas |
|---|---|---|
| `control_mutex` | `ControlState` | Gestión térmica y control PWM del ventilador (`current_temperature`, `fan_pwm_duty_cycle`, `current_threshold_code`). |
| `transmission_mutex` | `TransmissionState` | Gestión de comunicaciones UART con el ESP32 (`esp32_connected`, marcas de tiempo y banderas de reenvío pendiente). |
| `config_mutex` | `ConfigState` | Almacenamiento de parámetros modificables por el usuario mediante HMI (límites de umbrales `threshold_low/medium/high` y modo de operación). |
| `telemetry_mutex` | `TelemetryState` | Diagnóstico, estadísticas históricas de fallas y la máscara de bits activa `error_log_flags`. |
| `sys_mutex` | `SystemState` | Coordinación global de alta prioridad y banderas de seguridad del sistema (`system_enabled`, `shutdown_requested`). |

**Consecuencias:**
- Se maximiza la concurrencia y se minimiza el tiempo de permanencia en secciones críticas de los 7 hilos de Zephyr OS.
- Se previene la aparición de condiciones de carrera (*race conditions*) sin sacrificar el rendimiento general.
- Se añade una ligera responsabilidad en el desarrollo del firmware para garantizar la adquisición ordenada de mutexes en caso de que una tarea requiera consultar más de un dominio a la vez, mitigando así el riesgo de bloqueos mutuos (*deadlocks*).

### DEC-F-003 — Protocolo UART de 3 paquetes hacia ESP32

**Decisión:** Mantener el protocolo de 3 paquetes especificado en `discussion.md`, con posibilidad de añadir un cuarto tipo si la especificación del ESP32 lo requiere.

| ID | Tipo | Condición de envío |
|---|---|---|
| 0x01 | Dinámico (telemetría) | Cada 2s o si ΔT > 0.5°C |
| 0x02 | Configuración | Solo tras EVENT_CONFIG_UPDATED |
| 0x03 | Diagnóstico | En BOOT; incluye contadores de reinicio |

Todos los paquetes incluyen CRC16 y número de secuencia.

### DEC-F-004 — FSM del proceso externo (Heater Simulation) como GPIO keep-alive

**Decisión:** La señal del calentador simulado es un GPIO keep-alive activo solo cuando el sistema está en estado RUNNING sin errores. Esta es una simulación visual usando un LED. No requiere documentación detallada de secuencia de activación ya que eso pertenece a una expansión futura.

---

## 4. Decisiones del ESP32

### DEC-E-001 — Especificación interna del ESP32 queda como trabajo futuro

**Decisión:** La documentación del proyecto especifica la interfaz hacia el ESP32 (qué datos recibe, formato de paquetes, frecuencia) pero no define la arquitectura interna del firmware del ESP32.

**Lo que sí se documenta:**
- Formato de los paquetes UART recibidos
- Protocolo de CRC16 y número de secuencia
- Descripción general del dashboard web (qué debe mostrar)

**Lo que queda pendiente:**
- Arquitectura interna del firmware ESP32
- Manejo de desconexión y reconexión
- Estructura del dashboard web

---

## 5. Decisiones de documentación

### DEC-D-001 — Estructura de múltiples documentos en carpeta docs/

**Decisión:** La documentación se organiza en 5 archivos markdown dentro de `docs/`, siguiendo la guía `PROJECT_STRUCTURE_GUIDE.md` con ajustes menores.

```
docs/
├── 01-system-specification.md
├── 02-firmware-architecture.md
├── 03-state-machines.md
├── 04-design-decisions.md
├── 05-validation-plan.md
└── diagrams/
    ├── fsm-main.svg
    ├── fsm-thermal.svg
    ├── fsm-cooling.svg
    ├── fsm-power.svg
    ├── architecture-tasks.svg
    └── hardware-connections.svg
```

**Razón para múltiples documentos en lugar de uno solo:** Cada documento responde una única pregunta. Esto permite navegar directamente a lo relevante durante la implementación, corregir una sección sin afectar las demás y hacer que el docente pueda evaluar cada aspecto por separado. Un documento único de la extensión requerida sería difícil de mantener y de seguir durante el desarrollo.

**Razón para no usar más de 5 documentos:** Las etapas del sistema están muy interrelacionadas. Fragmentar más generaría referencias cruzadas constantes entre documentos y repetiría conceptos. 5 archivos es el punto de equilibrio.

### DEC-D-002 — Decisiones de diseño en documento separado (04-design-decisions.md)

**Decisión:** Todas las notas del tipo "se evaluó A vs B, se eligió B porque..." que estaban dispersas en `discussion.md` migran a `04-design-decisions.md` con formato estructurado DEC-XXX. No se eliminan; se formalizan.

**Consecuencia:** Los documentos técnicos (01, 02, 03) quedan limpios de notas de contexto. Las justificaciones quedan centralizadas y trazables.

### DEC-D-003 — Diagramas en formato SVG dentro de docs/diagrams/

**Decisión:** Todos los diagramas se generan y almacenan en `docs/diagrams/`. Los documentos markdown los referencian con rutas relativas. Los archivos fuente editables (draw.io, mermaid, etc.) se almacenan en la misma carpeta.

---

## 6. Decisiones de repositorio y workflow

### DEC-R-001 — Un repositorio con dos branches permanentes

**Decisión:** El proyecto usa un único repositorio de GitHub con la siguiente estructura de branches:

```
main    ← solo lo que está aprobado y verificado
dev     ← trabajo en progreso
```

**Por qué no repositorios separados:** Los repositorios separados dificultan la trazabilidad entre código y documentación, generan problemas de sincronización y no representan el flujo real del proyecto. El uso de branches es la práctica estándar en desarrollo de software y es suficiente para este caso.

**Workflow operativo:**

```bash
# Siempre trabajar en dev
git checkout dev

# Guardar cambios
git add .
git commit -m "descripción clara del cambio"
git push origin dev
```

Cuando un conjunto de cambios está verificado y completo, se fusiona a `main` usando un Pull Request desde la interfaz web de GitHub. No se requiere conocimiento adicional de Git para esto.

**Regla importante:** Nunca editar `main` directamente. Si algo se rompe en `dev`, solo afecta el trabajo en progreso.

### DEC-R-002 — Estructura del repositorio

```
project-root/
│
├── README.md                    ← punto de entrada, máximo 1 página
│
├── docs/
│   ├── 01-system-specification.md
│   ├── 02-firmware-architecture.md
│   ├── 03-state-machines.md
│   ├── 04-design-decisions.md
│   ├── 05-validation-plan.md
│   └── diagrams/
│
├── firmware/
│   ├── stm32/
│   └── esp32/
│
├── hardware/
│   ├── wiring-diagrams/
│   └── bom.md
│
└── references/
    ├── datasheets/
    └── zephyr-docs/
```

**Cambio respecto a la guía original:** `programa/` se renombra a `firmware/` por precisión técnica. `programa/shared/` se elimina por ahora; el protocolo UART compartido se documenta en `02-firmware-architecture.md`.

---

## 7. Procedimiento a seguir

### Fase 0 — Confirmaciones pendientes (próxima sesión)

Antes de escribir cualquier documento formal, confirmar:

1. **Mapa de periféricos del STM32L476RG:** Asignar pines concretos a cada periférico (SPI eliminado, I2C para OLED, ADC para NTC, PWM para ventilador, UART para ESP32, 6 GPIOs para LEDs, GPIOs para teclado matricial).
2. **Número de pantallas OLED:** [RESUELTO] Se utilizará una única pantalla.
3. **Tabla de representación LED:** Validar los 6 colores y sus estados contra el mapa de pines disponible en el microcontrolador.

### Fase 1 — Documentación base

Orden de redacción recomendado:

| Paso | Documento | Razón del orden |
|---|---|---|
| 1 | `01-system-specification.md` | Define el alcance; todo lo demás depende de esto |
| 2 | `04-design-decisions.md` (parcial) | Formalizar las decisiones ya tomadas antes de olvidar el contexto |
| 3 | `02-firmware-architecture.md` | Describe cómo se implementa lo especificado en 01 |
| 4 | `03-state-machines.md` | Detalla el comportamiento; requiere que la arquitectura esté clara |
| 5 | `docs/diagrams/` | Generar los diagramas SVG de todas las FSMs |
| 6 | `05-validation-plan.md` | Se completa parcialmente ahora; se termina durante la implementación |

### Fase 2 — Implementación del firmware

- Crear la estructura de carpetas `firmware/stm32/` con el proyecto Zephyr
- Implementar en el siguiente orden: estructura global + mutex → Temperature Manager → Cooling Manager → LED Manager → Power Manager → UI → Heater Simulation → ESP32 Communication
- Cada tarea implementada se valida contra los casos de prueba de `05-validation-plan.md`

### Fase 3 — Implementación del ESP32

- Definir la arquitectura interna del ESP32 (esto genera entradas adicionales en `04-design-decisions.md`)
- Implementar recepción UART con verificación CRC16
- Implementar dashboard web

### Fase 4 — Integración y validación

- Pruebas de sistema completo
- Completar `05-validation-plan.md` con resultados reales
- Revisión final de toda la documentación

---

## 8. Lo que NO cambia respecto al diseño previo

Para evitar confusión, estos elementos permanecen exactamente como estaban en `discussion.md`:

- Algoritmo Steinhart-Hart para conversión de temperatura
- Banda de silencio de 0.5°C y periodo mínimo de 2s para telemetría
- Failsafe del ventilador al 100% en caso de error de software
- Timeout de inactividad de UI de 30 segundos
- Pulsación corta (100-500ms) vs larga (2-5s) en el pulsador físico
- Protocolo CRC16 + número de secuencia en paquetes UART
- Modo degradado para fallos no críticos (OLED, ESP32, teclado)
- Escritura en flash antes del apagado durante SHUTDOWN

---

## 9. Estado actual del proyecto

| Elemento | Estado |
|---|---|
| Especificación funcional | Borrador avanzado en `discussion.md` |
| Arquitectura de firmware | Definida, pendiente de formalizar |
| FSMs | Definidas, pendientes de diagramar |
| Decisiones de diseño | Dispersas en `discussion.md`, pendientes de formalizar |
| Tabla de LEDs | Decidida en esta sesión, pendiente de validar contra pines |
| Mapa de periféricos | **Pendiente — próxima acción** |
| Firmware STM32 | No iniciado |
| Firmware ESP32 | No iniciado |
| Hardware / maqueta | Parcialmente ensamblado |
| Repositorio GitHub | No creado |
| Documentación formal | No iniciada |

---

*Documento generado en sesión de planificación — Junio 2026*
*Próxima acción: confirmar mapa de periféricos del STM32L476RG y número de pantallas OLED*
