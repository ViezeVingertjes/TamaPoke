// Piezas del hardware que el renderer del host reemplaza por stubs.
#include "shim/Arduino.h"
#include "shim/Arduino_GFX_Library.h"
#include "shim/Wire.h"
#include "shim/SD_MMC.h"
#include "../../rtcbat.h"
#include "../../audio.h"
#include "../../sdmon.h"
#include "../../nearby_radio.h"

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
bool batShouldShutdown() { return false; }
void batPowerOff() {}
bool usbPresent() { return false; }
bool usbPresentNow() { return false; }
void batSetPollMs(uint32_t) {}
void pwrSetup() {}
bool pwrShortPressed() { return false; }

// vecinos de mentira: las escenas rellenan gHostNb (el id decide el hueco, como en la tabla real)
NbEntry gHostNb[NB_TABLE_SIZE];
int gHostNbN = 0;
void nearbyBegin() {}
void nearbyPoll(uint32_t, const PowerCtx &, const NbSelf &) {}
bool nearbyRadioOn() { return false; }
uint32_t nearbyNextEventMs(uint32_t) { return 0; }
uint32_t nearbyEpoch() { return 0; }
int nearbyShown(const NbEntry **out) {
  static NbTable t;  // los punteros que devuelve shown() apuntan aqui
  t.clear();
  t.selfId = 0xFFFFFF;
  for (int i = 0; i < gHostNbN; i++) memcpy(&t.e[i], &gHostNb[i], sizeof(NbEntry));
  return t.shown(out);
}

void audioSetSleeping(bool) {}
void audioSetIdle(bool) {}
bool audioBusy() { return false; }
