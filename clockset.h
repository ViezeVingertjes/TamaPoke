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

// Calendar date of a day number (days since 1970-01-01) and back, Hinnant's civil algorithms
// (no mktime: its time zone differs between the board and the host tests).
inline void clockYmd(int32_t day, int &y, int &m, int &d) {
  int32_t z = day + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)yoe + era * 400 + (m <= 2);
}

inline int32_t clockDay(int y, int m, int d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400), doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  return era * 146097 + (int32_t)(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
}

// The same day n months later (or earlier), clamped to that month's length: 31 Jan + 1 = 28/29 Feb.
inline int32_t clockAddMonths(int32_t day, int n) {
  int y, m, d;
  clockYmd(day, y, m, d);
  int k = y * 12 + m - 1 + n;
  y = k / 12;
  m = k % 12 + 1;
  int len = clockDay(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - clockDay(y, m, 1);
  return clockDay(y, m, d < len ? d : len);
}

// The dates the clock screen offers: from 2025 (rtcEpoch() rejects earlier years) to the end of
// 2105, the last whole year a uint32_t epoch holds.
inline int32_t clockClampDay(int32_t day) {
  int32_t lo = clockDay(2025, 1, 1), hi = clockDay(2105, 12, 31);
  return day < lo ? lo : day > hi ? hi : day;
}

// Epoch for the date and hh:mm on the clock screen: with the date untouched, the nearest hh:mm
// (clockEpoch, also right if midnight passed while the screen was open); with a date picked,
// exactly that day.
inline uint32_t clockSet(uint32_t now, bool picked, int32_t day, int h, int m) {
  if (!picked) return clockEpoch(now, h, m);
  return (uint32_t)day * 86400 + h * 3600 + m * 60;
}
