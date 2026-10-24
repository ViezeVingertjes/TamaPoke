#pragma once
#include <stdint.h>
#include "pin_config.h"

// UI geometry. All drawing is laid out for a canvas of
// 466x466 (UI_DESIGN); on a different panel size the measures are scaled with U()/UY(),
// which at 466 return exactly their argument. Sprites and text are not
// scaled per pixel: their integer factors come from TS() (text) and SC() (sprites), below.
#define UI_DESIGN 466

#define UI_W LCD_WIDTH
#define UI_H LCD_HEIGHT

static_assert(LCD_WIDTH == LCD_HEIGHT, "la interfaz solo soporta paneles cuadrados o redondos");

// The short names (U, UY, TS, SC, CX, CY) are macros: include layout.h after
// the libraries, never before.
// design length (px at 466) -> panel px, rounding to the nearest
// (in 64 bits: long is 32 bits on the ESP32 and arguments derived from millis()
// would exceed 2^31 after a few hours of uptime)
constexpr int uiScale(int n, int panel) {
  return (int)(((int64_t)n * panel + (n >= 0 ? UI_DESIGN / 2 : -UI_DESIGN / 2)) / UI_DESIGN);
}
#define U(n) uiScale((n), UI_W)   // horizontal measures and positions
#define UY(n) uiScale((n), UI_H)  // vertical measures and positions

// integer factors (never below 1), rounded DOWN so that what is drawn
// is never larger than the box scaled with U()/UY():
//  - TS: text size (setSize/textW/centerX). Measured with the real strings:
//    rounding to the nearest overflowed boxes on 360 and 240 px panels.
//  - SC: sprite or pixel-art scale (drawMap, drawPmdAct...).
// Today they are equal; two names are kept so the intent is clear (and test_tools.py
// checks they are not mixed up) in case text rounding diverges from that of the
// sprites. Downside of rounding down: on 233 to 465 px panels size 2 text
// becomes 1 (the 466 UI leaves the boxes tight, and with round-to-nearest
// the text overflowed from ~440 px). The real targets are 466 and 480 px.
// The minimum of 1 means that below ~233 px the drawing can be larger than its box.
constexpr int uiAtLeast1(int v) { return v < 1 ? 1 : v; }
constexpr int uiScaleFit(int n, int panel) { return uiAtLeast1((int)((int64_t)n * panel / UI_DESIGN)); }
#define TS(n) uiScaleFit((n), UI_W)
#define SC(n) uiScaleFit((n), UI_W)

// linear factor relative to the design, for quantities that are not integer lengths
// (speeds and accelerations, in px per step)
#define UI_F ((float)UI_W / UI_DESIGN)

#define CX (UI_W / 2)             // screen center
#define CY (UI_H / 2)
#define UI_R (UI_W / 2 - U(2))    // radius of the circular menu background
#define BALL_R (UI_R - U(26))     // radius of the ball minigame wall
#define PANEL_Y UY(312)           // top of the bottom button strip
