// Medidas de la interfaz (layout.h): identidad en 466 y escalado sensato en el resto.
#include "framework.h"
#include "../layout.h"

TEST(layout, en_466_las_medidas_no_cambian) {
  for (int n = -600; n <= 2000; n++) CHECK_EQ(uiScale(n, UI_DESIGN), n);
}

TEST(layout, los_enteros_nunca_bajan_de_uno) {
  for (int panel : { 120, 240, 360, 466, 480 })
    for (int n = -3; n <= 20; n++) {
      CHECK(uiScaleFit(n, panel) >= 1);
    }
}

TEST(layout, la_escala_de_sprites_nunca_pasa_de_la_caja) {
  // SC redondea hacia abajo: un sprite de n x 32 px nunca supera lo que mide su caja escalada
  for (int panel : { 240, 300, 360, 410, 466, 480, 700, 932 })
    for (int n = 1; n <= 8; n++) {
      int s = uiScaleFit(n, panel);
      if (n * panel >= UI_DESIGN) CHECK(s * UI_DESIGN <= n * panel);  // sin el minimo de 1
      else CHECK_EQ(s, 1);
    }
  CHECK_EQ(uiScaleFit(5, 240), 2);   // 2,57 -> 2 (no 3)
  CHECK_EQ(uiScaleFit(5, 480), 5);   // 5,15 -> 5
  CHECK_EQ(uiScaleFit(2, 240), 1);   // 1,03 -> 1
  CHECK_EQ(uiScaleFit(1, 240), 1);   // 0,51 -> sube a 1
  CHECK_EQ(uiScaleFit(3, 932), 6);   // 6,0 -> 6
}

TEST(layout, texto_y_sprites_en_los_tamanos_de_diseno_y_de_la_2_16) {
  for (int n = 1; n <= 8; n++) {
    CHECK_EQ(uiScaleFit(n, UI_DESIGN), n);  // identidad en 466
    CHECK_EQ(uiScaleFit(n, 480), n);        // y en 480 para todos los factores en uso
  }
  CHECK_EQ(uiScaleFit(2, 410), 1);          // texto de tamano 2 en un panel de 410: baja a 1 (cabe en su caja)
  CHECK_EQ(uiScaleFit(3, 410), 2);
  CHECK_EQ(uiScaleFit(7, 360), 5);
}
TEST(layout, valores_calculados_a_mano) {
  CHECK_EQ(uiScale(1, 240), 1);      // 0,52 -> 1
  CHECK_EQ(uiScale(3, 240), 2);      // 1,55 -> 2
  CHECK_EQ(uiScale(100, 240), 52);   // 51,50 -> 52 (mitad hacia arriba)
  CHECK_EQ(uiScale(233, 480), 240);  // 240,05 -> 240
  CHECK_EQ(uiScale(-100, 240), -52);
  CHECK_EQ(uiScaleFit(4, 240), 2);   // 2,06 -> 2
  CHECK_EQ(uiScaleFit(1, 120), 1);   // 0,26 -> sube a 1
  CHECK_EQ(uiScaleFit(10, 480), 10); // 10,3 -> 10
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
  CHECK_EQ(TS(3), 3);
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
