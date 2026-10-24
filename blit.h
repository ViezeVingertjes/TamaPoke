#pragma once
#include <stdint.h>

// Une los pixeles contiguos del mismo color de una fila de sprite: un fillRect
// por tramo en vez de uno por pixel (el resultado en pantalla es identico).
// key(c) da la clave de color del pixel c, o -1 si es transparente;
// emit(c0, len, key) recibe cada tramo opaco.
template <class Key, class Emit>
inline void forRuns(int w, Key key, Emit emit) {
  int c = 0, k = w > 0 ? key(0) : -1;
  while (c < w) {
    int c0 = c, kn = k;
    while (++c < w && (kn = key(c)) == k) {}
    if (k >= 0) emit(c0, c - c0, k);
    k = kn;  // la clave que corto el tramo abre el siguiente: no se vuelve a leer
  }
}

// clave de un pixel indexado: -1 = transparente (0xFF); en silueta todos los
// pixeles opacos comparten clave (se pintan del mismo color de tinta)
inline int indexedKey(uint8_t idx, bool sil) { return idx == 0xFF ? -1 : (sil ? 0 : idx); }

// lo mismo para los mapas de caracteres: '.' es transparente y el resto, su caracter
// (sin signo: spriteColor() lo recibe de vuelta con (char)key)
inline int charKey(char ch, bool sil) { return ch == '.' ? -1 : (sil ? 0 : (unsigned char)ch); }
