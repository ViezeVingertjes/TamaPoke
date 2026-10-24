#pragma once
#include <stdint.h>

// Epoch for "hh:mm" chosen on the clock screen: the date that puts it closest to `now` (never
// more than 12 h away), so setting 23:30 at 00:30 goes back an hour into the previous day
// instead of forward 23 hours, and 00:05 at 23:50 goes forward into the next day. Seconds are 0.
// The time the screen starts from: the RTC (read once by the caller), else the cached time, else
// 2026-01-01 (the same seed a blank RTC gets at boot), so a time set with no clock at all is a
// real date and not day 0, which the game reads as "no clock".
inline uint32_t clockBase(uint32_t rtc, uint32_t cached) { return rtc ? rtc : cached ? cached : 1767225600UL; }

inline uint32_t clockEpoch(uint32_t now, int h, int m) {
  int64_t e = (int64_t)(now / 86400) * 86400 + h * 3600 + m * 60;
  if (e - (int64_t)now > 43200 && e >= 86400) e -= 86400;  // never before the epoch: without a valid time now is 0
  else if ((int64_t)now - e > 43200) e += 86400;
  return (uint32_t)e;
}
