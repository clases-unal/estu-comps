# Especificación Técnica Maestra: Sistema de Control Térmico de Alta Disponibilidad

## Arquitectura de Hardware y Software Basada en STM32L476RG, Zephyr OS y ESP32

---

# 1. Introducción y Objetivos del Sistema

El presente documento define la arquitectura técnica exhaustiva y los criterios de diseño para un sistema de control térmico industrial de alta disponibilidad. El objetivo principal es monitorear un proceso térmico crítico (representado por una carga o "calentador simulado") y gestionar su refrigeración activa mediante un lazo de control determinista y concurrente.

## Objetivos Clave de Diseño

* **Determinismo y Concurrencia:** Implementación sobre el kernel de **Zephyr OS** utilizando un enfoque multifilamento (multithreading) para garantizar tiempos de respuesta predecibles.
* **Autoridad Absoluta del Control:** El microcontrolador **STM32L476RG** actúa como el cerebro y la autoridad central de control. Las fallas o caídas de la interfaz de visualización externa no comprometen la seguridad del lazo térmico.
* **Alta Disponibilidad:** Capacidad de operar en **Modo Degradado** ante fallas en componentes no críticos (pantallas, teclado o telemetría).
* **Telemetría Inteligente:** Reducción del ancho de banda y del tráfico en el bus de comunicaciones seriales mediante políticas de histéresis y temporización.

---

# 2. Arquitectura de Hardware y Periféricos

## 2.1. Unidad de Procesamiento Central: STM32L476RG (Nucleo-L476RG)

El STM32L476RG gestiona el hardware crítico, los sensores, los actuadores locales y la lógica de sincronización de tiempo real. Sus periféricos se asignan de la siguiente manera:

* **ADC (Analog-to-Digital Converter):** Configurado a una resolución de 12 bits para la lectura del termistor NTC 3470. La conversión de la resistencia medida a grados Celsius se realiza mediante la ecuación de Steinhart-Hart:

  ```text
  1/T = A + B*ln(R) + C*(ln(R))^3
  ```

* **Timer en modo PWM (TIMx):** el STM32 no posee un bloque de hardware dedicado exclusivamente a "PWM"; en su lugar, uno de los Timers de propósito general (TIM) se configura en modo de generación de señal por comparación (*Capture/Compare*), produciendo una onda cuadrada de ciclo de trabajo variable sobre el pin asociado. Esta señal se orienta al control de velocidad del ventilador de refrigeración (rango operativo de 0% a 100%). Ver Sección 5.4 para el detalle de asignación de Timers por tarea.

* **UART (Universal Asynchronous Receiver-Transmitter):** Enlace serial asíncrono dedicado exclusivamente a la comunicación STM32 ↔ ESP32. Si bien la mayor parte del tráfico es STM32 → ESP32 (telemetría, configuración, errores), el enlace requiere un segundo hilo de recepción en el STM32 para procesar las confirmaciones de actividad (*heartbeat*) provenientes del ESP32 (ver Sección 6.4). **El uso de SPI para este enlace queda descartado**; toda la comunicación entre ambos microcontroladores se realiza exclusivamente por UART.

* **I2C (Inter-Integrated Circuit):** Bus de hardware para la comunicación con la pantalla OLED única del sistema (ver justificación de esta decisión en Sección 7.3).

* **SPI (Serial Peripheral Interface):** Líneas de hardware dedicadas para el control de alta velocidad del registro de desplazamiento. Se prohíbe el uso de simulación por software (*bit-banging*) para no consumir ciclos de CPU críticos.

* **GPIO (General Purpose Input/Output):**

  * *Entrada con interrupción externa (ISR):* Monitoreo del pulsador físico único del sistema, con filtrado de rebotes y discriminación por tiempo de pulsación (ver Sección 7.1).
  * *Salida digital (Keep-alive):* Línea de habilitación del proceso térmico externo.
  * *Líneas Matriciales:* Pines configurados como filas (salidas) y columnas (entradas con pull-up/pull-down) para el escaneo del teclado 4x4.

## 2.2. Interfaz de Monitoreo Remoto: ESP32

El ESP32 se configura exclusivamente como un **Punto de Acceso inalámbrico (AP - Access Point)**, con IP estática `192.168.4.1`. Actúa como un esclavo de visualización encargado de:

* Levantar un servidor web local y un dashboard gráfico.
* Recibir, parsear y graficar históricamente los datos seriales provenientes del STM32 (telemetría, configuración y diagnóstico de errores; ver Sección 6).
* El ESP32 no tiene potestad para modificar el lazo de control por sí mismo; es una entidad puramente receptiva y de interfaz humana remota.

> **Pendiente de definición (ver Sección 9):** el comportamiento detallado del ESP32 ante la pérdida del enlace UART (su propio modo degradado) se desarrollará en una iteración posterior de este documento.

## 2.3. Subsistema de Expansión Visual: Registros de Desplazamiento SN74HC595N

Para optimizar el uso de pines GPIO en el STM32 y asegurar la escalabilidad del sistema, se incorporan **dos** circuitos integrados **SN74HC595N** (salida en paralelo con cerrojo o *Latch*), encadenados en cascada sobre el mismo bus SPI. La ventaja crítica de este componente frente a opciones como el SN74HC164 es su memoria intermedia, la cual evita el parpadeo visual de los LEDs durante los desplazamientos de bits.

Se optó por **dos registros físicos independientes**, en lugar de uno solo con mayor multiplexación temporal, para separar conceptualmente dos dominios de información: el **Registro 1** representa el estado térmico normal del lazo de control (qué tan caliente está el proceso y cómo responde el ventilador), mientras que el **Registro 2** representa el **diagnóstico de salud del sistema** (qué módulos están fallando, si el sistema entró en alarma permanente y si la planta externa está autorizada a operar). Esta separación evita sobrecargar un único registro con dos tipos de información de naturaleza distinta.

**Nota sobre patrones de parpadeo:** todas las salidas digitales de ambos registros son estrictamente binarias (encendido/apagado) — el SN74HC595N no provee salida analógica ni PWM nativo. Por esta razón, ningún LED del sistema usa un efecto de "fundido" de brillo (que requeriría generar PWM por software a alta frecuencia sobre el mismo bus SPI, gastando ciclos de CPU en un efecto puramente estético); todo patrón de "latido" o parpadeo se construye alternando entre dos salidas digitales simples, desfasadas entre sí, o intermitiendo una sola salida a un ritmo fijo. Todos los pares de LEDs desfasados del sistema (heartbeat, HIGH/CRITICAL por timeout, causa de CRITICAL por falla de sensor) comparten una única frecuencia de referencia; la distinción entre ellos la da su posición física en el circuito, no su velocidad de parpadeo.

### Registro 1 — Estado Térmico (Salidas Q0-Q7)

| Pin de Salida | Componente Asociado | Función Técnica                                                           | Lógica Activa               |
| ------------- | -------------------- | --------------------------------------------------------------------------- | ----------------------------- |
| **Q0**        | LED Blanco A           | Indicador de Kernel Vivo (**Heartbeat**), fase A                            | Alternado con Q1, desfasado 180°  |
| **Q1**        | LED Blanco B           | Indicador de Kernel Vivo (**Heartbeat**), fase B                            | Alternado con Q0, desfasado 180°  |
| **Q2**        | LED Azul              | Estado COLD activo                                                          | Ver lógica de barra progresiva, abajo |
| **Q3**        | LED Verde             | Estado LOW activo                                                           | Ver lógica de barra progresiva, abajo |
| **Q4**        | LED Amarillo          | Estado MEDIUM activo                                                        | Ver lógica de barra progresiva, abajo |
| **Q5**        | LED Rojo              | Estado HIGH activo                                                          | Ver lógica de barra progresiva, abajo |
| **Q6**        | LED Rojo              | Estado CRITICAL activo (sobre-temperatura sostenida **o** falla de sensor)  | Ver lógica de barra progresiva, abajo |
| **Q7**        | No usado              | Expansión futura del sistema                                                | N/A                            |

**Lógica de barra progresiva (Q2-Q6) en operación normal:** los cinco LEDs de estado térmico se comportan como una barra acumulativa, no como cinco indicadores independientes. Para el estado actualmente activo, su LED correspondiente parpadea de forma intermitente (llamando la atención sobre el punto exacto donde está el sistema en este momento); todos los LEDs de estados *inferiores* al actual permanecen encendidos de forma continua (mostrando el camino recorrido); todos los LEDs de estados *superiores* al actual permanecen apagados. Por ejemplo, con el sistema en MEDIUM: Q2 (COLD) y Q3 (LOW) encendidos fijos, Q4 (MEDIUM) intermitente, Q5 (HIGH) y Q6 (CRITICAL) apagados.

**Caso especial — escalado a CRITICAL por timeout en HIGH (sobre-temperatura sostenida):** cuando el sistema escala de HIGH a CRITICAL por esta causa (Sección 4.1), Q5 y Q6 **parpadean juntos, desfasados 180° entre sí**, en lugar de que Q5 se apague y solo Q6 parpadee. Este patrón combinado comunica visualmente que el sistema proviene de un HIGH sostenido que no logró resolverse. Esta causa de CRITICAL se representa **íntegramente dentro del Registro 1**; el Registro 2 no necesita reflejar nada adicional por esta causa específica.

**Caso especial — CRITICAL por falla de sensor NTC:** en esta condición, el sistema ya no tiene una lectura de temperatura confiable, por lo que mostrar cualquier LED de la barra progresiva (Q2-Q6) representaría información falsa — por ejemplo, dejar encendido el LED de COLD comunicaría que el sistema "sigue sabiendo" que la temperatura es baja, cuando en realidad esa información dejó de ser válida en el instante en que se perdió la lectura. Por esta razón, **toda la barra progresiva (Q2 a Q6) se apaga por completo** mientras esta condición esté activa, incluyendo Q6 — la señal de "estoy en CRITICAL por esta causa" no se representa en el Registro 1 en absoluto, sino exclusivamente en el Registro 2 (ver más abajo). **El heartbeat (Q0/Q1) es la única excepción: continúa funcionando con normalidad**, ya que no representa una lectura térmica sino la actividad del kernel de Zephyr, que sigue vivo y en ejecución incluso durante esta condición (de hecho, debe estarlo, para poder ejecutar la lógica de dejar de alimentar el Watchdog, Sección 4.4).

> **Nota de diseño:** cada una de las dos causas de CRITICAL queda representada en un único registro físico, sin solaparse entre ambos: sobre-temperatura sostenida se señaliza únicamente en el Registro 1 (Q5/Q6); falla de sensor se señaliza únicamente en el Registro 2 (Q6/Q7, ver más abajo). Esto permite que un observador distinga la causa con solo identificar en cuál de los dos registros está ocurriendo el parpadeo de alerta máxima, sin necesidad de leer ambos registros en conjunto.

