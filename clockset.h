#pragma once
#include <stdint.h>

// Epoch for "hh:mm" chosen on the clock screen: the date that puts it closest to `now` (never
// more than 12 h away), so setting 23:30 at 00:30 goes back an hour into the previous day
// instead of forward 23 hours, and 00:05 at 23:50 goes forward into the next day. Seconds are 0.
inline uint32_t clockEpoch(uint32_t now, int h, int m) {
  int64_t e = (int64_t)(now / 86400) * 86400 + h * 3600 + m * 60;
  if (e - (int64_t)now > 43200) e -= 86400;
  else if ((int64_t)now - e > 43200) e += 86400;
  return (uint32_t)e;
}
