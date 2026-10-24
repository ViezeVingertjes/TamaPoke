#pragma once
#include "Wire.h"
struct TouchDrvCST92xx {
  void setPins(int, int) {}
  bool begin(TwoWire &, int, int, int) { return true; }
  void reset() {}
  void setMaxCoordinates(int, int) {}
  void setMirrorXY(bool, bool) {}
  void setSwapXY(bool) {}
  uint8_t getPoint(int16_t *, int16_t *, uint8_t) { return 0; }
};