### Registro 2 — Diagnóstico de Salud del Sistema (Salidas Q0-Q7)

| Pin de Salida | Bit de `error_log_flags` | Componente Asociado | Lógica Activa |
| ------------- | --------------------------- | -------------------- | ----------------- |
| **Q0**        | Bit 0                        | Bandera de falla: NTC / ADC | Intermitente mientras activa |
| **Q1**        | Bit 1                        | Bandera de falla: Pantalla OLED | Intermitente mientras activa |
| **Q2**        | Bit 2                        | Bandera de falla: ESP32 / UART | Intermitente mientras activa |
| **Q3**        | Bit 3                        | Bandera de falla: Teclado matricial | Intermitente mientras activa |
| **Q4**        | —                            | Indicador de **Sistema en Alarma Permanente** (Sección 4.5) | Encendido fijo |
| **Q5**        | —                            | **Autorización de Planta de Calefacción Externa** — espejo visual del estado del GPIO *keep-alive* (Sección 4.6) | Encendido fijo mientras el keep-alive está activo |
| **Q6**        | — (activo cuando `current_threshold_code == CRITICAL` por causa de sensor) | CRITICAL por falla de sensor, fase A | Intermitente, desfasado 180° con Q7 |
| **Q7**        | — (activo cuando `current_threshold_code == CRITICAL` por causa de sensor) | CRITICAL por falla de sensor, fase B | Intermitente, desfasado 180° con Q6 |

**Lógica de parpadeo del Registro 2:** las cuatro banderas individuales de falla (Q0-Q3) son **intermitentes** mientras el módulo correspondiente esté en falla activa, para llamar la atención del observador. El indicador de Alarma Permanente (Q4) y el indicador de autorización de la planta externa (Q5) son **encendidos fijos**, ya que ambos representan estados sostenidos del sistema (no eventos puntuales que requieran parpadeo). El par Q6/Q7 es la única representación visual de CRITICAL por falla de sensor en todo el sistema de LEDs: nótese que Q0 (bandera de falla de NTC) puede estar activo de forma independiente de Q6/Q7 — Q0 indica simplemente "el NTC está fallando ahora", mientras que Q6/Q7 indican específicamente "el sistema está en CRITICAL como consecuencia de esa falla". En la práctica, dado que una falla de NTC activa siempre escala a CRITICAL (Sección 4.1), ambos pares tienden a estar activos de forma simultánea durante esta condición.

Con este mapeo, el Registro 2 utiliza sus 8 salidas en su totalidad, sin bits reservados para expansión futura en esta versión del diseño.

---

# 3. Arquitectura de Software en Zephyr OS

## 3.1. Estructura Global de Estado y Regiones Críticas

Con el objetivo de minimizar la contención entre hilos concurrentes y reducir el tiempo de bloqueo en secciones críticas, el estado global del sistema se divide en múltiples estructuras de datos especializadas. Cada estructura representa un dominio funcional independiente y posee su propio mecanismo de exclusión mutua mediante Mutex de Zephyr OS.

Esta estrategia evita que tareas no relacionadas compitan por un único recurso compartido, permitiendo un mayor grado de paralelismo y una mejor escalabilidad del software. **Esta decisión de diseño —cinco mutex independientes en lugar de un único estado global protegido por un solo mutex— se mantiene de forma deliberada como principio rector de la arquitectura**, incluso frente a alternativas más simples evaluadas durante el diseño, precisamente porque preserva el paralelismo entre los siete hilos del sistema (Sección 5).

### ¿Qué es un mutex y cómo funciona?

Un **mutex** (*mutual exclusion*, exclusión mutua) es un mecanismo de sincronización que garantiza que, en un sistema con múltiples hilos de ejecución concurrentes, **solo un hilo a la vez** pueda acceder a una sección de código o estructura de datos compartida (denominada *sección crítica*). Funciona como un cerrojo binario: un hilo debe *adquirir* el mutex (`k_mutex_lock`) antes de entrar a la sección crítica, y debe *liberarlo* (`k_mutex_unlock`) al salir. Si otro hilo intenta adquirir un mutex que ya está tomado, dicho hilo se bloquea (queda en espera, sin consumir ciclos de CPU) hasta que el mutex quede disponible.

Su propósito es prevenir **condiciones de carrera** (*race conditions*): situaciones en las que el resultado de una operación depende del orden no determinista en que dos o más hilos acceden a un mismo dato, lo que puede producir lecturas inconsistentes o corrupción de estado. En Zephyr OS, los mutex (`k_mutex`) además implementan herencia de prioridad, lo que mitiga el problema de *inversión de prioridad* (donde un hilo de baja prioridad que retiene un mutex bloquea indefinidamente a un hilo de mayor prioridad que lo necesita).

### --1. Estado de Control Térmico (`control_mutex`)

Almacena las variables asociadas al procesamiento de temperatura y al control del ventilador.

```c
typedef enum {
    THERMAL_STATE_COLD = 0,
    THERMAL_STATE_LOW = 1,
    THERMAL_STATE_MEDIUM = 2,
    THERMAL_STATE_HIGH = 3,
    THERMAL_STATE_CRITICAL = 4
} ThermalState;

typedef struct {
    float current_temperature;      // Última lectura procesada del NTC (promedio móvil)
    uint32_t fan_pwm_duty_cycle;    // Ciclo de trabajo actual del ventilador (0-100)
    ThermalState current_threshold_code; // Estado térmico activo (ver enum arriba)
    uint32_t time_in_high_state_ms; // Acumulador de tiempo continuo en estado HIGH (para escalado a CRITICAL)
} ControlState;
```

**Nota de consistencia de tipos:** `current_threshold_code` se tipa directamente como `ThermalState` (no como `uint8_t` genérico), de modo que el compilador pueda verificar en tiempo de compilación que solo se le asignan valores válidos del enum, evitando así un desacople entre la definición del estado y su uso en la estructura.

| Estado   | Condición                                                  | PWM  |
| -------- | ----------------------------------------------------------- | ---- |
| COLD     | T < threshold_low                                            | 0%   |
| LOW      | threshold_low ≤ T < threshold_medium                          | 30%  |
| MEDIUM   | threshold_medium ≤ T < threshold_high                          | 60%  |
| HIGH     | T ≥ threshold_high                                            | 100% |
| CRITICAL | Timeout de HIGH sostenido **o** falla de sensor (ver 4.3)      | 100% |

**Naturaleza de la tabla de PWM:** los porcentajes anteriores son **valores fijos por estado** (una tabla de consulta directa, no una función continua ni interpolada). Es decir, el ciclo de trabajo del ventilador salta discretamente entre 0%, 30%, 60% y 100% según el estado térmico activo, sin pasos intermedios entre transiciones. Esta es la estrategia de control adoptada para la presente versión del sistema; la posibilidad de sustituir esta tabla por una curva de respuesta no lineal (por ejemplo, logarítmica) se evalúa como mejora futura en la Sección 9.

### --2. Estado de Comunicación (`transmission_mutex`)

Contiene la información necesaria para la gestión de transmisiones UART y control de cambios de estado enviados al ESP32.

```c
typedef struct {
    float last_sent_temperature;       // Temperatura registrada en el último envío UART
    uint32_t last_telemetry_send_tick; // Marca de tiempo del último envío de telemetría
    bool esp32_connected;              // Resultado del último heartbeat (ver Sección 6.4)
    bool config_resend_pending;        // Config modificada mientras esp32_connected == false
    bool error_resend_pending;         // Error modificado mientras esp32_connected == false
} TransmissionState;
```

> Ver Sección 6 para el detalle completo del protocolo de comunicación (estructura de paquete, tipos de mensaje, disparo por temporizador/umbral, y mecanismo de *heartbeat*).

**Nota sobre `last_telemetry_send_tick`:** este campo se implementa mediante `k_uptime_get()`, la función nativa de Zephyr OS que retorna los milisegundos transcurridos desde el arranque del kernel. Esto **no requiere un RTC (Real-Time Clock)**: es un contador relativo al tiempo de actividad del sistema, no una marca de fecha/hora absoluta. La incorporación de un RTC real (con fecha y hora de calendario, típicamente respaldado por batería) permitiría registrar el instante exacto en que ocurre un evento (por ejemplo, "la falla ocurrió el 21/06/2026 a las 14:32" en lugar de "hace 4823 ms"), lo cual sería valioso para una implementación de producción, pero excede el alcance de la presente versión del proyecto. Esta mejora queda anotada en la Sección 9.

### --3. Estado de Configuración (`config_mutex`)

Agrupa todos los parámetros modificables por el usuario mediante la HMI.

```c
typedef struct {
    float threshold_low;            // Límite del umbral bajo (editable, persiste en NVS)
    float threshold_medium;         // Límite del umbral medio (editable, persiste en NVS)
    float threshold_high;           // Límite del umbral alto (editable, persiste en NVS)
    bool hmi_mode;                  // false = Monitoreo, true = Configuración
} ConfigState;
```

**Nota sobre la histéresis:** el margen de histéresis utilizado para evitar oscilaciones entre estados térmicos **no forma parte de esta estructura**. Se define como una constante fija en el código fuente (no editable por el usuario mediante el teclado). El razonamiento de esta decisión, junto con la explicación técnica del fenómeno, se desarrolla en la Sección 4.2.

**Nota sobre el botón de apagado/configuración:** el mismo pulsador físico que activa el apagado seguro del sistema se utiliza, con un tiempo de pulsación distinto, para acceder al modo de edición de umbrales. La lógica de discriminación por tiempo de pulsación se define en la Sección 7.1 y no requiere campos adicionales en esta estructura, ya que se resuelve íntegramente dentro de la rutina de interrupción (ISR) del botón y el temporizador asociado.

### --4. Estado de Telemetría y Diagnóstico (`telemetry_mutex`)

Mantiene estadísticas y registros persistentes relacionados con el funcionamiento del sistema.

```c
typedef struct {
    uint32_t system_boot_count;        // Contador de reinicios del microcontrolador (persiste en NVS)
    uint8_t error_log_flags;           // Máscara de bits: fallas ACTIVAS en este momento (no persiste)
    uint32_t error_count[4];           // Contador acumulado histórico de fallas por módulo (persiste en NVS)
    uint8_t ntc_consecutive_failures;  // Reinicios consecutivos por falla de NTC (persiste en NVS, tope 3; ver Sección 4.5)
} TelemetryState;
```

#### Tabla de bits de `error_log_flags`

