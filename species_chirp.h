#pragma once
#include <stdint.h>

// Per-species chirp: three short synthesized notes derived from the Pokedex number and the
// base stats (faster species chirp higher). Original synthesis, no recorded or ROM audio.
enum ChirpWave : uint8_t { CHIRP_SQUARE = 0, CHIRP_TRI };

struct ChirpNote {
  uint16_t freq;   // Hz at the start of the note
  uint16_t ms;
  int16_t slide;   // Hz added linearly over the note
  uint8_t wave;
};

struct SpeciesChirp { ChirpNote notes[3]; };

// false for a number outside 1..DEX_COUNT (nothing is played)
bool speciesChirp(int16_t dex, SpeciesChirp *out);
