# Consideraciones del diseño del firmware

Este documento responde a las preguntas planteadas tras revisar el proyecto en las carpetas src/ y zephyr/.

## 1. ¿Es mejor usar hilos estáticos o dinámicos?

Para este sistema, los hilos estáticos son una buena opción y, en la práctica, encajan bastante bien con la arquitectura actual.

### Por qué conviene usar hilos estáticos aquí
- El firmware tiene varios módulos con una responsabilidad clara: lectura de sensor, control de ventilador, UI, comunicación UART y manejo de pulsador.
- Cada uno de esos módulos ya tiene un ciclo de ejecución permanente y un stack definido.
- K_THREAD_DEFINE() evita la complejidad de crear y destruir hilos en tiempo de ejecución y hace el arranque más simple.
- En un proyecto embebido como este, la predictibilidad y la simplicidad suelen ser más valiosas que la flexibilidad máxima.

### Cuándo sí conviene usar hilos dinámicos
- Si más adelante necesitas crear y destruir hilos según eventos variables.
- Si el número de tareas puede cambiar en tiempo de ejecución.
- Si quieres un diseño más flexible para una futura expansión, por ejemplo, más módulos de interfaz o más protocolos de comunicación.

### Recomendación para este proyecto
- Mantener los hilos estáticos en la versión actual.
- Solo pasar a dinámicos si aparece una necesidad real de creación/destrucción dinámica o si se quiere reducir el uso de RAM de forma más agresiva.

## 2. ¿Para mi implementación esto no es problema?

Correcto. En el diseño actual no es un problema importante.

La arquitectura ya está orientada a tareas con un ciclo propio, y el uso de hilos estáticos encaja bien con el patrón de trabajo del firmware. Lo que sí merece atención es la inicialización del estado compartido: el código está bien organizado, pero si se quiere una solución más robusta, conviene revisar si alguna tarea podría intentar leer estado antes de que main() termine la inicialización.

## 3. ¿Cómo lograr un mejor comportamiento al editar pines sin romper la compilación?

La forma más segura es separar claramente tres capas:

1. Definición del hardware en el overlay
2. Mapeo lógico del driver
3. Uso del pin en la tarea o módulo

### Recomendación práctica
- No cambies los pines directamente en varios archivos a la vez.
- Haz primero el cambio en el overlay.
- Después verifica que el driver que lo consume siga esperando el mismo recurso.
- Finalmente, revisa que la tarea que usa ese recurso no asuma otro pin por defecto.

### Lo que ya se hizo bien en este proyecto
- El overlay centraliza la asignación de pines.
- El driver de teclado usa un mapeo explícito y está documentado.
- El firmware no depende de un cambio de pin oculto dentro del código de la tarea.

### Para reducir el riesgo de romper la compilación
- Mantener una tabla de mapeo en un solo lugar.
- Si se quiere avanzar más, conviene introducir macros o constantes con nombres semánticos, por ejemplo:
  - KEYPAD_ROW0_PIN
  - KEYPAD_COL0_PIN
  - FAN_PWM_PIN
  - NTC_ADC_CHANNEL
- Así, al mover un pin, no tendrás que revisar varios archivos a mano.

### Recomendación concreta
- Para este proyecto, lo más útil es mantener el overlay como fuente de verdad y que el código consuma esos recursos de forma explícita.
- Evitar cambios “in-place” de pines desde el código sin actualizar la configuración del hardware.

## 4. El ESP32 funciona como esclavo de visualización, ¿qué propones?

Si el ESP32 actúa como esclavo de visualización, lo más razonable es tratarlo como un periférico de salida y no como un elemento que deba tomar decisiones de control.

### Propuesta recomendada
- El STM32 debe seguir siendo la autoridad de control del sistema.
- El ESP32 debe recibir telemetría y, si hace falta, órdenes simples de visualización o modo de interfaz.
- El STM32 debería enviar paquetes de estado periódicos con datos como:
  - temperatura actual
  - umbral activo
  - duty cycle del ventilador
  - estado habilitado/deshabilitado
  - errores del sensor

### Ventaja de este enfoque
- Se mantiene el control centralizado en el MCU principal.
- El ESP32 se simplifica a una capa de presentación.
- Se reduce el riesgo de inconsistencias entre lo que muestra la pantalla y lo que realmente está ocurriendo en el sistema.

### Recomendación adicional
- Si el ESP32 tiene que mostrar una UI más compleja, lo ideal es que reciba un subconjunto de datos ya procesados y no la lógica completa del control térmico.

## Sobre los pines del teclado: qué se usa realmente

Para evitar confusión, el proyecto ya quedó documentado con este mapeo actual:
- Filas del teclado: PC0, PC1, PC2, PC3
- Columnas del teclado: PC4, PC5, PC6, PC7

Esto aparece tanto en el overlay como en el driver del teclado.

## Sobre el uso de double vs float

En este microcontrolador, el uso de double no es un problema en sí mismo, pero tampoco aporta una ventaja real en este caso.

### Por qué se usa double en algunos puntos del código
- En la interfaz de usuario y en los logs, se usa para mostrar valores con más comodidad y con una representación más legible.
- En la práctica, para este firmware, la precisión de float ya es suficiente.

### Por qué no conviene usar double como estándar
- El STM32L476 tiene FPU, pero el costo de operar con double sigue siendo mayor que con float en términos de ciclos y de uso de recursos.
- En un sistema embebido con consumo y rendimiento limitados, float suele ser la opción más equilibrada.

### Recomendación
- Seguir usando float para el procesamiento interno y la lógica de control.
- Usar double solo cuando se necesite una salida de texto más precisa o una conversión intermedia para mostrar datos en la UI o en logs.

### Resumen corto
- float: mejor para la lógica en tiempo real y para el firmware embebido.
- double: útil para visualización o cálculos de mayor precisión, pero no necesario para el control térmico básico del proyecto.
