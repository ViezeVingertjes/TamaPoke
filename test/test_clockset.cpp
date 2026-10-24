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

TEST(clockset, sin_reloj_valido_no_da_la_vuelta_al_ano_2106) {
  // no valid time yet (now = 0): the asked hh:mm of day 0, never an underflow
  CHECK_EQ((int)clockEpoch(0, 18, 0), 18 * 3600);
  CHECK_EQ((int)clockEpoch(100, 0, 5), 5 * 60);
}

TEST(clockset, la_base_prefiere_el_rtc_luego_lo_guardado_y_luego_una_fecha_razonable) {
  CHECK_EQ((int)clockBase(5000000, 4999990), 5000000);   // the RTC is fresher than the cached time
  CHECK_EQ((int)clockBase(0, 4999990), 4999990);
  CHECK_EQ((int)clockBase(0, 0), 1767225600);            // no time at all: 2026-01-01, as at boot
  // so that any hh:mm set then is a real date, never day 0 (which means "no clock")
  CHECK(clockEpoch(clockBase(0, 0), 0, 0) / 86400 > 0);
}