| Bit | Módulo            | Significado cuando está en 1                                  | Mecanismo de limpieza del bit |
| --- | ------------------ | ---------------------------------------------------------------- | -------------------------------- |
| 0   | NTC / ADC           | Lectura fuera de rango físico válido (circuito abierto o corto)  | No se limpia en caliente — el sistema escala a reinicio físico (ver Sección 4.3/4.4) |
| 1   | Pantalla OLED       | Falla de comunicación I2C con la pantalla                         | Auto-verificación periódica cada 5 s (ver más abajo) |
| 2   | ESP32 / UART        | Pérdida de *heartbeat* (ver Sección 6.4)                           | Automática: se limpia al recibir un *heartbeat* válido nuevamente |
| 3   | Teclado matricial   | Patrón de lecturas físicamente incoherente durante el escaneo      | Auto-verificación periódica cada 5 s (ver más abajo) |
| 4-7 | Reservado           | Expansión futura                                                   | N/A                                |

**Justificación del tipo de dato:** `error_log_flags` se define como `uint8_t` (8 bits) y no como `uint32_t`, ya que el sistema solo requiere representar 4 fallas activas posibles más margen de expansión; usar un entero de 32 bits para esto sería un desperdicio de memoria sin justificación funcional en un microcontrolador con RAM limitada. En cambio, `error_count[4]`, `system_boot_count` y `ntc_consecutive_failures` sí se mantienen en tipos de mayor rango cuando corresponde, porque representan **contadores acumulados a lo largo de toda la vida útil del dispositivo**, donde sí existe la necesidad real de un rango amplio.

> **Nota sobre la bandera de falla del ventilador:** se evaluó incluir una quinta bandera para detectar fallas del ventilador (por ejemplo, motor detenido pese a recibir señal PWM), pero se descartó para esta versión: el sistema actual solo *envía* la señal PWM, sin ningún sensor de retroalimentación (como un tacómetro o sensor de efecto Hall) que permita confirmar el giro real del motor. Sin dicha retroalimentación, una bandera de este tipo nunca podría activarse en la práctica. La incorporación de un sensor de retroalimentación del ventilador queda anotada como mejora futura en la Sección 9.

**Semántica de auto-recuperación de fallas:** a diferencia de un esquema de "reconocimiento" (*acknowledge*) manual por parte del usuario, el sistema adopta una política de **auto-verificación periódica**: mientras un bit de `error_log_flags` esté activo, el módulo correspondiente reintenta su operación normal cada 5 segundos (igual intervalo que el *heartbeat* del ESP32, por consistencia de diseño), y el bit se limpia automáticamente en cuanto la operación vuelve a ser exitosa, sin intervención del usuario. Este mecanismo aplica a OLED y Teclado de forma directa; el caso de ESP32/UART ya cuenta con su propio mecanismo de verificación continua (el *heartbeat*, Sección 6.4), que cumple la misma función. **El caso de NTC/ADC es la única excepción**: dado que su recuperación implica un reinicio físico completo del sistema (Sección 4.3/4.4), no aplica un reintento "en caliente" sobre este bit específico. En todos los casos, **el historial acumulado en `error_count[4]` nunca se modifica por este mecanismo** — solo refleja el conteo histórico de fallas detectadas, independientemente de si ya fueron resueltas.

**Límite de reintentos automáticos:** reintentar indefinidamente cada 5 segundos, sin límite, gasta ciclos de CPU en verificar una y otra vez un subsistema cuya falla, pasado cierto punto, probablemente requiere intervención humana para resolverse. Para OLED y Teclado, tras **12 intentos consecutivos fallidos** (aproximadamente 1 minuto), el módulo correspondiente deja de reintentar automáticamente y pasa a un esquema de **verificación bajo demanda**: solo vuelve a intentarse la próxima vez que el sistema detecte una interacción directa relacionada (por ejemplo, actividad eléctrica en las líneas de columna del teclado, o un intento de cambiar `hmi_mode`). Este límite no aplica al *heartbeat* del ESP32 (que continúa de forma indefinida mientras el sistema opera, por ser la única vía de detectar una reconexión) ni al caso de NTC/ADC (que no reintenta en caliente, según lo ya establecido).

### --5. Estado General del Sistema (`sys_mutex`)

Contiene únicamente variables globales de coordinación que afectan a múltiples subsistemas y cuya consulta es frecuente por varios hilos.

```c
typedef struct {
    bool system_enabled;            // true = operación normal; false = protocolo de seguridad activo (ver Sección 4.5)
    bool shutdown_requested;        // Solicitud de apagado seguro
} SystemState;
```

**Semántica de `system_enabled`:** este campo es `true` durante toda operación normal del sistema, incluyendo los cinco estados térmicos habituales (COLD a CRITICAL por sobre-temperatura, que se recupera por sí solo). Pasa a `false` **exclusivamente** cuando el sistema entra en un protocolo de seguridad por error insalvable — concretamente, al alcanzar el tope de reinicios consecutivos por falla de sensor NTC (Sección 4.5). Mientras `system_enabled == false`, el sistema permanece en un estado de alarma sostenida que requiere intervención humana directa (revisión física del sensor) para resolverse; ningún módulo debe reanudar operaciones normales de control mientras esta bandera permanezca activa. Este campo vive en RAM y no persiste en NVS: cada nuevo ciclo de vida del sistema (tras una reconexión de alimentación) reevalúa la condición desde cero.

## 3.2. Mecanismos de Sincronización e IPC (Inter-Process Communication)

### Mutex (`k_mutex`)

Cada dominio funcional posee un mutex dedicado para proteger el acceso concurrente a sus estructuras de estado:

| Mutex                 | Estructura Protegida | Uso Principal                     |
| ----------------------- | ---------------------- | ------------------------------------ |
| `control_mutex`         | `ControlState`         | Gestión térmica y control PWM        |
| `transmission_mutex`    | `TransmissionState`    | Comunicación UART                    |
| `config_mutex`          | `ConfigState`           | Configuración de usuario             |
| `telemetry_mutex`       | `TelemetryState`        | Diagnóstico y estadísticas           |
| `sys_mutex`             | `SystemState`           | Coordinación global del sistema      |

Cualquier tarea que requiera consultar o modificar una estructura compartida deberá adquirir exclusivamente el mutex asociado a dicha estructura y liberarlo inmediatamente después de completar la operación. Esta política reduce el tiempo de bloqueo y evita condiciones de carrera.

### Objetos de Eventos (`k_event`)

Los eventos de Zephyr OS (`k_event`) son señales ligeras de sincronización que permiten a un hilo notificar a otros sobre la ocurrencia de un cambio de estado específico, sin necesidad de que los hilos receptores realicen *polling* activo (consultas repetidas que desperdician ciclos de CPU). Un hilo puede suspenderse (`k_event_wait`) hasta que se publique (`k_event_post`) uno o varios eventos de interés, momento en el cual el planificador de Zephyr lo despierta. A diferencia de un mutex, un evento no protege un recurso compartido: solo transporta la *notificación* de que algo ocurrió; el acceso al dato modificado sigue requiriendo el mutex correspondiente.

**¿Por qué usar eventos dedicados en lugar de que cada módulo consulte el estado directamente?** Un evento notifica un *cambio* — el instante en que algo pasó de un estado a otro — mientras que una variable de estado bajo mutex solo representa una condición actual, sin indicar cuándo cambió por última vez. Si los módulos interesados tuvieran que *descubrir* un cambio por sí mismos, existirían solo dos alternativas, ambas indeseables: hacer *polling* repetido de la variable (desperdiciando ciclos de CPU, en contra del objetivo de diseño de la Sección 1), o que el módulo productor llame directamente a funciones de los módulos consumidores (acoplándolos innecesariamente y rompiendo la separación de responsabilidades entre tareas que es central a la arquitectura, Sección 3.1/5). El patrón de eventos resuelve ambos problemas: el productor publica el cambio una sola vez, en el instante exacto en que ocurre, sin necesidad de conocer quiénes son sus consumidores ni cuántos hay.

* `EVENT_TEMPERATURE_UPDATED`: Emitido por el gestor de temperatura al procesar una nueva lectura válida.
* `EVENT_CONFIG_UPDATED`: Emitido por la tarea HMI al confirmarse un cambio de umbrales mediante el teclado.
* `EVENT_SHUTDOWN_REQUESTED`: Emitido por el gestor de energía para iniciar la secuencia de apagado seguro (activado por ISR de botón físico con pulsación larga, ver Sección 7.1).
* `EVENT_ERROR_DETECTED`: Emitido cuando algún módulo registra una condición de falla crítica.
* `EVENT_ESP32_DISCONNECTED`: Emitido cuando el mecanismo de *heartbeat* (Sección 6.4) determina que el ESP32 dejó de responder dentro del tiempo límite establecido.

| Evento                       | Productor                  | Consumidores                          |
| ------------------------------ | ----------------------------- | ---------------------------------------- |
| EVENT_TEMPERATURE_UPDATED      | Temperature Manager           | Cooling Manager, ESP32 Comm Manager, LED Representation Manager |
| EVENT_CONFIG_UPDATED           | Unified UI & Keypad Task      | Temperature Manager, ESP32 Comm Manager   |
| EVENT_SHUTDOWN_REQUESTED       | Power & Status Manager        | Todos los módulos                          |
| EVENT_ERROR_DETECTED           | Cualquier módulo               | Cooling Manager, Telemetry Manager, LED Representation Manager, Power & Status Manager |
| EVENT_ESP32_DISCONNECTED       | ESP32 Comm Manager             | Telemetry Manager, LED Representation Manager |

**Nota de consistencia:** el LED Representation Manager consume tres eventos, correspondientes a los dos registros de desplazamiento que gestiona (Sección 2.3). `EVENT_TEMPERATURE_UPDATED` determina qué LED del **Registro 1** debe encenderse según `current_threshold_code`; `EVENT_ERROR_DETECTED` y `EVENT_ESP32_DISCONNECTED` determinan qué bandera del **Registro 2** debe activarse o limpiarse según el contenido actualizado de `error_log_flags`. El Power & Status Manager se agrega como consumidor de `EVENT_ERROR_DETECTED` porque es quien decide, de forma reactiva, si debe dejar de alimentar el Watchdog (Sección 4.4) — sin suscribirse a este evento, dicho hilo tendría que consultar `error_log_flags` mediante *polling* constante para enterarse de una falla de sensor, contradiciendo el propósito mismo de usar eventos.

---

# 4. Lógica de Control Térmico

## 4.1. Máquina de Estados Térmicos y Escalado a CRITICAL

El sistema clasifica la temperatura procesada en cinco estados discretos (Sección 3.1). La transición a **CRITICAL** no depende únicamente de un valor instantáneo de temperatura, sino de dos condiciones independientes, ambas con la misma respuesta de seguridad (PWM al 100%) pero con orígenes y rutas de recuperación distintas:

