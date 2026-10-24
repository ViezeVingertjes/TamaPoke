#pragma once
#include <stdint.h>
#include <stdlib.h>

// Power policy. Everything that decides how hard the board works lives here as
// pure functions (no hardware), so they can be tested on the PC (test/test_power.cpp).
//
// Where the battery goes: the AMOLED screen (brightness and content), the CPU while
// drawing and pushing the canvas over QSPI (~70 ms of every 100 at 10 fps), the
// speaker amplifier if it is on while silent, and main loop wakeups.

// Inactivity steps (ms since the last touch or visible event)
#define POWER_CALM_MS 60000UL   // after this time, drawing is reduced a bit
#define POWER_DIM1_MS 90000UL   // screen dimmed
#define POWER_DIM2_MS 300000UL  // screen almost off

enum DimStage : uint8_t { DIM_AWAKE = 0, DIM_LOW = 1, DIM_OFF = 2 };

struct PowerCtx {
  uint32_t idleMs;    // ms without interaction
  bool screenOff;     // turned off with the PWR button
  bool sleeping;      // the pet is asleep
  bool gameOpen;      // minigame or punching bag: the physics is tuned to 85 ms per frame
  bool usb;           // external power
  bool charging;
  int batPct;         // 0-100, -1 if there is no battery
};

// ms since `last`, without overflowing if `last` is after `now` (a touch handled after
// reading the clock at the start of the loop): it was taken as 49 days of inactivity and the panel
// dropped to brightness 8 for an instant on every touch
inline uint32_t idleSince(uint32_t now, uint32_t last) {
  int32_t d = (int32_t)(now - last);
  return d < 0 && d > -1000 ? 0 : now - last;  // only a lead of a few ms counts as "now"; 25 days untouched are still 25 days
}

inline uint8_t dimStageFor(uint32_t idleMs) {
  return idleMs > POWER_DIM2_MS ? DIM_OFF : idleMs > POWER_DIM1_MS ? DIM_LOW : DIM_AWAKE;
}

// low battery: save more (only if it really runs on battery)
inline bool batteryLow(const PowerCtx &c) { return c.batPct >= 0 && c.batPct < 20 && !c.charging && !c.usb; }
inline bool batteryCritical(const PowerCtx &c) { return c.batPct >= 0 && c.batPct < 10 && !c.charging && !c.usb; }

// panel brightness (0-255): the usual one, capped when the battery is low
inline uint8_t brightnessFor(const PowerCtx &c) {
  if (c.screenOff) return 0;
  uint8_t stage = dimStageFor(c.idleMs);
  uint8_t t = c.sleeping ? 25 : (c.usb ? 180 : 145);
  if (stage == DIM_LOW) t = c.sleeping ? 10 : 60;
  else if (stage == DIM_OFF) t = 8;
  if (batteryCritical(c) && t > 60) t = 60;
  else if (batteryLow(c) && t > 100) t = 100;
  return t;
}

// ms between frames; 0 = do not draw (screen off). Each frame costs a full render
// and a canvas push over QSPI, and the panel only changes if it is redrawn.
inline uint32_t frameIntervalMs(const PowerCtx &c) {
  if (c.screenOff) return 0;
  if (c.gameOpen) return 85;  // the game rules: its physics is tuned to this period
  uint32_t ms;
  uint8_t stage = dimStageFor(c.idleMs);
  if (stage == DIM_OFF) ms = 1000;
  else if (c.sleeping && c.idleMs > POWER_CALM_MS) ms = 500;  // asleep and untouched: almost nothing moves
  else if (stage == DIM_LOW) ms = 250;
  else ms = c.idleMs > POWER_CALM_MS ? 150 : 100;
  if (batteryLow(c)) ms *= 2;
  return ms;
}

// the speaker amplifier draws power at idle: it is only kept on while
// someone may be playing
inline bool amplifierWanted(const PowerCtx &c) {
  return !c.screenOff && !c.sleeping && dimStageFor(c.idleMs) == DIM_AWAKE;
}

// main loop pause on a pass that does not draw
inline uint32_t idleDelayMs(const PowerCtx &c) { return c.screenOff ? 20 : 1; }

// with the screen off, or almost off (5 min untouched), and no USB, the CPU sleeps (light
// sleep) between frames and polls; not with USB: the serial port of the SD card would be lost
inline bool lightSleepAllowed(const PowerCtx &c) {
  return !c.usb && (c.screenOff || dimStageFor(c.idleMs) == DIM_OFF);
}
#define POWER_SLEEP_MS 200  // duration of each sleep; a touch wakes it earlier

// how often the battery is read over I2C
inline uint32_t powerCacheMs(const PowerCtx &c) {
  return (c.screenOff || dimStageFor(c.idleMs) == DIM_OFF) ? 10000 : 2000;
}

// Touch wake-up from the PWR screen-off: a stray brush must not light the panel (pocket,
// bag), so the finger has to stay put for this long. The PWR button still wakes at once.
#define POWER_WAKE_HOLD_MS 700
inline bool touchWakes(uint32_t heldMs, int dx, int dy, int slopX, int slopY) {
  return heldMs >= POWER_WAKE_HOLD_MS && abs(dx) < slopX && abs(dy) < slopY;
}
