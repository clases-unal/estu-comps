# Plan de Validación

> Casos de prueba por módulo. Se completa progresivamente con cada módulo
> implementado. Las pruebas de integración del sistema completo (`docs/0X-...`)
> se documentan solo después de que cada módulo individual pase sus propias
> pruebas — ver razonamiento de orden en la conversación de diseño del proyecto.
>
> **Convención:** cada caso de prueba tiene un ID `TM-XXX` (Temperature Manager),
> `CM-XXX` (Cooling Manager), etc. Esto permite referenciarlos desde
> `04-design-decisions.md` si una prueba revela la necesidad de un cambio de diseño.

---

## Módulo: `temperature_manager`

### Por qué se puede probar de forma aislada ahora mismo

Los otros 5 hilos (excepto `cooling_manager`) siguen siendo stubs sin lógica —
no leen ni escriben estado real. Esto significa que cualquier comportamiento
observable en el monitor serial relacionado a temperatura proviene
exclusivamente de este módulo, sin necesidad de deshabilitar nada.

### TM-001 — Lectura en reposo (sanity check)

**Procedimiento:** flashear el firmware, abrir el monitor serial, dejar el NTC
a temperatura ambiente sin manipular.

**Resultado esperado:** líneas `temperature_manager: NTC: raw=XX.XX
filtered=XX.XX` cada ~500ms, con valores entre 18-28°C aproximadamente (rango
razonable de temperatura ambiente interior). `raw` y `filtered` deben
converger entre sí tras ~5 lecturas (la ventana del promedio móvil).

**Criterio de fallo:** si el valor es negativo, mayor a 50°C, `NaN`, o no
cambia nunca en absoluto (sospecha de lectura ADC congelada).

### TM-002 — Respuesta a estímulo de calor

**Procedimiento:** con el monitor serial abierto, sujetar el NTC entre los
dedos (o acercar una fuente de calor controlada, ej. secador a baja potencia
y distancia) durante ~15 segundos, luego soltar.

**Resultado esperado:** `filtered` sube de forma monotónica mientras se aplica
calor, y baja gradualmente al soltar (el promedio móvil de 5 muestras hace que
el descenso no sea instantáneo — es esperado, no es un error).

**Esto también valida la TOPOLOGÍA del divisor** (ver comentario en
`ntc_sensor.c`): si la temperatura baja al calentar el sensor, el divisor está
invertido respecto al supuesto — anota esto en `04-design-decisions.md` y
corrige la fórmula antes de continuar.

### TM-003 — Detección de falla del sensor (cable desconectado)

**Procedimiento:** con el sistema corriendo, desconectar físicamente el cable
del NTC (o cortocircuitarlo, según qué falla quieras simular).

**Resultado esperado:** tras ~2.5 segundos (5 lecturas fallidas consecutivas a
500ms cada una), debe aparecer `temperature_manager: NTC declarado en falla
tras 5 lecturas fallidas consecutivas`. Antes de eso, deberían verse hasta 4
líneas `WRN` de advertencia individuales.

**Criterio de fallo:** si el sistema sigue reportando temperaturas "normales"
con el sensor desconectado, el rango de validez en `ntc_sensor.c`
(`VALID_TEMP_MIN_C`/`MAX_C`) probablemente no está cubriendo el caso de
circuito abierto — revisar qué voltaje produce el ADC en ese caso real
(debería ser cercano a 0V o a VDD, dependiendo de la topología) y ajustar.

### TM-004 — Recuperación tras reconexión

**Procedimiento:** tras provocar TM-003, reconectar el NTC.

**Resultado esperado:** la siguiente lectura exitosa debe limpiar el contador
de fallas y el log debe volver a `raw=... filtered=...` normal. (Internamente,
esto también debería limpiar `ERROR_FLAG_NTC_SENSOR` en `TelemetryState` —
no es directamente visible en log todavía hasta que se imprima telemetría en
algún otro módulo, pero es la condición que `cooling_manager` usa para
desactivar el failsafe — ver CM-004).

---

## Módulo: `cooling_manager`

### CM-001 — Mapeo umbral → duty cycle, sin estímulo

