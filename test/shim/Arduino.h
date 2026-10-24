// Minimal Arduino.h shim to run the firmware logic on the host.
// It only covers what pet.cpp / i18n.cpp use. The clock is fake and controllable
// so tests are deterministic.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- fake clock -----------------------------------------------------------
uint32_t millis();
void mockSetMillis(uint32_t ms);
void mockAdvanceMillis(uint32_t ms);

// ---- deterministic random (same semantics as Arduino: [0, howbig) ) -------
long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);
// forces the next random() call(s) to a fixed value (for tests)
void mockForceRandom(long value);   // every random() returns value
void mockClearForcedRandom();

// ---- Serial ----------------------------------------------------------------
struct MockSerial {
  int printf(const char *fmt, ...);
  void print(const char *s);
  void println(const char *s);
  void begin(unsigned long);
};
extern MockSerial Serial;
void mockSerialSilence(bool quiet);

// ---- utilities ------------------------------------------------------------
template <class T> static inline T min(T a, T b) { return a < b ? a : b; }
template <class T> static inline T max(T a, T b) { return a > b ? a : b; }
