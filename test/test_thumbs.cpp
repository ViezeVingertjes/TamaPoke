// thumbs.bin entry checks (thumb_format.h).
#include "framework.h"
#include "../thumb_format.h"
#include <vector>

// blob of one entry: w x h pixels, palCount colors, every pixel = px
static std::vector<uint8_t> entry(uint8_t w, uint8_t h, uint8_t pal, uint8_t px) {
  std::vector<uint8_t> e = { w, h, pal };
  for (int i = 0; i < pal * 2; i++) e.push_back(0x11);
  for (int i = 0; i < w * h; i++) e.push_back(px);
  return e;
}

TEST(thumbs, una_miniatura_bien_formada_cabe_y_es_valida) {
  auto e = entry(4, 3, 2, 1);
  CHECK_EQ((int)thumbEntryBytes(e.data(), e.size(), 0, 0), 3 + 4 + 12);
  CHECK(thumbIndicesValid(e.data()));
  auto t = entry(4, 3, 2, 0xFF);           // all transparent
  CHECK(thumbIndicesValid(t.data()));
}

TEST(thumbs, un_indice_fuera_de_la_paleta_se_rechaza) {
  auto e = entry(4, 3, 2, 2);              // 2 colors: indices 0 and 1 only
  CHECK(!thumbIndicesValid(e.data()));
  auto f = entry(4, 3, 2, 1);
  f.back() = 7;                            // a single bad pixel at the end
  CHECK(!thumbIndicesValid(f.data()));
}

TEST(thumbs, una_miniatura_cortada_o_con_offset_malo_no_cabe) {
  auto e = entry(4, 3, 2, 1);
  CHECK_EQ((int)thumbEntryBytes(e.data(), e.size() - 1, 0, 0), 0);   // truncated pixels
  CHECK_EQ((int)thumbEntryBytes(e.data(), e.size(), (uint32_t)e.size() - 2, 0), 0);  // header does not fit
  CHECK_EQ((int)thumbEntryBytes(e.data(), e.size(), 0xFFFFFFF0u, 0), 0);  // far outside
  e[0] = 255; e[1] = 255;                  // claims 65 KB of pixels
  CHECK_EQ((int)thumbEntryBytes(e.data(), e.size(), 0, 0), 0);
}

TEST(thumbs, un_offset_dentro_de_la_tabla_o_en_un_blob_diminuto_se_rechaza) {
  auto e = entry(4, 3, 2, 1);
  CHECK(thumbEntryBytes(e.data(), e.size(), 0, 0) > 0);
  CHECK_EQ((int)thumbEntryBytes(e.data(), e.size(), 0, 6), 0);    // points into the header/table
  CHECK_EQ((int)thumbEntryBytes(e.data(), 2, 0, 0), 0);           // smaller than a header: no underflow
}
