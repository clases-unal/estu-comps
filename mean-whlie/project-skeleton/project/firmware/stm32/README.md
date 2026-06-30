# firmware/stm32 — Notas de arranque (PlatformIO + Zephyr, primera vez con Zephyr)

> **Cambio de tooling registrado:** este proyecto usa **PlatformIO** (extensión de
> VSCode) como entorno de desarrollo, no el flujo `west` nativo de Zephyr. La
> estructura de carpetas y el `CMakeLists.txt` están adaptados a lo que PlatformIO
> espera. Ver nota en `docs/04-design-decisions.md` (pendiente de formalizar como
> DEC-F-XXX).

## Estructura de este proyecto bajo PlatformIO

```
firmware/stm32/
├── platformio.ini          ← configuración de PlatformIO (placa, framework, monitor)
├── zephyr/
│   ├── CMakeLists.txt       ← boilerplate de Zephyr + target_sources (rutas ../src/...)
│   ├── prj.conf             ← Kconfig de la aplicación
│   └── boards/               ← overlays de devicetree por placa (<BOARD>.overlay), pendiente
└── src/                      ← TODO el código fuente (igual que en west puro)
    ├── main.c
    ├── state/
    ├── tasks/
    ├── drivers/
    └── protocol/
```

`src/` no se mueve ni se duplica — solo `CMakeLists.txt` y `prj.conf` migraron a
`zephyr/`, y las rutas dentro de `CMakeLists.txt` ahora son relativas (`../src/...`)
porque ese archivo vive un nivel más adentro.

## Diferencias importantes frente a `west` puro

1. **No hay `menuconfig`/`guiconfig` interactivo.** Todo `CONFIG_*` se escribe a
   mano en `zephyr/prj.conf`. Si una guía online te dice "corre menuconfig y
   activa X", busca el nombre exacto de la opción (`CONFIG_X=y`) en la
   documentación de Kconfig de Zephyr y agrégalo manualmente.
2. **Overlays de devicetree** van en `zephyr/boards/<BOARD>.overlay`, donde
   `<BOARD>` debe coincidir exactamente con el nombre del target oficial de
   Zephyr (`nucleo_l476rg`), no necesariamente con el nombre de placa de
   PlatformIO si llegaran a diferir.
3. **El build se dispara desde la UI de PlatformIO** (ícono de check en la barra
   inferior de VSCode) o con `pio run` desde terminal — no con `west build`.
4. **Para flashear:** botón de flecha en PlatformIO, o `pio run -t upload`. La
   Nucleo-L476RG trae ST-Link integrado, PlatformIO debería detectarlo solo.

## Qué falta antes de poder compilar contra hardware real

Este código no compila todavía contra hardware real porque depende de un
overlay de devicetree (en `zephyr/boards/`) que declara los periféricos físicos
y sus pines — pendiente del mapa de periféricos (ver
`docs/02-firmware-architecture.md` Sección 4).

Sí debería compilar en un build básico (sin overlay custom) porque `state/` y
los stubs de `tasks/` solo usan `k_mutex`, `k_thread` y `LOG_INF` — kernel puro,
sin tocar hardware todavía.

## Primer objetivo recomendado

1. Abrir esta carpeta (`firmware/stm32/`) como proyecto de PlatformIO en VSCode
   (debería detectar `platformio.ini` automáticamente).
2. Ejecutar el build (`pio run` o el botón de check) con el código actual,
   **sin overlay**, contra la placa `nucleo_l476rg` ya declarada en
   `platformio.ini`. Esto valida que `prj.conf`, `CMakeLists.txt` y los stubs
   compilan sin errores bajo PlatformIO.
3. Flashear (`pio run -t upload`) y abrir el monitor serial (`pio device
   monitor` o el ícono correspondiente) — deberías ver los mensajes `LOG_INF`
   de arranque de cada uno de los 7 hilos. Esto confirma que `K_THREAD_DEFINE`
   efectivamente los arrancó.
4. Solo después de confirmar esto, empezar a reemplazar el contenido de
   `temperature_manager.c` con lógica real, siguiendo el orden ya definido en
   `00-project-decisions-and-procedure.md` Fase 2.

Si el build falla, el mensaje de error de PlatformIO suele ser más críptico que
el de `west` nativo (mezcla salida de CMake con la suya propia) — copia el
error completo, no solo la última línea, para poder diagnosticarlo bien.
