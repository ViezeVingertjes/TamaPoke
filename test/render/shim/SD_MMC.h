#pragma once
#include "FS.h"
#include <stdlib.h>
#include <sys/stat.h>

struct HostSD {
  std::string root() {
    const char *r = getenv("TAMAPOKE_SD");
    return r ? r : "tools/sdcard";
  }
  void setPins(int, int, int) {}
  bool begin(const char *, bool, bool) {
    struct stat st;
    return stat(root().c_str(), &st) == 0;
  }
  uint64_t cardSize() { return 0; }
  uint64_t totalBytes() { return 0; }
  uint64_t usedBytes() { return 0; }
  bool mkdir(const char *) { return true; }
  bool exists(const char *p) {
    struct stat st;
    return stat((root() + p).c_str(), &st) == 0;
  }
  bool remove(const char *) { return false; }
  // read-only: the sprites folder is the repo's and is left untouched
  File open(const char *p, const char *mode = FILE_READ) {
    if (mode[0] != 'r') return File();
    FILE *f = fopen((root() + p).c_str(), mode);
    return f ? File(f) : File();
  }
};
extern HostSD SD_MMC;