1. **CRITICAL por sobre-temperatura sostenida:** el sistema permanece en estado HIGH (ventilador al 100%) durante un tiempo continuo superior al tolerable sin que la temperatura descienda por debajo del umbral alto. Esto indica que el proceso térmico está generando más calor del que el sistema de refrigeración puede disipar. El conteo de tiempo en HIGH se acumula en `time_in_high_state_ms` (Sección 3.1) y se reinicia cada vez que el sistema sale del estado HIGH.
2. **CRITICAL por falla de sensor NTC:** el ADC devuelve una lectura fuera del rango físico válido del termistor (circuito abierto o cortocircuito). En este caso, el sistema no tiene certeza del estado térmico real y, por seguridad, asume el peor escenario posible.

> **Nota de implementación:** el valor exacto del tiempo de tolerancia en HIGH antes de escalar a CRITICAL por sobre-temperatura, así como la acción específica sobre la línea *keep-alive* del proceso térmico externo en este escenario, quedan pendientes de definición (ver Sección 9).

## 4.2. Histéresis en la Transición de Estados Térmicos

Sin un mecanismo de histéresis, un sistema que cambia de estado al cruzar un único valor de temperatura (por ejemplo, pasar de LOW a MEDIUM exactamente a 50.0 °C) puede oscilar rápidamente entre dos estados si la temperatura real fluctúa de forma natural alrededor de ese punto (49.9 °C, 50.1 °C, 49.8 °C...). Esto se traduce en que el ventilador cambiaría de velocidad constantemente, generando ruido eléctrico, desgaste mecánico innecesario y un volumen excesivo de eventos de transmisión UART.

La histéresis introduce dos umbrales distintos para subir y para bajar de estado: el sistema sube a MEDIUM al llegar a 50 °C, pero solo regresa a LOW si la temperatura cae por debajo de 48 °C (un margen de 2 °C). Esto crea una "zona muerta" que evita oscilaciones, a cambio de una pequeña demora en la respuesta del sistema — un compromiso (*trade-off*) estándar en sistemas de control.

**Regla de dirección del margen:** la histéresis se aplica de forma asimétrica respecto a la dirección del cambio, no a la magnitud del margen entre umbrales (que sí es simétrica entre los tres umbrales, ver más abajo). El margen se resta del umbral configurado únicamente al evaluar si el sistema debe **bajar** de estado; al evaluar si debe **subir** de estado, se compara directamente contra el valor exacto del umbral, sin margen. Es decir, subir de estado es inmediato al cruzar el umbral; bajar de estado requiere cruzar el umbral menos el margen. Esta asimetría es intencional: prioriza una reacción rápida ante un aumento de temperatura (incrementar la velocidad del ventilador sin demora) y un criterio más conservador para reducirla (evitar enfriar de más prematuramente).

**Parámetros definidos:**

* **Margen de histéresis:** 2 °C, fijo en código (constante, no editable por el usuario).
* **Aplicación:** mismo margen numérico en los tres umbrales (`threshold_low`, `threshold_medium`, `threshold_high`).

**Justificación de los valores fijos:** se evaluó dejar la histéresis configurable por el usuario (igual que los umbrales principales), pero se descartó porque la histéresis es un mecanismo de *estabilidad del lazo de control*, no un parámetro que dependa del proceso externo que se esté monitoreando (a diferencia de los umbrales de temperatura, que sí varían según la aplicación). Del mismo modo, se evaluó asignar un margen distinto a cada umbral, pero se optó por un valor común: una histéresis diferenciada por umbral solo se justificaría con evidencia empírica de comportamiento térmico distinto cerca de cada punto de transición, evidencia que no existe para un calentador simulado sin historial de operación real. La opción de margen común es, además, más simple de implementar y de justificar en la sustentación del proyecto.

**Advertencia sobre calibración manual:** la histéresis reduce la probabilidad de oscilación visual entre estados, pero no la elimina por completo en todos los casos. Si un umbral se configura en un punto que coincide casi exactamente con el equilibrio térmico real del proceso (por ejemplo, donde el ciclo de trabajo resultante del ventilador mantiene la temperatura oscilando justo alrededor de ese valor), el sistema puede seguir alternando de forma visible entre dos estados pese al margen de histéresis. **El sistema no se autocalibra para corregir esta situación.** La única solución es que el usuario ajuste manualmente el umbral en cuestión (mediante el teclado, Sección 7.2) a un punto más alejado del equilibrio térmico observado, guiándose por el valor numérico de temperatura mostrado en la pantalla OLED o por la tendencia histórica visible en el dashboard del ESP32 — nunca asumiendo que el sistema corregirá la configuración por su cuenta.

## 4.3. Diagnóstico de Fallas: Separación entre Código Visual y Causa Interna

El código visual del Registro 1 (Q2-Q6, Sección 2.3) tiene capacidad limitada de representación: solo existen 5 estados térmicos posibles a nivel de hardware de señalización dedicado a la barra progresiva. Sin embargo, el sistema requiere distinguir, **a nivel interno**, entre las dos causas de CRITICAL descritas en la Sección 4.1, ya que ambas exigen respuestas de recuperación distintas.

Esta distinción se resuelve sin necesidad de una estructura de datos adicional, reutilizando el campo `error_log_flags` ya definido en `TelemetryState` (Sección 3.1): el bit 0 (NTC/ADC) indica específicamente que la causa de CRITICAL es la falla de sensor, mientras que la ausencia de dicho bit, combinada con `current_threshold_code == THERMAL_STATE_CRITICAL`, indica sobre-temperatura sostenida. Esta distinción se refleja visualmente sin ambigüedad: sobre-temperatura sostenida se señaliza mediante el par Q5/Q6 del **Registro 1**; falla de sensor se señaliza mediante el par Q6/Q7 del **Registro 2**, con la barra de umbrales del Registro 1 completamente apagada en este último caso (Sección 2.3). Esto permite distinguir la causa incluso en ausencia de pantalla operativa, sin necesidad de leer ambos registros en conjunto.

### Recuperación según la causa

| Causa de CRITICAL                  | Mecanismo de recuperación                                                                                                                                                                          |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Sobre-temperatura sostenida          | *Pendiente de definición — ver Sección 9.*                                                                                                                                                            |
| Falla de sensor NTC                  | **No se recupera en software.** El sistema deja de alimentar deliberadamente el Watchdog Timer de Zephyr (`wdt_feed()`) al detectar la falla, provocando un **reinicio físico completo** del STM32 tras el vencimiento del temporizador del Watchdog (ver Sección 4.4). Este reinicio se repite hasta un máximo de 3 intentos consecutivos, tras lo cual el sistema escala a un estado de **Alarma Permanente** que ya no reintenta el reinicio automático (ver Sección 4.5). |

## 4.4. Watchdog Timer como Mecanismo General de Seguridad

La recuperación ante falla de sensor se implementa mediante el **Watchdog Timer (WDT)** nativo de Zephyr OS, en lugar de un reinicio lógico de la máquina de estados en software. Un reinicio físico completo borra todo el contenido de RAM y reinicia la ejecución desde el flujo `BOOT` (Sección 9), lo cual ofrece una garantía de seguridad más robusta que una simple reinicialización de variables: el sistema vuelve a un estado completamente conocido y verificado, en lugar de asumir que el estado en RAM no estaba corrupto.

**Mecanismo de disparo:** el Watchdog funciona como una **red de seguridad general del sistema**, no como un mecanismo exclusivo para la falla del NTC. En condiciones normales, el Power & Status Manager alimenta el Watchdog periódicamente (`wdt_feed()`) como parte de su ciclo habitual de ejecución, y se suscribe a `EVENT_ERROR_DETECTED` (Sección 3.2) para reaccionar ante una falla apenas se publica, en lugar de tener que consultarla por *polling*. Si la falla corresponde a `sensor_fault` (bit 0 de `error_log_flags`), el sistema **deja de alimentar el Watchdog**, lo que provoca el reinicio automático tras el vencimiento del temporizador configurado. Esta misma red de seguridad protege adicionalmente contra otros escenarios no anticipados explícitamente en el diseño, como el bloqueo indefinido de un hilo crítico, sin necesidad de lógica adicional específica para cada caso.

**Persistencia de datos a través del reinicio:** dado que el reinicio del Watchdog borra el contenido de RAM, es fundamental que los contadores de diagnóstico (`error_count[4]`, `system_boot_count`, `ntc_consecutive_failures`) y los umbrales de configuración (`threshold_low/medium/high`) residan en la memoria NVS (Sección 8), de forma que el sistema no pierda su historial de fallas ni la configuración del usuario al reiniciarse por esta causa.

## 4.5. Ruptura del Ciclo de Reinicio Infinito: Alarma Permanente

El mecanismo descrito en la Sección 4.4 presenta un riesgo de diseño no trivial: si la falla del sensor NTC es de origen físico permanente (por ejemplo, un cable desconectado), cada reinicio del Watchdog vuelve a ejecutar `BOOT`, vuelve a detectar la misma falla, y vuelve a disparar un nuevo reinicio — entrando en un **bucle infinito de reinicios** en el cual el ventilador nunca alcanza un estado de control estable y el proceso térmico jamás recibe refrigeración efectiva durante un periodo prolongado.

Para romper este ciclo, se incorpora un contador de reinicios consecutivos atribuibles específicamente a la falla de NTC:

```c
// Campo en TelemetryState, persiste en NVS (ver struct completo en Sección 3.1):
uint8_t ntc_consecutive_failures;
```

**Flujo de control:**

1. Durante `BOOT` → `Evaluar_Hardware`, si se detecta falla de NTC, se incrementa `ntc_consecutive_failures` en NVS.
2. Si `ntc_consecutive_failures < 3`: el sistema sigue el camino normal ya definido en la Sección 4.4 (deja de alimentar el Watchdog, reinicio físico).
3. Si `ntc_consecutive_failures == 3`: el sistema **no** vuelve a intentar el reinicio vía Watchdog. En su lugar, entra en **Alarma Permanente**:
   * Fija el ventilador a 100% PWM de forma sostenida (failsafe final).
   * Establece `SystemState.system_enabled = false` (Sección 3.1).
   * Enciende el LED de Alarma Permanente (Q4 del Registro 2, Sección 2.3), de forma fija.
   * **Inhabilita por completo el pulsador físico**: mientras `system_enabled == false`, ninguna pulsación del botón (corta, larga, o de cualquier duración) tiene efecto alguno. La única forma de salir de este estado es la intervención humana directa descrita más abajo, nunca una interacción por software.
   * Reinicia `ntc_consecutive_failures` a 0 en NVS, de modo que un futuro arranque limpio del sistema (tras intervención humana real sobre el hardware) vuelva a evaluar la condición desde cero, sin arrastrar memoria de incidentes anteriores.

