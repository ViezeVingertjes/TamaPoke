# Host renderer

Builds `TamaPoke.ino` as is against the real graphics code of the
*GFX Library for Arduino* library (`Arduino_GFX.cpp`, `Arduino_Canvas.cpp`) on the PC, takes
the game to every screen (egg, day/night scene, gallery, card, keyboard,
clock, minigames, farewell) and saves a screenshot of each. It lets you see and
compare the interface without a board.

    make shots                # build/shots/*.ppm, 466x466, English

- Circles, rounded rectangles and text come out of the same code that runs on the board.
- Sprites come from `tools/sdcard` (or from `SD_DIR=other/folder`), as if it were the SD.
- Time, battery, `random()` and the critter state are fixed and reset on every
  screen: two runs give the same image, whatever the order.
- The *GFX Library for Arduino* library must be installed
  (`GFX_FONT_FILE=... make shots` if it is not in `~/Arduino/libraries`).
- The real panel is round: what the screenshot paints outside the 466 px circle is not
  visible on the board.
- The references in `golden/en` and `golden/sq480` come from g++ on Linux with
  GFX Library for Arduino 1.6.9; `make check` compares against them and
  `make golden` regenerates them (only if the interface change is intentional).
