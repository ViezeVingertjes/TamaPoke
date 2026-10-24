#pragma once
#include <stdint.h>
#include "pin_config.h"

// Geometria de la interfaz. Todo el dibujo esta maquetado para un lienzo de
// 466x466 (UI_DESIGN); en otro tamano de panel las medidas se escalan con U()/UY(),
// que en 466 devuelven exactamente su argumento. Los sprites y el texto no se
// escalan por pixel: sus factores enteros salen de TS() (texto) y SC() (sprites), mas abajo.
#define UI_DESIGN 466

#define UI_W LCD_WIDTH
#define UI_H LCD_HEIGHT

static_assert(LCD_WIDTH == LCD_HEIGHT, "la interfaz solo soporta paneles cuadrados o redondos");

// Los nombres cortos (U, UY, TS, SC, CX, CY) son macros: incluir layout.h despues de
// las librerias, nunca antes.
// longitud de diseno (px en 466) -> px del panel, redondeando al mas cercano
// (en 64 bits: long mide 32 en el ESP32 y los argumentos derivados de millis()
// pasarian de 2^31 tras unas horas encendido)
constexpr int uiScale(int n, int panel) {
  return (int)(((int64_t)n * panel + (n >= 0 ? UI_DESIGN / 2 : -UI_DESIGN / 2)) / UI_DESIGN);
}
#define U(n) uiScale((n), UI_W)   // medidas y posiciones horizontales
#define UY(n) uiScale((n), UI_H)  // medidas y posiciones verticales

// factores enteros (nunca bajan de 1), redondeados HACIA ABAJO para que lo dibujado
// no sea mas grande que la caja que se escalo con U()/UY():
//  - TS: tamano de texto (setSize/textW/centerX). Medido con las cadenas reales de los 8
//    idiomas: redondear al mas cercano desbordaba cajas en paneles de 360 y 240 px.
//  - SC: escala de sprite o pixel-art (drawMap, drawPmdAct...).
// Son iguales; se mantienen dos nombres para que el uso quede claro y un test vigile
// que no se crucen. El minimo de 1 hace que en paneles de menos de ~350 px el dibujo
// pueda ser mas grande que su caja.
constexpr int uiAtLeast1(int v) { return v < 1 ? 1 : v; }
constexpr int uiScaleFit(int n, int panel) { return uiAtLeast1((int)((int64_t)n * panel / UI_DESIGN)); }
#define TS(n) uiScaleFit((n), UI_W)
#define SC(n) uiScaleFit((n), UI_W)

// factor lineal respecto al diseno, para magnitudes que no son longitudes enteras
// (velocidades y aceleraciones, en px por paso)
#define UI_F ((float)UI_W / UI_DESIGN)

#define CX (UI_W / 2)             // centro de la pantalla
#define CY (UI_H / 2)
#define UI_R (UI_W / 2 - U(2))    // radio del fondo circular de los menus
#define BALL_R (UI_R - U(26))     // radio de la pared del minijuego de la bola
#define PANEL_Y UY(312)           // inicio de la franja inferior de botones
