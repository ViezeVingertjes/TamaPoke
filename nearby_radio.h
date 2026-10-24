#pragma once
#include <stdint.h>
#include "nearby.h"
#include "power.h"

// Radio layer for nearby TamaPokes (ESP-NOW). The logic (packet, table, schedule) is
// in nearby.h; this only turns the radio on and off. The host renderer has a fake
// version (test/render/nearby_host.cpp).

struct NbSelf {
  bool valid;       // false while still an egg: nothing is sent
  uint8_t species;
  bool shiny, sleeping;
  uint16_t level;
};

void nearbyBegin();                         // reads (or creates) this device's id in NVS; searches for 60 s
bool nearbySearching();
// Radio state machine: called on every pass of the main loop (cheap when the
// radio is off). Turns the radio on in the scheduled windows, sends and receives, then turns it off.
void nearbyPoll(uint32_t now, const PowerCtx &c, const NbSelf &self);
bool nearbyRadioOn();                       // light sleep is not possible while the radio is on
uint32_t nearbyNextEventMs(uint32_t now);   // ms until the next window (0 = none): sleep must not go past it
int nearbyShown(const NbEntry **out);       // neighbors to draw (up to NB_MAX_SHOWN)
uint32_t nearbyEpoch();                     // +1 each time who is nearby changes
