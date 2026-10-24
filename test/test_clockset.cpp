// Setting the clock from hh:mm (clockset.h).
#include "framework.h"
#include "../clockset.h"

static uint32_t at(uint32_t day, int h, int m, int s = 0) { return day * 86400UL + h * 3600UL + m * 60UL + s; }

TEST(clockset, la_misma_jornada_cambia_solo_la_hora) {
  CHECK_EQ((int)clockEpoch(at(20000, 14, 7, 30), 15, 30), (int)at(20000, 15, 30));
  CHECK_EQ((int)clockEpoch(at(20000, 14, 7, 30), 9, 5), (int)at(20000, 9, 5));
}

TEST(clockset, pasada_la_medianoche_hacia_atras_vuelve_al_dia_anterior) {
  // 00:30, set 23:30: one hour earlier (the day before), not 23 hours ahead
  CHECK_EQ((int)clockEpoch(at(20000, 0, 30), 23, 30), (int)at(19999, 23, 30));
}

TEST(clockset, antes_de_la_medianoche_hacia_delante_pasa_al_dia_siguiente) {
  // 23:50, set 00:05: fifteen minutes later (the next day), not 23 hours back
  CHECK_EQ((int)clockEpoch(at(20000, 23, 50), 0, 5), (int)at(20001, 0, 5));
}

TEST(clockset, el_cambio_nunca_pasa_de_doce_horas) {
  for (int h = 0; h < 24; h++)
    for (int m = 0; m < 60; m += 15) {
      uint32_t now = at(20000, 3, 20), e = clockEpoch(now, h, m);
      int64_t d = (int64_t)e - (int64_t)now;
      CHECK(d >= -43200 && d <= 43200);
      CHECK_EQ((int)(e % 86400), h * 3600 + m * 60);   // and it lands on the asked hh:mm
    }
}