**Procedimiento:** con el sistema en reposo a temperatura ambiente (por debajo
de `threshold_low` = 30°C por defecto), observar el log.

**Resultado esperado:** `cooling_manager: T=XX.XXC umbral=0 duty=0%` cada
segundo (umbral 0 = `THRESHOLD_COLD`).

### CM-002 — Transición de umbrales con calor aplicado

**Procedimiento:** aplicar calor al NTC de forma sostenida (igual que TM-002)
hasta superar los 3 umbrales configurados por defecto (30°C / 45°C / 60°C —
ver `config_state.c`, son placeholders, ajustar si ya tienes valores reales
decididos).

**Resultado esperado:** el `umbral` en el log debe subir en secuencia
0→1→2→3 a medida que la temperatura cruza cada threshold, y `duty` debe subir
en paralelo (0%→30%→60%→100%). Verificar con multímetro en el pin PA6 (o de
forma más simple, con un ventilador real conectado, percibir el cambio audible
de velocidad) que el duty cycle reportado por software corresponde a un
cambio real en la señal PWM.

**Cómo medir el PWM sin osciloscopio:** si no tienes acceso a uno, un
multímetro en modo de voltaje DC en el pin de salida del MOSFET/driver del
ventilador (no directamente en PA6, que es señal lógica 3.3V) debería mostrar
un voltaje promedio proporcional al duty cycle, asumiendo que el filtrado del
driver lo permite. Alternativa más confiable si tienes acceso: la herramienta
`logic analyzer` por software de bajo costo, o pedir prestado un osciloscopio
una sola vez para validar la frecuencia de 25kHz.

### CM-003 — Failsafe ante falla de NTC

**Procedimiento:** repetir TM-003 (desconectar el NTC) mientras se observa el
log de `cooling_manager`.

**Resultado esperado:** en cuanto `temperature_manager` marca
`ERROR_FLAG_NTC_SENSOR`, la siguiente iteración de `cooling_manager` (máximo
1 segundo después) debe mostrar `Failsafe activo (NTC en falla): ventilador
forzado a 100%`, independientemente de cuál fuera el umbral anterior.

**Esta prueba valida una decisión de diseño que no estaba cerrada en los
documentos fuente** (failsafe = velocidad máxima ante sensor no confiable).
Si tras ver esto en hardware real prefieres otra política (ej. mantener el
último duty conocido, o ir a una velocidad fija media), es el momento de
decidirlo y registrarlo como entrada nueva en `04-design-decisions.md` antes
de seguir construyendo sobre este comportamiento.

### CM-004 — Recuperación del failsafe

**Procedimiento:** reconectar el NTC tras CM-003.

**Resultado esperado:** el siguiente ciclo de `cooling_manager` debe volver a
calcular el umbral real a partir de la temperatura actual (ya no forzado a
`THRESHOLD_HIGH`/100%).

---

## Cómo aislar un módulo si en el futuro deja de ser trivial

A medida que se implementen más de los 7 hilos, ya no todos los efectos
observables en el log van a venir de un solo módulo. Cuando llegue ese punto,
hay dos formas de aislar pruebas sin reescribir código:

1. **Filtrado de log por módulo:** Zephyr permite ajustar el nivel de log por
   módulo individualmente en tiempo de compilación
   (`CONFIG_LOG_OVERRIDE_LEVEL` o macros `LOG_LEVEL` por archivo) o, si se
   activa `CONFIG_LOG_RUNTIME_FILTERING` + `CONFIG_SHELL`, también en tiempo
   de ejecución vía comandos de shell. Permite silenciar el ruido de hilos no
   relevantes a la prueba actual sin tocar su código.
2. **Comentar temporalmente el `target_sources()` del módulo no relevante**
   en `zephyr/CMakeLists.txt` — más invasivo, usar solo si el filtrado de log
   no es suficiente (ej. dos módulos compitiendo por el mismo mutex de forma
   confusa).

---

*Próxima sección a completar: pruebas de `power_status_manager` (siguiente
módulo a implementar) y, después de los 7 módulos individuales, el plan de
pruebas de integración del sistema completo.*
