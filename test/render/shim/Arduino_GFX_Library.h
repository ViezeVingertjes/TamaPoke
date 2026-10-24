// The real Arduino_GFX (Arduino_GFX.cpp, Arduino_Canvas.cpp from the library
// GFX Library for Arduino, compiled on the PC): drawing and text come out
// exactly as on the board. Only the fake bus and panel live here;
// the panel receives each flush() of the canvas and hands it to gOnFrame.
#pragma once
#include "Arduino.h"
#include <functional>
#include "Arduino_GFX.h"
#include "canvas/Arduino_Canvas.h"

#define RGB565_BLACK 0x0000
#define RGB565_WHITE 0xFFFF
#define RGB565_RED 0xF800

extern std::function<void(const uint16_t *, int, int)> gOnFrame;

class Arduino_ESP32QSPI : public Arduino_DataBus {
public:
  Arduino_ESP32QSPI(int, int, int, int, int, int) {}
  bool begin(int32_t = 0, int8_t = 0) override { return true; }
  void beginWrite() override {}
  void endWrite() override {}
  void writeCommand(uint8_t) override {}
  void writeCommand16(uint16_t) override {}
  void writeCommandBytes(uint8_t *, uint32_t) override {}
  void write(uint8_t) override {}
  void write16(uint16_t) override {}
  void writeRepeat(uint16_t, uint32_t) override {}
  void writeBytes(uint8_t *, uint32_t) override {}
  void writePixels(uint16_t *, uint32_t) override {}
};

class Arduino_CO5300 : public Arduino_G {
public:
  Arduino_CO5300(Arduino_DataBus *, int, uint8_t, int16_t w, int16_t h, uint8_t, uint8_t, uint8_t, uint8_t)
      : Arduino_G(w, h) {}
  bool begin(int32_t = 0) override { return true; }
  void setBrightness(uint8_t) {}
  void draw16bitRGBBitmap(int16_t, int16_t, uint16_t *bitmap, int16_t w, int16_t h) override {
    if (gOnFrame) gOnFrame(bitmap, w, h);
  }
  void drawBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t, uint16_t, uint16_t) override {}
  void drawIndexedBitmap(int16_t, int16_t, uint8_t *, uint16_t *, int16_t, int16_t, int16_t = 0) override {}
  void draw3bitRGBBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t) override {}
  void draw24bitRGBBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t) override {}
};
