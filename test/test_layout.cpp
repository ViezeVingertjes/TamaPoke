// Medidas de la interfaz (layout.h): identidad en 466 y escalado sensato en el resto.
#include "framework.h"
#include "../layout.h"

TEST(layout, en_466_las_medidas_no_cambian) {
  for (int n = -600; n <= 2000; n++) CHECK_EQ(uiScale(n, UI_DESIGN), n);
}

TEST(layout, los_enteros_nunca_bajan_de_uno) {
  for (int panel : { 120, 240, 360, 466, 480 })
    for (int n = -3; n <= 20; n++) CHECK(uiScaleInt(n, panel) >= 1);
}

TEST(layout, valores_calculados_a_mano) {
  CHECK_EQ(uiScale(1, 240), 1);      // 0,52 -> 1
  CHECK_EQ(uiScale(3, 240), 2);      // 1,55 -> 2
  CHECK_EQ(uiScale(100, 240), 52);   // 51,50 -> 52 (mitad hacia arriba)
  CHECK_EQ(uiScale(233, 480), 240);  // 240,05 -> 240
  CHECK_EQ(uiScale(-100, 240), -52);
  CHECK_EQ(uiScaleInt(4, 240), 2);   // 2,06 -> 2
  CHECK_EQ(uiScaleInt(1, 120), 1);   // 0,26 -> sube a 1
  CHECK_EQ(uiScaleInt(10, 480), 10); // 10,3 -> 10
}

TEST(layout, las_constantes_derivadas_valen_lo_de_siempre_en_466) {
  CHECK_EQ(UI_W, 466);
  CHECK_EQ(UI_H, 466);
  CHECK_EQ(CX, 233);
  CHECK_EQ(CY, 233);
  CHECK_EQ(UI_R, 231);
  CHECK_EQ(BALL_R, 205);
  CHECK_EQ(PANEL_Y, 312);
  CHECK_EQ(U(560), 560);
  CHECK_EQ(SC(5), 5);
}

TEST(layout, escala_proporcional_y_monotona) {
  for (int panel : { 240, 360, 410, 480, 720 }) {
    int prev = uiScale(0, panel);
    for (int n = 1; n <= 600; n++) {
      int v = uiScale(n, panel);
      CHECK(v >= prev);                                   // nunca retrocede
      double exact = (double)n * panel / UI_DESIGN;
      CHECK(v >= exact - 0.5 && v <= exact + 0.5);        // redondeo al mas cercano
      prev = v;
    }
  }
}

TEST(layout, el_redondeo_es_simetrico_para_negativos) {
  for (int panel : { 240, 480 })
    for (int n = 1; n <= 300; n++) CHECK_EQ(uiScale(-n, panel), -uiScale(n, panel));
}

TEST(layout, la_pantalla_completa_cabe_en_el_panel) {
  CHECK_EQ(uiScale(UI_DESIGN, 240), 240);
  CHECK_EQ(uiScale(UI_DESIGN, 480), 480);
}

TEST(layout, argumentos_grandes_no_desbordan) {
  // millis() de varios dias dividido entre 14, escalado a un panel de 480
  for (long n : { 100000L, 3000000L, 90000000L, 300000000L })
    CHECK_EQ(uiScale((int)n, 480), (int)((int64_t)n * 480 / UI_DESIGN + ((int64_t)n * 480 % UI_DESIGN >= UI_DESIGN / 2)));
  CHECK_EQ(uiScale(300000000, UI_DESIGN), 300000000);
}
