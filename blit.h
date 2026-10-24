#pragma once
#include <stdint.h>

// Merges contiguous same-color pixels in a sprite row: one fillRect
// per run instead of one per pixel (the on-screen result is identical).
// key(c) gives the color key of pixel c, or -1 if transparent;
// emit(c0, len, key) receives each opaque run.
template <class Key, class Emit>
inline void forRuns(int w, Key key, Emit emit) {
  int c = 0, k = w > 0 ? key(0) : -1;
  while (c < w) {
    int c0 = c, kn = k;
    while (++c < w && (kn = key(c)) == k) {}
    if (k >= 0) emit(c0, c - c0, k);
    k = kn;  // the key that ended the run starts the next one: it is not read again
  }
}

// key of an indexed pixel: -1 = transparent (0xFF); in silhouette all the
// opaque pixels share a key (they are painted in the same ink color)
inline int indexedKey(uint8_t idx, bool sil) { return idx == 0xFF ? -1 : (sil ? 0 : idx); }

// the same for character maps: '.' is transparent and the rest use their character
// (unsigned: spriteColor() gets it back with (char)key)
inline int charKey(char ch, bool sil) { return ch == '.' ? -1 : (sil ? 0 : (unsigned char)ch); }