**Salida del estado de Alarma Permanente:** este estado solo se resuelve mediante intervención humana directa sobre el hardware (revisión y reconexión física del sensor NTC) seguida de un nuevo ciclo de encendido del sistema (corte y restauración de alimentación). No existe ningún mecanismo de software, botón o secuencia de teclado que permita salir de este estado, ya que el propio botón queda inhabilitado mientras la alarma está activa (ver arriba); permitir una salida puramente lógica contradiría el propósito mismo de la alarma, que es garantizar que un humano verificó la causa raíz antes de reanudar la operación.

**Limpieza del contador tras recuperación espontánea:** si el sistema arranca y el NTC opera con normalidad de forma sostenida, se asume que cualquier incidente previo quedó resuelto, y `ntc_consecutive_failures` se reinicia a 0 — de lo contrario, fallas aisladas y distantes en el tiempo (por ejemplo, un falso contacto ocasional una vez cada varios meses) terminarían acumulándose hacia el tope de 3 sin relación real entre sí, llevando el sistema a Alarma Permanente de forma injustificada. El Temperature Manager mantiene un temporizador (`k_timer`) que, tras un periodo continuo de lecturas válidas del NTC sin ninguna falla detectada, dispara la limpieza del contador en NVS.

> **Nota sobre el valor del temporizador:** para efectos de demostración del proyecto, este periodo se configura en **15 segundos**, un valor deliberadamente corto para que el comportamiento sea observable durante una presentación en vivo. En una implementación de producción real, este valor debería ser considerablemente mayor — del orden de 5 a 10 minutos de operación estable — para evitar que una breve inestabilidad momentánea del sensor limpie el contador antes de que el problema subyacente tenga oportunidad de manifestarse de nuevo.

## 4.6. Secuencia de Autorización de la Planta de Calefacción Externa

El sistema de refrigeración debe estar operativo y verificado **antes** de que la planta de calefacción externa (el proceso térmico que se está controlando) inicie su funcionamiento — de lo contrario, existiría una ventana de tiempo en la que el calor se genera sin ningún mecanismo de control activo, lo cual sería inseguro en un sistema de control térmico real.

Esta secuencia de seguridad se controla mediante la línea *keep-alive*, un **GPIO de salida exclusivo** reservado para esta función (Sección 2.1) — esta línea es la única señal eléctrica real que comunica al sistema de control con la planta externa (o su simulación); ningún otro componente del sistema sustituye esta función.

1. Durante `BOOT`, el sistema ejecuta su rutina de verificación de hardware (`Evaluar_Hardware`).
2. Únicamente si dicha verificación es exitosa (o el sistema entra en un Modo Degradado que preserva el lazo térmico crítico, Sección 7.4), el STM32 activa el GPIO *keep-alive*. Este es el instante exacto en el que el sistema "autoriza" a la planta de calefacción externa a comenzar a operar.
3. El LED correspondiente del Registro 2 (Q5, Sección 2.3) se enciende de forma fija. Este LED es puramente un **espejo visual** del estado del GPIO — permite a un observador de la maqueta ver el estado de autorización sin necesidad de medir el pin con un instrumento — pero no sustituye al GPIO como la señal de control real: la responsabilidad de habilitar o no la planta externa recae exclusivamente en el GPIO.
4. Si en cualquier momento posterior el sistema desactiva el GPIO *keep-alive* (por ejemplo, durante la secuencia de apagado seguro descrita en el flujo `SHUTDOWN`), el LED Q5 se apaga en conjunto, representando que la autorización fue revocada y, en una implementación real, la planta de calefacción externa debería detenerse en consecuencia.

> **Nota de alcance:** el comportamiento específico del *keep-alive* durante un escalado a CRITICAL por sobre-temperatura sostenida (es decir, si la autorización debe revocarse también en ese escenario, no solo durante el apagado seguro) permanece como punto pendiente de definición, ya identificado en la Sección 9.

---

# 5. Definición Detallada de Tareas (Threads)

El sistema distribuye sus responsabilidades en 7 hilos concurrentes que cooperan bajo el planificador predictivo por prioridad de Zephyr OS. Se optó por mantener estos 7 hilos, sin subdividirlos en hilos más granulares por sub-responsabilidad, ya que los pasos internos de cada tarea son secuenciales y dependientes entre sí (por ejemplo, no tiene sentido clasificar el umbral térmico en paralelo con la lectura del ADC del mismo ciclo, porque el segundo paso necesita el resultado del primero). Dividir estos pasos en hilos adicionales no ganaría paralelismo real, y sí añadiría más uso de RAM (un stack por hilo) y más mecanismos de paso de datos entre hilos sin beneficio claro. En su lugar, cada tarea se documenta a continuación con el detalle de su secuencia interna de pasos, para eliminar cualquier ambigüedad sobre su alcance.

| Tarea (Thread)               | Prioridad en Zephyr | Periféricos Asignados          | Responsabilidad Principal                                                                                                                                                       |
| ------------------------------ | ---------------------- | --------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Power & Status Manager          | 0 (Máxima)              | GPIO (Input con ISR)               | Monitoreo del pulsador único, discriminación de tiempo de pulsación (corta/larga/zona muerta), alimentación del Watchdog, y gestión de la máquina de estados global (FSM).         |
| Temperature Manager             | 1                       | ADC (NTC 3470)                     | Muestreo periódico del sensor, filtrado digital (promedio móvil), clasificación de umbrales térmicos con histéresis, detección de falla de sensor. Actualiza `ControlState`.       |
| Cooling Manager                 | 2                       | TIM en modo PWM (Ventilador)       | Ajuste dinámico del ciclo de trabajo del ventilador utilizando la información contenida en `ControlState`. Actúa como ejecutor del Failsafe (100% PWM) ante errores críticos.        |
| Heater Simulation Task          | 3                       | GPIO (Output Keep-alive)            | Gestión de la línea de habilitación del hardware externo, incluyendo la secuencia de autorización descrita en la Sección 4.6. Genera la lógica de la carga térmica simulada en paralelo. |
| Unified UI & Keypad Task        | 5                       | I2C (1x OLED), GPIO (Matriz)        | Escaneo no bloqueante del teclado con validación indirecta de coherencia, navegación y edición de parámetros de configuración, actualización de la pantalla OLED única. Actualiza `ConfigState`.  |
| LED Representation Manager      | 6                       | SPI (2x SN74HC595N)                 | Generación del patrón de heartbeat y de la barra de umbrales térmicos en el Registro 1; actualización de banderas de falla, alarma permanente, causa de CRITICAL y autorización de planta externa en el Registro 2.   |
| ESP32 Comm Manager              | 10 (Baja)               | UART                                | Construcción y transmisión asíncrona de paquetes de telemetría, configuración y errores; gestión del *heartbeat* y detección de desconexión del ESP32.                              |

## 5.1. Justificación de las Prioridades

La prioridad asignada a cada tarea refleja la gravedad de la consecuencia si dicha tarea se retrasa en ejecutarse, no la importancia conceptual de su función. Este criterio — consecuencia de la tardanza, no relevancia percibida — es el que ordena la tabla anterior:

* **Power & Status Manager (0):** es el único hilo cuya tardanza puede provocar un reinicio físico no solicitado del sistema completo, al no alimentar el Watchdog a tiempo (Sección 4.4). Ninguna otra tarea tiene una consecuencia de esa magnitud si se retrasa, por lo que ocupa la prioridad máxima.
* **Temperature Manager (1):** primer eslabón del lazo de control térmico (Sección 1). Cooling Manager depende de que esta tarea complete su ciclo (lectura, filtrado, clasificación) para tener datos frescos que consumir; debe ejecutarse con mayor prioridad que su consumidor.
* **Cooling Manager (2):** consume la salida de Temperature Manager para ajustar la velocidad real del ventilador. Su tardanza retrasa la respuesta física del sistema de refrigeración, por lo que se mantiene en el segundo nivel más alto, justo después de su fuente de datos.
* **Heater Simulation Task (3):** gestiona el GPIO *keep-alive*, la señal de autorización real hacia la planta de calefacción externa (Sección 4.6). Una tardanza aquí tiene una consecuencia de seguridad real — la planta externa esperando una señal de autorización o de corte que no llega a tiempo —, a diferencia de un simple indicador visual. Por esta razón se ubica por encima de las tareas de interfaz humana y de representación visual, no por debajo de ellas.
* **Unified UI & Keypad Task (5):** gestiona interacción humana directa (teclado, pantalla). Un retraso es perceptible por el usuario (por ejemplo, una tecla que tarda en reflejarse), pero no compromete la seguridad del lazo de control ni de la planta externa.
* **LED Representation Manager (6):** actualiza indicadores puramente visuales. Un retraso de algunos milisegundos en el parpadeo de un LED no tiene ninguna consecuencia funcional sobre el sistema, por lo que se ubica en el nivel más bajo entre las tareas de prioridad media.
* **ESP32 Comm Manager (10):** su tardanza solo afecta la frescura de los datos mostrados en el dashboard remoto, sin ninguna repercusión sobre el lazo físico de control ni sobre la seguridad. Es, por tanto, la prioridad más baja del sistema.

## 5.2. Secuencia de Operación: Ciclo Típico

Un ciclo típico de operación normal comienza cuando Temperature Manager produce una nueva lectura válida y emite `EVENT_TEMPERATURE_UPDATED`; este evento se consume en paralelo por tres tareas independientes (Cooling Manager, LED Representation Manager y ESP32 Comm Manager), cada una reaccionando según su propia condición de disparo, sin que ninguna espere a las demás. En paralelo a este flujo, Power & Status Manager y Heater Simulation Task operan en sus propios ciclos sostenidos, independientes del evento de temperatura: el primero alimentando el Watchdog de forma continua, el segundo manteniendo activa la línea *keep-alive*. Cuando ocurre una falla (por ejemplo, falla de sensor NTC), el flujo normal se interrumpe: `EVENT_ERROR_DETECTED` se publica y es consumido tanto por Power & Status Manager (que deja de alimentar el Watchdog) como por LED Representation Manager (que reconfigura ambos registros de LEDs según la Sección 2.3), convergiendo en el reinicio físico del sistema descrito en la Sección 4.4.

## 5.3. Secuencia Interna de Pasos por Tarea

