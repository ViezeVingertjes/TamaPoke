#pragma once
#include "Wire.h"
// the host tests script the finger: handleTouch() reads these
extern bool gShimTouchDown;
extern int16_t gShimTouchX, gShimTouchY;
struct TouchDrvCST92xx {
  void setPins(int, int) {}
  bool begin(TwoWire &, int, int, int) { return true; }
  void reset() {}
  void setMaxCoordinates(int, int) {}
  void setMirrorXY(bool, bool) {}
  void setSwapXY(bool) {}
  uint8_t getPoint(int16_t *x, int16_t *y, uint8_t) {
    if (!gShimTouchDown) return 0;
    *x = gShimTouchX;
    *y = gShimTouchY;
    return 1;
  }
};
