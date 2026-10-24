// Species chirps (species_chirp.cpp).
#include "framework.h"
#include "../species_chirp.h"
#include "../dex.h"
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
      CHECK(n.freq + n.slide >= 140 && n.freq + n.slide <= 2600);  // and it ends in range too
      CHECK(n.ms >= 50 && n.ms <= 120);
      CHECK(n.wave <= CHIRP_TRI);
      total += n.ms;
    }
    CHECK(total <= 340);                   // short enough for a tap
  }
}

// the notes as text (fields, not bytes: the struct has padding)
static std::string key(const SpeciesChirp &c) {
  std::string k;
  for (const ChirpNote &n : c.notes)
    k += std::to_string(n.freq) + "," + std::to_string(n.ms) + "," + std::to_string(n.slide) + "," +
         std::to_string(n.wave) + ";";
  return k;
}

TEST(chirp, cada_especie_suena_distinta_y_siempre_igual) {
  std::set<std::string> seen;
  for (int d = 1; d <= DEX_COUNT; d++) {
    SpeciesChirp a, b;
    speciesChirp(d, &a);
    speciesChirp(d, &b);
    CHECK(key(a) == key(b));
    seen.insert(key(a));
  }
  CHECK_EQ((int)seen.size(), DEX_COUNT);
}