**Power & Status Manager:**
1. Permanece suspendido (`k_event_wait`) hasta recibir una interrupción del pulsador físico o `EVENT_ERROR_DETECTED`.
2. Ante una pulsación, mide su duración y la clasifica según las franjas de la Sección 7.1 (corta, zona muerta, larga).
3. Según la clasificación y el modo actual (`ConfigState.hmi_mode`), alterna el modo de configuración o emite `EVENT_SHUTDOWN_REQUESTED`.
4. Ante `EVENT_ERROR_DETECTED` con causa `sensor_fault`, deja de alimentar el Watchdog (Sección 4.4).
5. En su ciclo habitual (mientras no haya falla de NTC), alimenta el Watchdog (`wdt_feed()`).

**Temperature Manager:**
1. Espera el despertar del temporizador de muestreo periódico.
2. Ejecuta la lectura cruda del ADC sobre el canal del NTC.
3. Aplica la ecuación de Steinhart-Hart para convertir la lectura a grados Celsius.
4. Aplica el filtro de promedio móvil (Sección 9, tamaño de ventana pendiente de definición).
5. Compara el valor filtrado contra los umbrales configurados, aplicando la regla de histéresis asimétrica (Sección 4.2).
6. Actualiza `current_temperature` y `current_threshold_code` en `ControlState` bajo `control_mutex`.
7. Emite `EVENT_TEMPERATURE_UPDATED`.
8. Si la lectura está fuera de rango físico válido, activa el bit de `sensor_fault` en `error_log_flags` y emite `EVENT_ERROR_DETECTED`; en caso contrario, gestiona el temporizador de estabilidad de 15 s para la limpieza de `ntc_consecutive_failures` (Sección 4.5).

**Cooling Manager:**
1. Permanece suspendido hasta recibir `EVENT_TEMPERATURE_UPDATED`.
2. Lee `current_threshold_code` de `ControlState` bajo `control_mutex`.
3. Determina el ciclo de trabajo correspondiente según la tabla fija de PWM por estado (Sección 3.1).
4. Ajusta el registro de comparación del Timer en modo PWM.
5. Actualiza `fan_pwm_duty_cycle` en `ControlState`.

**Unified UI & Keypad Task:**
1. Refresca la pantalla OLED según el modo activo (`ConfigState.hmi_mode`).
2. Escanea el teclado matricial de forma no bloqueante.
3. Si está en modo configuración, procesa la tecla detectada según el mapeo de la Sección 7.2.
4. Al confirmar cambios (`*`), ejecuta la validación de la Sección 7.4 y, si es exitosa, actualiza `ConfigState` y emite `EVENT_CONFIG_UPDATED`.
5. Gestiona los temporizadores de auto-verificación de OLED/Teclado (Sección 3.1) y el de espera de confirmación de teclado al entrar a configuración (Sección 7.2).

**LED Representation Manager:**
1. Mantiene el ciclo de alternancia del heartbeat (Q0/Q1 del Registro 1) de forma continua.
2. Ante `EVENT_TEMPERATURE_UPDATED`, actualiza la barra progresiva del Registro 1 (Q2-Q6) según la lógica de la Sección 2.3, incluyendo el apagado completo de dicha barra si la causa de CRITICAL es falla de sensor.
3. Ante `EVENT_ERROR_DETECTED` o `EVENT_ESP32_DISCONNECTED`, actualiza las banderas correspondientes del Registro 2 (Q0-Q3, Q6-Q7).
4. Refleja el estado de `system_enabled` y del GPIO *keep-alive* en Q4 y Q5 del Registro 2, respectivamente.
5. Transmite el contenido actualizado de ambos registros vía SPI en cascada.

**Heater Simulation Task:**
1. Durante `BOOT`, espera la confirmación de verificación de hardware exitosa.
2. Activa el GPIO *keep-alive*, autorizando a la planta externa (Sección 4.6).
3. Mantiene la lógica de simulación de la carga térmica en paralelo, mientras el sistema permanece en operación normal.
4. Ante `EVENT_SHUTDOWN_REQUESTED`, desactiva el GPIO *keep-alive* como parte de la secuencia de apagado.

**ESP32 Comm Manager:**
1. Gestiona tres temporizadores independientes: telemetría (10 s o por delta de temperatura), heartbeat (5 s) y reenvío pendiente tras reconexión.
2. Ante el cumplimiento de cualquiera de las condiciones de disparo (Sección 6.2), construye el paquete correspondiente (SOH, número de secuencia, tipo, longitud, payload, CRC16) y lo transmite por UART.
3. En un segundo hilo de recepción, escucha la respuesta de heartbeat del ESP32 y actualiza `esp32_connected` en `TransmissionState`.
4. Si el heartbeat no se confirma dentro del tiempo límite, emite `EVENT_ESP32_DISCONNECTED`.

## 5.4. Asignación de Periféricos y Recursos por Tarea

La siguiente tabla consolida la configuración de cada periférico o recurso del STM32 utilizado en el sistema, indicando qué tarea lo gestiona y con qué propósito específico. Se distingue entre **periféricos de hardware** (bloques físicos del microcontrolador) y **temporizadores de software** (`k_timer` de Zephyr, que no consumen un periférico de hardware dedicado, sino que se apoyan en el *system tick* del kernel).

| Periférico / Recurso | Tipo | Tarea(s) que lo usan | Configuración / Propósito |
| ----------------------- | ------ | ------------------------ | ------------------------------ |
| ADC1, canal del NTC | Hardware | Temperature Manager | 12 bits de resolución, lectura del NTC vía divisor resistivo |
| TIM en modo PWM | Hardware | Cooling Manager | Genera la señal PWM hacia el driver del ventilador |
| USART1 (TX) | Hardware | ESP32 Comm Manager | Transmisión de paquetes hacia el ESP32 |
| USART1 (RX) | Hardware | ESP32 Comm Manager (segundo hilo) | Recepción de heartbeat del ESP32 |
| SPI1 | Hardware | LED Representation Manager | Control en cascada de los 2x SN74HC595N |
| I2C1 | Hardware | Unified UI & Keypad Task | Comunicación con la pantalla OLED única |
| GPIO (EXTI) | Hardware | Power & Status Manager | Interrupción externa del pulsador físico |
| GPIO (salida digital, exclusivo) | Hardware | Heater Simulation Task | Línea *keep-alive* hacia la planta externa (Sección 4.6) |
| GPIO (matriz 4x4) | Hardware | Unified UI & Keypad Task | Filas (salida) y columnas (entrada) del teclado |
| IWDG (Watchdog) | Hardware | Power & Status Manager | Reinicio físico ante falla de sensor no atendida (Sección 4.4) |
| `k_timer` (heartbeat TX) | Software | ESP32 Comm Manager | Dispara cada 5 s el envío de `MSG_TYPE_HEARTBEAT` |
| `k_timer` (telemetría) | Software | ESP32 Comm Manager | Dispara cada 10 s el envío de `MSG_TYPE_TELEMETRY` si no hubo envío anticipado por delta de temperatura |
| `k_timer` (auto-verificación) | Software | Unified UI & Keypad Task | Reintenta verificación de OLED/Teclado cada 5 s, hasta 12 intentos (Sección 3.1) |
| `k_timer` (confirmación de teclado) | Software | Unified UI & Keypad Task | Espera 20 s dentro de modo configuración antes de asumir ausencia de teclado (Sección 7.2) |
| `k_timer` (Watchdog feed) | Software (invoca el periférico IWDG) | Power & Status Manager | Alimenta el Watchdog periódicamente |
| `k_timer` (estabilidad NTC) | Software | Temperature Manager | Cuenta 15 s de lecturas válidas continuas antes de limpiar `ntc_consecutive_failures` (Sección 4.5) |

---

# 6. Protocolo de Comunicación UART (STM32 ↔ ESP32)

## 6.1. Estructura General del Paquete Serial

```
[SOH] [SEQ_NUM] [TIPO_MSG] [LONGITUD] [PAYLOAD...] [CRC16]
```

* **SOH (Start of Header):** byte fijo de sincronización que marca el inicio de un paquete válido.
* **SEQ_NUM:** número de secuencia incremental (1 byte, con reinicio cíclico al superar 255), que permite al receptor detectar paquetes perdidos o duplicados — una capacidad que un checksum por sí solo no provee, ya que este último valida únicamente la integridad del contenido, no el orden ni la completitud de la serie de paquetes recibidos.
* **TIPO_MSG:** identifica la categoría de datos transportados (ver Sección 6.2).
* **LONGITUD:** tamaño en bytes del campo PAYLOAD, para permitir payloads de tamaño variable según el tipo de mensaje.
* **PAYLOAD:** copia atómica de los campos relevantes de la estructura de estado correspondiente, tomada bajo el mutex respectivo y liberada inmediatamente después de copiar (minimizando el tiempo de bloqueo).
* **CRC16:** código de verificación de errores de 16 bits sobre el paquete completo. "CRC" (*Cyclic Redundancy Check*) es un tipo específico de checksum, basado en división polinómica sobre los bits del mensaje, con mejor capacidad de detección de errores que una suma simple; CRC16 es la variante que produce un resultado de 16 bits. Se prefiere sobre un CRC8 simple porque reduce significativamente la probabilidad de que una corrupción de múltiples bits pase desapercibida (aproximadamente 1 en 65536 paquetes corruptos no detectados con CRC16, frente a aproximadamente 1 en 256 con CRC8). Dado que el enlace UART no cuenta con reintentos automáticos a nivel de protocolo, esta mayor capacidad de detección aporta valor real de robustez sin un costo de implementación significativamente mayor.

## 6.2. Tipos de Mensaje y Política de Envío

A diferencia de un esquema homogéneo donde todo el estado se transmite con la misma cadencia, el sistema diferencia la frecuencia de envío según la naturaleza de cada dominio de datos, evitando tráfico innecesario en el bus serial:

```c
typedef enum {
    MSG_TYPE_TELEMETRY = 0,   // current_temperature, current_threshold_code, fan_pwm_duty_cycle
    MSG_TYPE_CONFIG    = 1,   // threshold_low/medium/high
    MSG_TYPE_ERROR     = 2,   // error_log_flags, error_count[4], ntc_consecutive_failures
    MSG_TYPE_HEARTBEAT = 3,   // verificación de enlace activo (ver Sección 6.4)
} MessageType;
```

| Tipo de mensaje      | Condición de disparo                                                                                                   |
| ----------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| `MSG_TYPE_TELEMETRY`    | Lo que ocurra primero entre: (a) transcurrido un temporizador de **10 segundos** desde el último envío, o (b) la temperatura actual difiere en **2 °C o más** respecto a `last_sent_temperature`. |
| `MSG_TYPE_CONFIG`       | Al establecer la conexión inicial con el ESP32, y cada vez que se confirma un cambio de umbrales (`EVENT_CONFIG_UPDATED`). |
| `MSG_TYPE_ERROR`        | Cada vez que cambia el contenido de `error_log_flags` o se incrementa algún `error_count[i]` (incluyendo `ntc_consecutive_failures`).      |
| `MSG_TYPE_HEARTBEAT`    | Periódicamente, cada **5 segundos**, independiente de los demás tipos de mensaje.                                          |

