// Solo los nombres de las fuentes CJK que usa el sketch; sus datos reales los
// genera fonts.py desde la libreria U8g2.
#pragma once
#include <stdint.h>
#define U8G2_WITH_UNICODE  // como la libreria real: sin esto Arduino_GFX ignora los glifos > 255
extern const uint8_t u8g2_font_unifont_t_japanese3[];
extern const uint8_t u8g2_font_unifont_t_korean2[];
