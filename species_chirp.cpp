#include "species_chirp.h"
#include "dex.h"

static uint16_t clampHz(int hz) { return hz < 140 ? 140 : hz > 2600 ? 2600 : (uint16_t)hz; }

bool speciesChirp(int16_t dex, SpeciesChirp *out) {
  if (!out || dex < 1 || dex > DEX_COUNT) return false;
  const DexEntry &e = DEX_TBL[dex];
  const uint8_t seed = (uint8_t)(dex * 53 + e.bAtk * 3 + e.bSpe);
  const int base = 420 + e.bSpe * 4;          // speed sets the pitch
  const int dir = (seed & 0x80) ? 1 : -1;     // rising or falling call
  const int span = 70 + (seed & 0x5F);
  out->notes[0] = { clampHz(base + (seed & 0x1F) * 5), (uint16_t)(70 + (seed & 0x1F)),
                    (int16_t)(dir * span), (uint8_t)(seed % 2) };
  out->notes[1] = { clampHz(base + dir * (35 + ((seed >> 3) & 0x7F))), (uint16_t)(55 + ((seed >> 5) & 0x1F)),
                    (int16_t)(-dir * (span / 2)), (uint8_t)((seed >> 3) % 2) };
  // the dex number itself sets the last pitch, so neighbours with similar stats still differ
  out->notes[2] = { clampHz(220 + dex * 9), (uint16_t)(80 + ((seed >> 2) & 0x0F)),
                    (int16_t)(-dir * (span + 35)), CHIRP_TRI };
  return true;
}
