// File / FS sobre el disco del host: la "SD" es una carpeta (TAMAPOKE_SD o
// tools/sdcard), asi el renderer carga los mismos sprites que la placa.
#pragma once
#include "Arduino.h"
#include <stdio.h>

#define FILE_READ "r"
#define FILE_WRITE "w"

class File {
public:
  File() {}
  explicit File(FILE *f) : f_(f) {}
  explicit operator bool() const { return f_ != nullptr; }
  uint32_t size() {
    long p = ftell(f_);
    fseek(f_, 0, SEEK_END);
    long n = ftell(f_);
    fseek(f_, p, SEEK_SET);
    return (uint32_t)n;
  }
  size_t read(uint8_t *b, size_t n) { return fread(b, 1, n, f_); }
  size_t write(const uint8_t *b, size_t n) { return fwrite(b, 1, n, f_); }
  void close() {
    if (f_) fclose(f_);
    f_ = nullptr;
  }
  File openNextFile() { return File(); }
  const char *name() { return ""; }
private:
  FILE *f_ = nullptr;
};
