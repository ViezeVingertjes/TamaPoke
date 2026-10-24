// Extras de Arduino/ESP32 que necesita TamaPoke.ino para compilar en el PC.
// Se apoya en el shim comun de test/shim.
#pragma once
#include "../../shim/Arduino.h"
#include <string>
#include <algorithm>
#include <math.h>
#ifndef PI
#define PI 3.14159265358979f
#endif

class __FlashStringHelper;
#define IRAM_ATTR
#define INPUT_PULLUP 2
#define FALLING 2
#define digitalPinToInterrupt(p) (p)
static inline void pinMode(int, int) {}
static inline void attachInterrupt(int, void (*)(), int) {}
static inline void delay(uint32_t) {}
static inline void *ps_malloc(size_t n) { return malloc(n); }

struct MockEsp {
  uint32_t getFreeHeap() { return 200000; }
  uint32_t getMinFreeHeap() { return 150000; }
  void restart() {}
};
extern MockEsp ESP;

// String minimo (solo lo que usa el sketch y sdmon.h)
class String : public std::string {
public:
  using std::string::operator=;
  String() {}
  String(const std::string &s) : std::string(s) {}
  operator const char *() const { return c_str(); }
  int indexOf(char c, size_t from = 0) const { size_t p = find(c, from); return p == npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { size_t p = rfind(c); return p == npos ? -1 : (int)p; }
  String(const char *s) : std::string(s) {}
  void trim() {
    size_t a = find_first_not_of(" \t\r\n");
    size_t b = find_last_not_of(" \t\r\n");
    assign(a == npos ? "" : substr(a, b - a + 1));
  }
  bool startsWith(const char *p) const { return rfind(p, 0) == 0; }
  String substring(size_t a) const { return String(substr(a).c_str()); }
  String substring(size_t a, size_t b) const { return String(substr(a, b - a).c_str()); }
  int toInt() const { return atoi(c_str()); }
};

// el Serial real es mas ancho que el del shim comun: se envuelve y se redirige
struct HostSerial : MockSerial {
  void setTxTimeoutMs(int) {}
  void setRxBufferSize(int) {}
  void setTimeout(int) {}
  int available() { return 0; }
  String readStringUntil(char) { return String(); }
  size_t readBytes(uint8_t *, size_t) { return 0; }
  using MockSerial::println;
  void println() {}
  void println(int) {}
};
extern HostSerial HSerial;
#define Serial HSerial
