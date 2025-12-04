Diario de Proyecto: Máquina de Turing "Nicowo"

Integrantes: Nicolas Galleguillos
Curso: Fundamentos de Cs. de Comp.
Fecha de Inicio: 05/09/25

05/09 - 10/09: Conceptualización y Diseño

Estado: Planificación

Decisión de Diseño: Se optó por una arquitectura híbrida. Una cinta física de LEDs (WS2812B) para representar los datos, combinada con un cabezal móvil (Motor Nema 17) que porta un sensor de color (TCS34725) para "leer" el estado.

Representación: Usaremos lógica unaria.

Verde (1) = Dato.

Azul (0) = Vacío.

Azul Oscuro (2) = Separador de operación.

Justificación: La lógica unaria simplifica los estados de la máquina de Turing, permitiendo enfocar el esfuerzo en la integración electromecánica.

11/09 - 23/10: Selección de Componentes y Montaje Inicial

Estado: Construcción

Hardware:

Controlador: ESP32 (Para la interfaz web).

Actuador: Motor Paso a Paso Nema 17 + Driver A4988.

Sensor: TCS34725 (I2C) para alta fidelidad de color.

Avance: Se montó un riel de 30cm y se fijó la tira LED.

23/10 - 25/11: Integración Desarrollo / Pruebas 

Problema (Motor): El motor vibraba excesivamente y hacía ruido ("grrr") sin girar correctamente.

Solución: Las bobinas estaban mal paradas. Se intercambiaron los cables 1A/1B y se activó el Microstepping (1/16).

Problema (Driver): El driver A4988 no arrancaba.

Solución: Faltaba el puente físico entre los pines RESET y SLEEP. Se corrigió soldando un puente de 3v.