**Nota sobre el envío de diagnóstico en tiempo real:** se evaluó la alternativa de enviar el estado de diagnóstico (`MSG_TYPE_ERROR`) únicamente una vez, al establecer la conexión inicial con el ESP32, en lugar de cada vez que cambia. Se descartó esta alternativa porque reduciría significativamente la utilidad del dashboard remoto: el ESP32 dejaría de enterarse de fallas nuevas que ocurran durante la operación normal del sistema, hasta el siguiente reinicio. Se mantiene, por tanto, el envío en tiempo real cada vez que `error_log_flags` o algún contador de fallas cambia.

**Nota sobre parámetros de telemetría fijos:** el intervalo de 10 segundos y el delta de 2 °C se definen como constantes fijas en el código fuente para la presente versión del proyecto. Su conversión en parámetros configurables por el usuario (almacenados en `ConfigState` y editables mediante el teclado, igual que los umbrales térmicos) queda anotada como mejora futura en la Sección 9.

## 6.3. Reenvío de Datos Pendientes tras Reconexión

Los mensajes de tipo `CONFIG` y `ERROR` no se reintentan automáticamente con cadencia fija (a diferencia de la telemetría). Si un cambio de configuración o de estado de error ocurre mientras `TransmissionState.esp32_connected == false`, el sistema activa la bandera correspondiente (`config_resend_pending` o `error_resend_pending`). En cuanto el mecanismo de *heartbeat* (Sección 6.4) detecta que el ESP32 volvió a responder, el ESP32 Comm Manager reenvía el último estado conocido de dichos dominios antes de reanudar el ciclo normal de telemetría periódica, garantizando que el dashboard remoto no quede con información desactualizada tras una reconexión.

## 6.4. Mecanismo de Heartbeat y Detección de Desconexión

Se evaluaron dos esquemas de confirmación de enlace: (a) un acuse de recibo (ACK) individual por cada paquete enviado, o (b) un mensaje de *heartbeat* independiente, enviado a intervalo fijo, exclusivamente para verificar que el receptor sigue activo. Se optó por el esquema de **heartbeat separado**, al ser el patrón estándar en sistemas embebidos para esta finalidad: desacopla la pregunta "¿el receptor está vivo?" de la pregunta "¿este paquete específico llegó?", evitando la sobrecarga de esperar confirmación individual para cada uno de los tres tipos de datos transmitidos.

El STM32 envía un paquete `MSG_TYPE_HEARTBEAT` cada 5 segundos por el hilo principal de transmisión UART, y utiliza un **segundo hilo de recepción UART** dedicado a escuchar la respuesta del ESP32. Si no se recibe confirmación dentro del tiempo límite establecido, `TransmissionState.esp32_connected` se actualiza a `false` y se emite `EVENT_ESP32_DISCONNECTED`.

> **Pendiente de definición (ver Sección 9):** el procesamiento detallado de este evento en el lado del ESP32, así como el comportamiento de su propio modo degradado, se desarrollará en una iteración posterior de este documento.

---

# 7. Lógica de Control, Interfaz Humano-Máquina (HMI) y Navegación

## 7.1. Discriminación del Pulsador Físico por Tiempo de Pulsación

El sistema cuenta con un único pulsador físico que cumple dos funciones distintas según el tiempo que permanezca presionado, gestionado por el Power & Status Manager mediante ISR y un temporizador asociado:

| Duración de la pulsación | Acción                                                                                                         |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| Corta (< 1 segundo)         | Entrar o salir del modo de configuración (alterna `ConfigState.hmi_mode`).                                          |
| Zona muerta (1 - 3 segundos)| Sin efecto. Existe deliberadamente para evitar que una pulsación "casi larga" se interprete de forma ambigua.        |
| Larga (> 3 segundos)        | Si el sistema está en modo Monitoreo: inicia la secuencia de apagado seguro (`EVENT_SHUTDOWN_REQUESTED`). Si el sistema está en modo Configuración: cancela la edición en curso y retorna a modo Monitoreo, **sin** apagar el sistema. |

> **Excepción — Alarma Permanente:** mientras `SystemState.system_enabled == false` (Sección 4.5), el pulsador físico queda completamente inhabilitado, sin importar la duración de la pulsación. La única vía de salida de este estado es una intervención humana directa sobre el hardware, seguida de un nuevo ciclo de encendido del sistema.

## 7.2. Mapeo del Teclado Matricial 4x4 en Modo Configuración

```
[1] [2] [3] [A]
[4] [5] [6] [B]
[7] [8] [9] [C]
[*] [0] [#] [D]
```

| Tecla   | Función                                              |
| --------- | ------------------------------------------------------- |
| **A**     | Avanzar al siguiente campo editable (low → medium → high → low...) |
| **B**     | Retroceder al campo editable anterior                    |
| **C**     | Incrementar el valor del campo activo (+1 °C)             |
| **D**     | Decrementar el valor del campo activo (−1 °C)              |
| **\***    | Confirmar cambios (*commit*) — dispara `EVENT_CONFIG_UPDATED` |
| **#**     | Cancelar edición — descarta cambios, retorna a monitoreo    |
| 0-9 (resto)| Reservadas, sin función asignada en esta versión           |

**Justificación del esquema de incremento/decremento:** se descartó la opción de digitar directamente un valor numérico completo (por ejemplo, "65") porque requeriría lógica adicional de buffer de entrada, manejo de corrección de dígitos y validación de formato — complejidad considerable para una interfaz basada en teclado matricial 4x4 y pantalla OLED de tamaño reducido. El esquema de incremento/decremento por pasos fijos es funcionalmente suficiente para el rango de ajuste esperado y simplifica tanto la implementación como la explicación del flujo de validación (Sección 7.4).

**Validación de la salud del teclado matricial y resolución del problema de entrada circular:** un teclado matricial pasivo no posee un mecanismo directo de "presencia" (a diferencia de un dispositivo I2C, que puede responder a una transacción de prueba); su salud solo puede inferirse de forma indirecta, observando si el patrón de lecturas del escaneo es físicamente coherente con una pulsación humana real.

Esto presenta un problema de diseño que requiere resolverse con cuidado: si la entrada al modo configuración estuviera condicionada a que el teclado pase primero una verificación, y dicha verificación solo pudiera ejecutarse *dentro* del propio modo configuración (que es el único lugar donde se espera que el usuario presione teclas), el sistema quedaría en un círculo sin salida — una vez que el teclado se marca en falla, nunca habría una vía para que vuelva a confirmarse como operativo. Para evitarlo, **la entrada al modo configuración (pulsación corta del botón) nunca depende del estado de la bandera de falla del teclado** — solo depende de que la pantalla OLED esté operativa (Sección 7.4). Una vez dentro del modo configuración:

* Si el teclado no tenía ninguna falla registrada, opera con normalidad de inmediato.
* Si el teclado estaba marcado en falla, la **primera pulsación de cualquier tecla** dentro de este modo confirma su presencia y limpia la bandera correspondiente, permitiendo la edición normal a partir de ese momento.
* Si transcurren **20 segundos** dentro de modo configuración sin ninguna pulsación detectada, el sistema asume ausencia de teclado, muestra brevemente el mensaje "Teclado no detectado" en pantalla, y retorna automáticamente a modo Monitoreo (sin que esto represente una falla nueva si el teclado simplemente no fue tocado a tiempo por el usuario, distinguible del caso de falla real solo por la ausencia total y sostenida de actividad coherente en escaneos posteriores).

Este temporizador de 20 segundos es independiente del ciclo general de auto-verificación de 5 segundos descrito en la Sección 3.1 (que aplica fuera del modo configuración, mientras el sistema opera en monitoreo normal).

## 7.3. Sistema de Pantalla Única OLED

El diseño contempla **una única pantalla OLED**, comunicada por I2C, en lugar de dos pantallas independientes. Esta decisión simplifica el bus I2C (un solo dispositivo, sin necesidad de arbitraje ni multiplexación temporal entre dos pantallas) y se sostiene en que gran parte de la información que antes requería una segunda pantalla dedicada al monitoreo pasivo ya queda cubierta por los dos registros de desplazamiento de LEDs (Sección 2.3): el estado térmico activo, las banderas de falla por módulo, el estado de alarma permanente y la autorización de la planta de calefacción externa son todos visibles sin necesidad de una pantalla.

La única OLED alterna entre dos modos de despliegue, determinados por `ConfigState.hmi_mode` (Sección 3.1), sin mostrar ambos contenidos simultáneamente:

* **Modo Monitoreo** (`hmi_mode == false`): muestra el valor numérico exacto de la temperatura actual (información que los LEDs no pueden representar con precisión), mensajes generales del sistema, y, en estado CRITICAL, el texto descriptivo de la causa específica (sobre-temperatura sostenida o falla de sensor NTC; ver Sección 4.3). Durante el apagado, despliega el mensaje de cierre. Adicionalmente, si el bit de falla del **Teclado matricial** o del **ESP32/UART** está activo en `error_log_flags` (Sección 3.1), se muestra un mensaje de texto breve indicando el módulo afectado (por ejemplo, "Teclado no disponible" o "ESP32 desconectado"), de forma independiente y simultánea a la representación que ya ofrecen los LEDs del Registro 2 — esto da al usuario una vía adicional de diagnóstico textual sin depender de interpretar el patrón de parpadeo de los LEDs. Las fallas de NTC y de la propia pantalla OLED no se incluyen en este mensaje: la primera porque ya cuenta con representación completa al escalar a CRITICAL (Sección 4.3), y la segunda porque la pantalla no puede reportar su propia falla.
* **Modo Configuración** (`hmi_mode == true`): reemplaza por completo la visualización de monitoreo y renderiza el menú interactivo de edición de umbrales descrito en la Sección 7.2.

Los procesos de control térmico en segundo plano (adquisición de temperatura, cálculo de umbrales, control PWM) continúan ejecutándose de forma concurrente e ininterrumpida en ambos modos, sin alteración alguna — la pantalla es puramente una interfaz de visualización/edición y no forma parte del camino crítico de control (Sección 7.4).

## 7.4. Regla de Validación de Umbrales y Bloqueo de Configuración sin Pantalla

### Validación de umbrales

Toda modificación de umbrales confirmada mediante la tecla `*` debe satisfacer la siguiente expresión lógica de forma obligatoria:

