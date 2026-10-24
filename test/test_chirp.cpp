// Species chirps (species_chirp.cpp).
#include "framework.h"
#include "../species_chirp.h"
#include "../dex.h"
#include <string.h>
#include <set>
#include <string>

TEST(chirp, numeros_fuera_de_rango_no_suenan) {
  SpeciesChirp c;
  CHECK(!speciesChirp(0, &c));
  CHECK(!speciesChirp(-1, &c));            // an egg
  CHECK(!speciesChirp(DEX_COUNT + 1, &c));
  CHECK(!speciesChirp(25, nullptr));
}

TEST(chirp, todas_las_especies_dan_notas_seguras) {
  for (int d = 1; d <= DEX_COUNT; d++) {
    SpeciesChirp c;
    CHECK(speciesChirp(d, &c));
    int total = 0;
    for (const ChirpNote &n : c.notes) {
      CHECK(n.freq >= 140 && n.freq <= 2600);
      CHECK(n.freq + n.slide >= 20);       // the glide never reaches zero
      CHECK(n.ms >= 50 && n.ms <= 120);
      CHECK(n.wave <= CHIRP_TRI);
      total += n.ms;
    }
    CHECK(total <= 340);                   // short enough for a tap
  }
}

TEST(chirp, cada_especie_suena_distinta_y_siempre_igual) {
  std::set<std::string> seen;
  for (int d = 1; d <= DEX_COUNT; d++) {
    SpeciesChirp a, b;
    speciesChirp(d, &a);
    speciesChirp(d, &b);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    seen.insert(std::string((const char *)&a, sizeof(a)));
  }
  CHECK_EQ((int)seen.size(), DEX_COUNT);
}
