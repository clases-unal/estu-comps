# Guía de Estructura del Proyecto

## Objetivo

Esta estructura organiza el proyecto de forma que la documentación, el hardware y el software evolucionen de manera independiente pero trazable.

El objetivo principal es evitar que la especificación técnica, la arquitectura y la implementación queden mezcladas en un único documento o repositorio desorganizado.

---

# Directorio raíz

```text
project-root/
```

Contiene únicamente información general del proyecto y las carpetas principales.

Debe ser posible comprender la organización global del proyecto leyendo únicamente este nivel.

---

# README.md

## Propósito técnico

Punto de entrada principal del proyecto.

Debe permitir a cualquier desarrollador comprender:

* Objetivo del sistema.
* Hardware utilizado.
* Tecnologías empleadas.
* Estado actual del proyecto.
* Ubicación de la documentación.

## Propósito no técnico

Permite que cualquier persona (profesor, tutor, cliente o colaborador) comprenda rápidamente qué se está desarrollando.

---

# docs/

## Propósito técnico

Almacena toda la documentación de ingeniería.

Ningún código fuente debe almacenarse aquí.

## Propósito no técnico

Constituye la memoria técnica del proyecto.

Permite comprender decisiones y requisitos incluso años después.

---

# 01-system-specification.md

## Responde a la pregunta:

¿Qué debe hacer el sistema?

## Debe incluir

* Objetivo del sistema.
* Alcance.
* Requisitos funcionales.
* Requisitos no funcionales.
* Restricciones.
* Hardware empleado.
* Modos de operación.
* Alarmas y seguridad.
* Casos de uso.

## No debe incluir

* Código.
* APIs.
* Detalles internos de implementación.

---

# 02-firmware-architecture.md

## Responde a la pregunta:

¿Cómo está organizado el firmware?

## Debe incluir

* Arquitectura general.
* Hilos de Zephyr.
* Timers.
* Mutexes.
* Eventos.
* Comunicación UART.
* Watchdog.
* Organización modular.

## No debe incluir

* Requisitos funcionales.
* Resultados de pruebas.

---

# 03-state-machines.md

## Responde a la pregunta:

¿Cómo se comporta cada subsistema?

## Debe incluir

Una FSM independiente para:

* Arranque.
* Gestión térmica.
* Gestión de alarmas.
* Comunicación ESP32.
* Interfaz de usuario.
* Recuperación ante errores.

Cada FSM debe definir:

* Estados.
* Eventos.
* Transiciones.
* Acciones.

---

# 04-design-decisions.md

## Responde a la pregunta:

¿Por qué se tomó cada decisión?

## Formato recomendado

DEC-001

Título:
Uso de UART entre STM32 y ESP32.

Alternativas:

* UART
* SPI

Decisión:
UART

Justificación:
...

Consecuencias:
...

---

# 05-validation-plan.md

## Responde a la pregunta:

¿Cómo se verificará que el sistema funciona?

## Debe incluir

* Casos de prueba.
* Procedimientos.
* Resultados esperados.
* Resultados obtenidos.
* Criterios de aceptación.

---

# docs/diagrams/

## Propósito técnico

Almacenar todos los diagramas fuente.

Ejemplos:

* Arquitectura.
* FSM.
* Hardware.
* Flujo de datos.
* Secuencias.

## Recomendación

Mantener también los archivos editables originales.

---

# programa/

## Propósito técnico

Contiene todo el software desarrollado.

No debe contener documentación extensa.

La documentación debe permanecer en docs/.

---

# programa/stm32/

## Contenido

Firmware principal basado en Zephyr.

Ejemplos:

* Aplicación principal.
* Drivers.
* Módulos.
* Configuración Zephyr.
* Device Tree.
* Kconfig.

## Responsabilidad

Control principal del sistema.

---

# programa/esp32/

## Contenido

Firmware auxiliar del ESP32.

Ejemplos:

* Portal web.
* Modo AP.
* Comunicación UART.
* Configuración WiFi.

## Responsabilidad

Interfaz remota y conectividad.

---

# programa/shared/

## Contenido

Elementos comunes entre STM32 y ESP32.

Ejemplos:

* Protocolos UART.
* Definiciones compartidas.
* Especificaciones de mensajes.
* Documentación de interfaces.

---

# hardware/

## Propósito técnico

Almacenar información relacionada con el hardware.

## Debe incluir

* Esquemáticos.
* Diagramas de conexión.
* PCB.
* Lista de materiales.
* Simulaciones.

---

# references/

## Propósito técnico

Bibliografía y documentación externa.

## Debe incluir

* Datasheets.
* Manuales.
* Normativas.
* Notas de aplicación.
* Documentación de Zephyr.

## Regla importante

Los archivos aquí son referencias externas.

No deben modificarse.

---

# Principio general

Cada carpeta debe responder una única pregunta:

* docs → ¿Qué es y cómo funciona?
* programa → ¿Cómo está implementado?
* hardware → ¿Cómo está construido?
* references → ¿En qué documentación se apoya?

Si una información no responde a la pregunta de la carpeta donde se pretende almacenar, probablemente pertenece a otra ubicación.