```
threshold_low < threshold_medium < threshold_high
```

Si la validación falla, el sistema rechaza los cambios introducidos, muestra un mensaje de "Configuración Inválida" en la pantalla, y mantiene los límites previamente vigentes intactos en memoria.

### Bloqueo de acceso al modo configuración sin pantalla operativa

Dado que la edición de umbrales sin referencia visual del valor que se está modificando podría fijar parámetros incoherentes a ciegas, comprometiendo el comportamiento del lazo de control, se establece la siguiente regla de seguridad: **el modo configuración solo puede activarse si la pantalla OLED está operativa.** Si el bit correspondiente de `error_log_flags` (Sección 3.1) indica que la OLED está en falla, la pulsación corta del botón (que normalmente alterna `hmi_mode`, Sección 7.1) **no tiene efecto alguno** — el sistema permanece en modo Monitoreo de forma indefinida, sin importar cuántas veces se presione el botón, hasta que la pantalla vuelva a responder (mecanismo de auto-verificación periódica, Sección 3.1).

Esta restricción se mantiene **sin excepción ni mecanismo de respaldo alternativo**: no se contempla usar el ESP32 como pantalla de configuración sustituta, ya que ello distorsionaría el propósito original del ESP32 como interfaz de monitoreo remoto puramente receptiva (Sección 2.2), introduciendo además una vía de control del lazo térmico desde un dispositivo que, por diseño, no debe tener esa potestad (Sección 2.2).

### Operación en Modo Degradado

El sistema deberá continuar operando ante:

* Falla de la pantalla OLED (con la restricción adicional de bloqueo de configuración descrita arriba).
* Falla del teclado matricial.
* Falla del enlace UART.
* Falla o reinicio del ESP32.

La pérdida de cualquiera de estos subsistemas no deberá afectar:

* La adquisición de temperatura.
* El cálculo de umbrales.
* El control PWM.
* La activación de mecanismos de seguridad.

La interfaz gráfica y el teclado se consideran subsistemas auxiliares y no forman parte del camino crítico de control.

---

# 8. Persistencia de Datos en Memoria NVS

No todos los campos del estado global ameritan persistencia en memoria flash (NVS): la flash posee un número finito de ciclos de escritura, por lo que persistir valores que cambian con alta frecuencia (como la temperatura instantánea) generaría un desgaste innecesario sin beneficio real, dado que dichos valores se reconstruyen en segundos tras cualquier reinicio.

| Dato                                  | ¿Persiste en NVS? | Justificación                                                                                   |
| ---------------------------------------- | -------------------- | --------------------------------------------------------------------------------------------------- |
| `threshold_low/medium/high`              | Sí                    | Cambian con baja frecuencia (solo por intervención del usuario) y deben sobrevivir a reinicios.       |
| `system_boot_count`                      | Sí                    | Es por definición un contador de reinicios; carece de sentido si no persiste entre ellos.              |
| `error_count[4]`                         | Sí                    | Debe sobrevivir incluso a un reinicio físico provocado por el propio Watchdog (Sección 4.4).            |
| `ntc_consecutive_failures`               | Sí                    | Debe sobrevivir entre reinicios físicos consecutivos para poder contarlos y romper el bucle infinito (Sección 4.5). Se reinicia a 0 únicamente al alcanzar el tope de 3 y entrar en Alarma Permanente. |
| `current_temperature`, `fan_pwm_duty_cycle` | No                 | Cambian constantemente; se reconstruyen mediante una nueva lectura del ADC en los primeros segundos tras el arranque. |
| `error_log_flags` (estado activo)        | No                    | Es un estado transitorio; cada módulo se re-verifica desde cero durante la rutina `Evaluar_Hardware` del flujo BOOT, y posteriormente mediante el ciclo de auto-verificación periódica (Sección 3.1). |
| `system_enabled`                         | No                    | Vive en RAM; cada nuevo ciclo de vida del sistema reevalúa la condición de Alarma Permanente desde cero (Sección 3.1 y 4.5). |
| Margen de histéresis                     | No aplica             | Es una constante de compilación, no una variable en memoria editable.                                  |

---

# 9. Puntos Pendientes de Definición y Mejoras Futuras

## 9.1. Puntos Pendientes de Definición

Los siguientes aspectos fueron identificados durante el proceso de especificación pero **deliberadamente pospuestos** para una iteración posterior de este documento, sin que ello afecte la coherencia de las decisiones ya tomadas:

1. **Comportamiento del ESP32 ante su propia desconexión:** procesamiento detallado de `EVENT_ESP32_DISCONNECTED` desde la perspectiva del firmware del ESP32, y definición de su modo degradado (qué muestra el dashboard web cuando deja de recibir datos del STM32).
2. **Tamaño de ventana del filtro de promedio móvil:** el Temperature Manager aplica un filtrado digital de tipo promedio móvil sobre las lecturas del ADC (Sección 5), pero el tamaño de la ventana (número de muestras N) queda pendiente de definición en la fase de implementación, una vez se disponga de mediciones reales de ruido del sensor.
3. **Acción sobre la línea *keep-alive* en CRITICAL por sobre-temperatura:** queda por definir si, en este escenario específico (a diferencia de la falla de sensor, que sí provoca reinicio vía Watchdog), el sistema debe cortar la línea de habilitación del proceso térmico externo como medida adicional de seguridad (Sección 4.6), y cuál es el tiempo exacto de tolerancia en estado HIGH antes de escalar a CRITICAL por esta causa.

## 9.2. Mejoras Futuras (Fuera del Alcance de la Presente Versión)

Los siguientes aspectos se identificaron como mejoras técnicamente válidas durante el proceso de diseño, pero se optó por no desarrollarlos en esta versión del proyecto, ya sea por exceder el alcance razonable de un proyecto universitario o por requerir hardware adicional no contemplado:

1. **RTC (Real-Time Clock) para marcas de tiempo absolutas:** la implementación actual usa `k_uptime_get()` de Zephyr (tiempo relativo desde el arranque, Sección 3.1), suficiente para la lógica de temporización del sistema. Un RTC real con fecha/hora de calendario permitiría registrar el instante exacto de ocurrencia de eventos y fallas, lo cual sería valioso en una implementación de producción.
2. **Sensor de retroalimentación del ventilador:** actualmente el sistema solo emite la señal PWM hacia el ventilador, sin ningún mecanismo (tacómetro, sensor de efecto Hall) que confirme el giro real del motor. Incorporar dicha retroalimentación permitiría agregar una bandera de falla de ventilador al esquema de diagnóstico (Sección 3.1).
3. **Curva de respuesta no lineal del ventilador:** la versión actual utiliza una tabla de 4 valores fijos de PWM por estado térmico (Sección 3.1). Una función de respuesta continua o por tramos (por ejemplo, logarítmica), con parámetros configurables de duración del salto entre valores, ofrecería un control más suave, a cambio de mayor complejidad de implementación y de edición por teclado.
4. **Parámetros de telemetría configurables:** el intervalo del temporizador de envío (10 s) y el delta de temperatura que dispara un envío anticipado (2 °C), actualmente constantes fijas (Sección 6.2), podrían convertirse en valores editables por el usuario mediante el mismo esquema de teclado utilizado para los umbrales térmicos.

---

# 10. Arquitectura Modular del Firmware

Más allá de las decisiones de diseño lógico (estructuras de estado, máquina de estados, protocolo de comunicación), el código fuente del proyecto se organiza en archivos separados por módulo, en lugar de concentrarse en un único archivo. Esta decisión de organización forma parte de la guía de implementación del proyecto y se justifica en varios beneficios concretos:

* **Aislamiento de responsabilidades:** cada uno de los 7 hilos (Sección 5) y cada una de las 5 estructuras de estado (Sección 3.1) ya tiene una responsabilidad y un dominio de datos bien definidos a nivel de diseño; reflejar esa misma separación en archivos `.c`/`.h` independientes mantiene la coherencia entre el diseño documentado y el código real.
* **Facilita las pruebas unitarias por módulo:** por ejemplo, la lógica de histéresis del Temperature Manager puede probarse de forma aislada, sin necesitar que el resto del sistema esté corriendo.
* **Reduce el acoplamiento entre módulos:** si cada módulo expone únicamente las funciones necesarias mediante su propio archivo de cabecera, se dificulta que, por ejemplo, el código del teclado termine accediendo directamente a variables internas del control térmico sin pasar por el mutex correspondiente — la propia organización de archivos ayuda a hacer cumplir la disciplina de mutex definida en la Sección 3.1.
* **Compilación incremental más rápida:** al modificar un solo módulo durante el desarrollo, el sistema de build de Zephyr (basado en CMake) solo necesita recompilar ese archivo y volver a enlazar, no todo el proyecto.

## 10.1. Estructura de Directorios Propuesta

```
src/
├── main.c                      // Inicialización del kernel, creación de hilos
├── state/
│   ├── control_state.c/.h      // ControlState + control_mutex
│   ├── transmission_state.c/.h // TransmissionState + transmission_mutex
│   ├── config_state.c/.h       // ConfigState + config_mutex
│   ├── telemetry_state.c/.h    // TelemetryState + telemetry_mutex
│   └── system_state.c/.h       // SystemState + sys_mutex
├── tasks/
│   ├── power_status_manager.c/.h
│   ├── temperature_manager.c/.h
│   ├── cooling_manager.c/.h
│   ├── ui_keypad_task.c/.h
│   ├── led_representation_manager.c/.h
│   ├── heater_simulation_task.c/.h
│   └── esp32_comm_manager.c/.h
├── drivers/
│   ├── ntc_sensor.c/.h          // Lectura ADC + ecuación Steinhart-Hart
│   ├── shift_register.c/.h      // Driver genérico SPI para los 2x SN74HC595N
│   └── matrix_keypad.c/.h       // Escaneo de teclado
└── protocol/
    └── uart_packet.c/.h         // Construcción/parseo de paquetes, CRC16
```

Cada carpeta agrupa archivos de naturaleza equivalente: `state/` contiene exclusivamente las estructuras de datos compartidas y sus mutex (sin lógica de negocio); `tasks/` contiene la implementación de cada uno de los 7 hilos descritos en la Sección 5; `drivers/` contiene el código de bajo nivel específico de cada componente de hardware, reutilizable desde cualquier tarea que lo necesite; `protocol/` contiene la lógica de construcción y parseo de paquetes UART, independiente de qué tarea la invoque.

---

*Documento en construcción colaborativa. Última actualización: consolidación de Bloques 1-4, ronda de correcciones de consistencia, y segunda ronda de ajustes (patrones de LED, ruptura de ciclo de auto-verificación, bloqueo circular del teclado, arquitectura modular del firmware, tabla de periféricos).*
