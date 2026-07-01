# Errores y observaciones pendientes

Este documento recoge puntos que fueron detectados durante la revisión del firmware pero que no se corrigieron en esta pasada para no alterar el comportamiento del sistema.

## 1. Condición de carrera teórica en la inicialización

El proyecto usa K_THREAD_DEFINE() para crear los hilos de forma estática. Esto funciona bien en Zephyr, pero existe una condición de carrera teórica en la que un hilo podría intentar usar una estructura de estado antes de que main() complete sus _state_init().

- Riesgo: uso de estado compartido antes de estar completamente inicializado.
- Impacto: podría provocar comportamientos intermitentes en arranques muy tempranos.
- Estado: pendiente de evaluación y posible refactorización.

## 2. Política de failsafe del ventilador

El módulo de cooling_manager fuerza al ventilador a 100% si el NTC falla. Esta decisión es razonable desde el punto de vista de seguridad, pero no está documentada en la arquitectura del proyecto como una decisión formal.

- Riesgo: si el hardware real no desea ese comportamiento, puede resultar agresivo.
- Impacto: consumo de energía y posible sobreenfriamiento.
- Estado: pendiente de confirmar contra la especificación del sistema.

## 3. Dependencia fuerte de los pines del overlay

El firmware asume que los pines definidos en el overlay son correctos y permanentes. Si cambia el hardware o se mueve un componente a otro pin, será necesario ajustar tanto el overlay como los drivers y tareas que lo usan.

- Riesgo: cambios físicos del PCB o del prototipo pueden requerir varias modificaciones coordinadas.
- Impacto: alto si se hace hardware de forma incremental.
- Estado: pendiente de encapsular mejor la asignación de pines.

## 4. Comunicación UART y protocolo

La comunicación con el ESP32 está implementada, pero el comportamiento del protocolo en presencia de errores de trama, reintentos o tiempo de espera aún no se documenta de forma exhaustiva en el código.

- Riesgo: fragilidad ante paquetes corruptos o interrupciones de línea.
- Impacto: pérdida o retraso de telemetría.
- Estado: pendiente de ampliar la robustez del protocolo.

## 5. Error de compilación por nombres de labels de DeviceTree del keypad

El fallo de compilación se produjo porque el código del driver del teclado intentó resolver los nodos del keypad con nombres de labels que no coincidían con los que Zephyr 4.4 expone en este proyecto. En concreto, se estaba usando `DT_NODELABEL(row0)`/`DT_NODELABEL(col0)` cuando el árbol generado de DeviceTree define `row_0`, `row_1`, `col_0` y `col_1` (y sus variantes de 2 y 3).

- Causa: desalineación entre los labels usados en el código y los macros expuestos por el encabezado generado de Zephyr.
- Resolución: se corrigieron las referencias en [src/drivers/matrix_keypad.c](src/drivers/matrix_keypad.c) para usar `DT_NODELABEL(row_0)` y `DT_NODELABEL(col_0)` (junto con sus equivalentes `row_1`/`col_1`, etc.) y se recompiló el firmware hasta obtener un build exitoso.
