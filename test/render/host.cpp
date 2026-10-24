// Piezas del hardware que el renderer del host reemplaza por stubs.
#include "shim/Arduino.h"
#include "shim/Arduino_GFX_Library.h"
#include "shim/Wire.h"
#include "shim/SD_MMC.h"
#include "../../rtcbat.h"
#include "../../audio.h"
#include "../../sdmon.h"

MockEsp ESP;
HostSerial HSerial;
TwoWire Wire;
HostSD SD_MMC;

std::function<void(const uint16_t *, int, int)> gOnFrame;

// RTC y PMU: hora fija y bateria al 80 % para que las capturas sean estables
static uint32_t gEpoch = 1767225600UL + 12 * 3600;
int gBatPercent = 80;
bool gBatCharging = false;
bool rtcBegin() { return true; }
uint32_t rtcEpoch() { return gEpoch; }
void rtcSetEpoch(uint32_t e) { gEpoch = e; }
bool batBegin() { return true; }
void pmuEnablePanel() {}
int batPercent() { return gBatPercent; }
bool batCharging() { return gBatCharging; }
int batMillivolts() { return 3900; }
bool usbPresent() { return false; }
bool usbPresentNow() { return false; }
void batSetPollMs(uint32_t) {}
void pwrSetup() {}
bool pwrShortPressed() { return false; }

void audioSetSleeping(bool) {}
void audioSetIdle(bool) {}
bool audioBusy() { return false; }
