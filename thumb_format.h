#pragma once
#include <stdint.h>
#include <string.h>

// thumbs.bin entries (see tools/make_thumbs.py): u8 w, u8 h, u8 palCount, u16 pal[palCount],
// u8 idx[w*h] with 0xFF = transparent. Pure helpers so the host tests can feed them bad data.

// size in bytes of the entry at `off`, or 0 if its header or body does not fit in `size`
inline uint32_t thumbEntryBytes(const uint8_t *d, uint32_t size, uint32_t off) {
  if (off > size - 3) return 0;  // the w,h,palCount header must fit (size >= 10 always)
  uint32_t need = 3 + (uint32_t)d[off + 2] * 2 + (uint32_t)d[off] * d[off + 1];
  return need > size || off > size - need ? 0 : need;
}

// every pixel is transparent or points at a palette entry; drawThumb() reads the palette
// by that index, so a bigger one would read past the palette (and past the file at the end)
inline bool thumbIndicesValid(const uint8_t *entry) {
  uint32_t n = (uint32_t)entry[0] * entry[1];
  const uint8_t *px = entry + 3 + entry[2] * 2;
  for (uint32_t i = 0; i < n; i++)
    if (px[i] != 0xFF && px[i] >= entry[2]) return false;
  return true;
}
