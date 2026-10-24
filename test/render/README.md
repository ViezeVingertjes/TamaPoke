# Renderer del host

Compila `TamaPoke.ino` tal cual contra el codigo grafico real de la libreria
*GFX Library for Arduino* (`Arduino_GFX.cpp`, `Arduino_Canvas.cpp`) en el PC, lleva
el juego a cada pantalla (huevo, escena de dia/noche, galeria, ficha, teclado,
reloj, minijuegos, despedida) y guarda una captura de cada una. Sirve para ver y
comparar la interfaz sin placa.

    make shots                # build/shots/*.ppm, 466x466, ingles

- Circulos, rectangulos redondeados y texto salen del mismo codigo que corre en la placa.
- Los sprites salen de `tools/sdcard` (o de `SD_DIR=otra/carpeta`), como si fuera la SD.
- Hora, bateria, `random()` y el estado del bicho son fijos y se reinician en cada
  pantalla: dos ejecuciones dan la misma imagen, sea cual sea el orden.
- Hace falta tener instalada la libreria *GFX Library for Arduino*
  (`GFX_FONT_FILE=... make shots` si no esta en `~/Arduino/libraries`).
- El panel real es redondo: lo que la captura pinta fuera del circulo de 466 px no se
  ve en la placa.
- Las referencias de `golden/en` y `golden/sq480` salen de g++ en Linux con
  GFX Library for Arduino 1.6.9; `make check` las compara y
  `make golden` las regenera (solo si el cambio de interfaz es intencionado).
