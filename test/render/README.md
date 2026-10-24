# Renderer del host

Compila `TamaPoke.ino` tal cual contra el codigo grafico real de la libreria
*GFX Library for Arduino* (`Arduino_GFX.cpp`, `Arduino_Canvas.cpp`) en el PC, lleva
el juego a cada pantalla (huevo, escena de dia/noche, galeria, ficha, teclado,
reloj, minijuegos, despedida) y guarda una captura de cada una. Sirve para ver y
comparar la interfaz sin placa.

    make shots                # build/shots/*.ppm, 466x466, ingles
    make shots LANG_ID=2      # otro idioma (0 ES, 1 EN, 2 FR, 3 DE, 4 IT, 5 PT, 6 JA, 7 KO)

- Circulos, rectangulos redondeados, texto y fuentes CJK (unifont japones/coreano,
  extraidos de la libreria U8g2) salen del mismo codigo que corre en la placa.
- Los sprites salen de `tools/sdcard` (o de `SD_DIR=otra/carpeta`), como si fuera la SD.
- Hora, bateria, `random()` y el estado del bicho son fijos y se reinician en cada
  pantalla: dos ejecuciones dan la misma imagen, sea cual sea el orden.
- Hace falta tener instaladas las librerias *GFX Library for Arduino* y *U8g2*
  (`GFX_FONT_FILE=... U8G2_FONTS=... make shots` si no estan en `~/Arduino/libraries`).
- El panel real es redondo: lo que la captura pinta fuera del circulo de 466 px no se
  ve en la placa.
