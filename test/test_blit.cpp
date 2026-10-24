// forRuns (blit.h): pintar por tramos deja exactamente los mismos pixeles que
// pintar pixel a pixel.
#include "framework.h"
#include "../blit.h"
#include <vector>

// lienzo de juguete: dibuja un fillRect s x s por pixel, o uno por tramo
static std::vector<int> paint(const std::vector<uint8_t> &img, int w, int h, int s, bool runs) {
  int W = w * s, H = h * s;
  std::vector<int> out(W * H, -1);
  auto rect = [&](int x, int y, int rw, int rh, int col) {
    for (int j = 0; j < rh; j++)
      for (int i = 0; i < rw; i++) out[(y + j) * W + x + i] = col;
  };
  for (int r = 0; r < h; r++) {
    const uint8_t *row = &img[r * w];
    if (runs) {
      forRuns(w, [&](int c) { return indexedKey(row[c], false); },
              [&](int c0, int len, int k) { rect(c0 * s, r * s, len * s, s, k); });
    } else {
      for (int c = 0; c < w; c++)
        if (row[c] != 0xFF) rect(c * s, r * s, s, s, row[c]);
    }
  }
  return out;
}

TEST(blit, los_tramos_pintan_los_mismos_pixeles) {
  uint32_t seed = 12345;
  for (int iter = 0; iter < 200; iter++) {
    int w = 1 + iter % 40, h = 1 + (iter / 3) % 30, s = 1 + iter % 5;
    std::vector<uint8_t> img(w * h);
    for (auto &p : img) {
      seed = seed * 1664525u + 1013904223u;
      uint32_t v = (seed >> 16) % 6;
      p = v == 0 ? 0xFF : v == 1 ? 7 : (uint8_t)(v % 3);  // pocos colores: provoca tramos largos
    }
    CHECK(paint(img, w, h, s, true) == paint(img, w, h, s, false));
  }
}

TEST(blit, filas_vacia_llena_y_de_un_pixel) {
  std::vector<uint8_t> empty(8, 0xFF), full(8, 3), one = {5};
  CHECK(paint(empty, 8, 1, 2, true) == paint(empty, 8, 1, 2, false));
  CHECK(paint(full, 8, 1, 2, true) == paint(full, 8, 1, 2, false));
  CHECK(paint(one, 1, 1, 3, true) == paint(one, 1, 1, 3, false));
}

TEST(blit, clave_indexada_transparente_y_silueta) {
  CHECK_EQ(indexedKey(0xFF, false), -1);
  CHECK_EQ(indexedKey(0xFF, true), -1);
  CHECK_EQ(indexedKey(9, false), 9);
  CHECK_EQ(indexedKey(9, true), 0);  // la silueta une colores distintos en un tramo
  CHECK_EQ(indexedKey(0, true), 0);
}

// la silueta junta colores distintos en un tramo y pinta todo en tinta
TEST(blit, la_silueta_une_colores_distintos) {
  std::vector<uint8_t> img = { 1, 2, 3, 0xFF, 4, 4, 0xFF, 7 };
  auto naive = [&](bool sil) {
    std::vector<int> out(img.size(), -1);
    for (size_t c = 0; c < img.size(); c++) if (img[c] != 0xFF) out[c] = sil ? 99 : img[c];
    return out;
  };
  auto runs = [&](bool sil, int *count) {
    std::vector<int> out(img.size(), -1);
    *count = 0;
    forRuns((int)img.size(), [&](int c) { return indexedKey(img[c], sil); },
            [&](int c0, int len, int k) {
              (*count)++;
              for (int i = 0; i < len; i++) out[c0 + i] = sil ? 99 : k;
            });
    return out;
  };
  int nSil, nCol;
  CHECK(runs(true, &nSil) == naive(true));
  CHECK(runs(false, &nCol) == naive(false));
  CHECK_EQ(nSil, 3);   // [1,2,3] [4,4] [7]: la silueta junta 1,2,3 en un solo tramo
  CHECK_EQ(nCol, 5);   // a color: [1] [2] [3] [4,4] [7]
}

// mapas de caracteres: '.' transparente, cualquier otro caracter (tambien >= 0x80) viaja como clave sin signo
TEST(blit, claves_de_mapa_de_caracteres) {
  CHECK_EQ(charKey('.', false), -1);
  CHECK_EQ(charKey('.', true), -1);
  CHECK_EQ(charKey('k', false), (int)'k');
  CHECK_EQ(charKey('k', true), 0);
  CHECK_EQ(charKey((char)0xE9, false), 0xE9);            // sin signo: no sale negativo
  CHECK_EQ((char)charKey((char)0xE9, false), (char)0xE9);  // y vuelve igual con (char)k
}

TEST(blit, filas_vacias_y_de_un_solo_color_o_todo_distinto) {
  int calls = 0;
  forRuns(0, [&](int) { return 1; }, [&](int, int, int) { calls++; });
  CHECK_EQ(calls, 0);                                      // ancho 0
  std::vector<uint8_t> alt = { 1, 2, 1, 2, 1, 2 };         // peor caso: todos los tramos de 1 pixel
  forRuns(6, [&](int c) { return (int)alt[c]; }, [&](int c0, int len, int k) { CHECK_EQ(len, 1); CHECK_EQ(k, (int)alt[c0]); calls++; });
  CHECK_EQ(calls, 6);
}
