#include "species_chirp.h"
#include "dex.h"

static uint16_t clampHz(int hz) { return hz < 140 ? 140 : hz > 2600 ? 2600 : (uint16_t)hz; }

// a note that starts at `freq` and glides by `slide`; the glide is shortened so it also ends in range
static ChirpNote note(int freq, int ms, int slide, uint8_t wave) {
  uint16_t start = clampHz(freq);
  return { start, (uint16_t)ms, (int16_t)(clampHz(start + slide) - start), wave };
}

bool speciesChirp(int16_t dex, SpeciesChirp *out) {
  if (!out || dex < 1 || dex > DEX_COUNT) return false;
  const DexEntry &e = DEX_TBL[dex];
  const uint8_t seed = (uint8_t)(dex * 53 + e.bAtk * 3 + e.bSpe);
  const int base = 420 + e.bSpe * 4;          // speed sets the pitch
  const int dir = (seed & 0x80) ? 1 : -1;     // rising or falling call
  const int span = 70 + (seed & 0x5F);
  out->notes[0] = note(base + (seed & 0x1F) * 5, 70 + (seed & 0x1F), dir * span, seed % 2);
  out->notes[1] = note(base + dir * (35 + ((seed >> 3) & 0x7F)), 55 + ((seed >> 5) & 0x1F), -dir * (span / 2),
                       (seed >> 3) % 2);
  // the dex number itself sets the last pitch, so neighbours with similar stats still differ
  out->notes[2] = note(220 + dex * 9, 80 + ((seed >> 2) & 0x0F), -dir * (span + 35), CHIRP_TRI);
  return true;
}
