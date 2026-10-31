#pragma once
#include <stdint.h>
#include <string.h>
#include "power.h"

// Nearby TamaPokes over ESP-NOW (optional). This header is the protocol, the table of
// neighbors and the radio schedule as pure functions (tested on the PC, see
// test/test_nearby.cpp). The real radio is in nearby.cpp.
//
// Power: listening is what costs (~100 mA with the radio on), sending an
// 11-byte packet costs almost nothing. So the radio is only turned on in short windows and
// the schedule depends on whether the device is in use: see nbPeriodMs().

#define NB_MAGIC0 'T'
#define NB_MAGIC1 'P'
#define NB_VERSION 1
#define NB_BEACON_LEN 11
#define NB_TABLE_SIZE 8       // neighbors remembered
#define NB_MAX_SHOWN 4        // neighbors drawn next to the pet
#define NB_TTL_MS 900000UL    // a neighbor with no signals for 15 min is forgotten (two devices with 0.8 s windows hear each other every few minutes)
#define NB_MAX_PPS 30         // packets accepted per second (the rest are dropped)
#define NB_REPEAT_MS 500      // two signals from the same neighbor closer together than this do not count

// ---- packet format (11 bytes) ----
//  0 'T'   1 'P'   2 version   3 species (1-151)   4 flags (bit 0 shiny, bit 1 asleep,
//  bits 2-4 tick: how many 160 ms ticks the sender's window has been open, 7 = no window)
//  5-6 level (little endian)   7-9 device id (24 bits, nonzero)   10 crc8 of the previous 10
// Only data the other device would already see if it were next to this one is sent: no nickname or anything personal.
struct NbBeacon {
  uint8_t species;
  bool shiny, sleeping;
  uint16_t level;
  uint32_t id;  // 24 bits, nonzero
  uint8_t tick; // 0-6: sender's window has been open tick*NB_TICK_MS; NB_TICK_NONE: not in a window
};
#define NB_TICK_MS 160
#define NB_TICK_NONE 7

inline uint8_t nbCrc8(const uint8_t *d, int n) {
  uint8_t c = 0;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

inline int nbEncode(uint8_t *out, uint32_t id, int species, bool shiny, bool sleeping, int level, int tick = NB_TICK_NONE) {
  out[0] = NB_MAGIC0;
  out[1] = NB_MAGIC1;
  out[2] = NB_VERSION;
  out[3] = (uint8_t)species;
  out[4] = (uint8_t)((shiny ? 1 : 0) | (sleeping ? 2 : 0) | ((tick & 7) << 2));
  out[5] = (uint8_t)(level & 0xFF);
  out[6] = (uint8_t)(level >> 8);
  out[7] = (uint8_t)(id & 0xFF);
  out[8] = (uint8_t)((id >> 8) & 0xFF);
  out[9] = (uint8_t)((id >> 16) & 0xFF);
  out[10] = nbCrc8(out, 10);
  return NB_BEACON_LEN;
}

// Validates a packet received from anyone (it is unencrypted broadcast): length, signature,
// version, crc and ranges. Only what passes this reaches the table or the drawing.
inline bool nbDecode(const uint8_t *d, int len, NbBeacon *out) {
  if (len != NB_BEACON_LEN || d[0] != NB_MAGIC0 || d[1] != NB_MAGIC1 || d[2] != NB_VERSION) return false;
  if (nbCrc8(d, 10) != d[10]) return false;
  int species = d[3];
  int level = d[5] | (d[6] << 8);
  uint32_t id = (uint32_t)d[7] | ((uint32_t)d[8] << 8) | ((uint32_t)d[9] << 16);
  if (species < 1 || species > 151 || level < 1 || level > 999 || id == 0 || (d[4] & ~0x1F)) return false;
  out->species = (uint8_t)species;
  out->shiny = d[4] & 1;
  out->sleeping = d[4] & 2;
  out->tick = (d[4] >> 2) & 7;
  out->level = (uint16_t)level;
  out->id = id;
  return true;
}

// ---- neighbor table ----
struct NbEntry {
  bool used;
  bool shiny, sleeping;
  uint8_t species;
  int8_t rssi;
  uint16_t level;
  uint32_t id;
  uint32_t lastSeen;  // ms
  uint32_t lastCount; // ms of the last accepted signal
};

enum NbResult : uint8_t { NB_IGNORED = 0, NB_UPDATED, NB_NEW };

struct NbTable {
  NbEntry e[NB_TABLE_SIZE];
  uint32_t selfId = 0;
  uint32_t secStart = 0;
  uint16_t secCount = 0;

  void clear() { memset(e, 0, sizeof(e)); secStart = 0; secCount = 0; }

  // Processes a valid signal. Drops our own, those arriving too close together (global and
  // per neighbor) and, if the table is full, replaces the oldest.
  NbResult accept(const NbBeacon &b, int rssi, uint32_t now) {
    if (b.id == selfId) return NB_IGNORED;
    if (now - secStart >= 1000 || secStart == 0) { secStart = now ? now : 1; secCount = 0; }
    if (++secCount > NB_MAX_PPS) return NB_IGNORED;
    NbEntry *slot = nullptr;
    for (auto &x : e) if (x.used && x.id == b.id) { slot = &x; break; }
    NbResult r = NB_UPDATED;
    if (slot) {
      if (now - slot->lastCount < NB_REPEAT_MS) return NB_IGNORED;
    } else {
      r = NB_NEW;
      NbEntry *old = nullptr;
      for (auto &x : e) {
        if (!x.used) { slot = &x; break; }
        if (!old || (int32_t)(old->lastSeen - x.lastSeen) > 0) old = &x;
      }
      if (!slot) slot = old;
    }
    slot->used = true;
    slot->id = b.id;
    slot->species = b.species;
    slot->shiny = b.shiny;
    slot->sleeping = b.sleeping;
    slot->level = b.level;
    slot->rssi = (int8_t)(rssi < -127 ? -127 : rssi > 0 ? 0 : rssi);
    slot->lastSeen = slot->lastCount = now;
    return r;
  }

  // forgets those with no signals for more than ttl; returns how many left
  int prune(uint32_t now, uint32_t ttl = NB_TTL_MS) {
    int gone = 0;
    for (auto &x : e)
      if (x.used && (int32_t)(now - x.lastSeen) > (int32_t)ttl) { x.used = false; gone++; }
    return gone;
  }

  // up to NB_MAX_SHOWN neighbors: the most recently seen, sorted by id so that each
  // keeps its place even when signals from others arrive
  int shown(const NbEntry **out) const {
    const NbEntry *pick[NB_TABLE_SIZE];
    int n = 0;
    for (auto &x : e) if (x.used) pick[n++] = &x;
    for (int i = 1; i < n; i++)  // by lastSeen descending (insertion)
      for (int j = i; j > 0 && (int32_t)(pick[j]->lastSeen - pick[j - 1]->lastSeen) > 0; j--) {
        const NbEntry *t = pick[j]; pick[j] = pick[j - 1]; pick[j - 1] = t;
      }
    if (n > NB_MAX_SHOWN) n = NB_MAX_SHOWN;
    for (int i = 1; i < n; i++)  // by id ascending
      for (int j = i; j > 0 && pick[j]->id < pick[j - 1]->id; j--) {
        const NbEntry *t = pick[j]; pick[j] = pick[j - 1]; pick[j - 1] = t;
      }
    for (int i = 0; i < n; i++) out[i] = pick[i];
    return n;
  }
};

// ---- radio schedule ----
enum NbMode : uint8_t { NB_OFF = 0, NB_ACTIVE, NB_DIM, NB_NIGHT, NB_SEARCH };

// ms between radio windows: more often while someone is using the device, and almost never with the
// screen off. Each window lasts NB_WINDOW_MS. NB_SEARCH is the active search: the radio
// listens continuously for NB_SEARCH_MS when the feature is enabled or its button is tapped (with another
// device next to it, they see each other in seconds); it costs ~100 mA during that minute and nothing after.
#define NB_WINDOW_MS 800
#define NB_SEARCH_MS 60000UL
inline uint32_t nbPeriodMs(NbMode m) {
  switch (m) {
    case NB_ACTIVE: return 25000;
    case NB_DIM: return 50000;
    case NB_NIGHT: return 300000;
    default: return 0;  // NB_OFF has no windows; NB_SEARCH is a single continuous one
  }
}
// how long the radio is on, in thousandths of total time (for average consumption)
inline uint32_t nbDutyPermille(NbMode m) {
  return m == NB_SEARCH ? 1000 : m ? NB_WINDOW_MS * 1000UL / nbPeriodMs(m) : 0;
}

// searching: an active search is in progress (only with the screen on)
inline NbMode nbModeFor(const PowerCtx &c, bool enabled, bool searching = false) {
  if (!enabled || batteryLow(c)) return NB_OFF;   // with low battery nothing is spent on this
  if (c.screenOff) return NB_NIGHT;
  if (searching) return NB_SEARCH;
  if (c.sleeping || dimStageFor(c.idleMs) != DIM_AWAKE) return NB_DIM;
  return NB_ACTIVE;
}

// ms until the next window: the period +-25 % at random, so that two devices do not stay
// synchronized forever without hearing each other (or always overlapping)
inline uint32_t nbNextDelayMs(NbMode m, uint32_t rnd) {
  uint32_t p = nbPeriodMs(m);
  return p ? p - p / 4 + rnd % (p / 2 + 1) : 0;
}

// ---- window synchronisation ----
// Windows at random times meet only every 6-25 minutes, so a neighbour is forgotten and its state
// (asleep/awake) goes stale. Once two devices hear each other they agree on a common grid and open
// their windows at the same instants from then on. All periods are multiples of 25 s (and divide
// NB_GRID_MS), so devices in different modes still share windows: active every grid point, dim every
// 2nd, night every 12th. Both sides derive the same anchor: the earlier of the two window starts
// (a device that only listens continuously uses the other's start).
#define NB_STEP_MS 25000UL         // finest grid step: every window opens at a multiple of this from the anchor
#define NB_GRID_MS 300000UL
#define NB_SYNC_TOL_MS 400UL       // a beacon this close to the grid counts as in sync
#define NB_SYNC_LIFE_MS 1200000UL  // with no in-sync beacon for 20 min the grid is dropped (random windows again)

// signed distance of d from the nearest multiple of g
inline int32_t nbGridOffset(int32_t d, uint32_t g = NB_STEP_MS) {
  int32_t m = d % (int32_t)g;
  if (m < 0) m += (int32_t)g;
  return m > (int32_t)(g / 2) ? m - (int32_t)g : m;
}

struct NbSync {
  bool valid = false;
  uint32_t anchor = 0;    // an instant at which a window opens on the shared grid
  uint32_t lastHit = 0;

  bool live(uint32_t now) const { return valid && (uint32_t)(now - lastHit) < NB_SYNC_LIFE_MS; }
  void clear() { valid = false; }

  // A beacon was received at rxAt. haveOwn/ownStart: our own window was open then (not in the
  // continuous search). Returns true if the beacon was used.
  bool hear(const NbBeacon &b, uint32_t rxAt, bool haveOwn, uint32_t ownStart, uint32_t now) {
    bool haveW = b.tick < NB_TICK_NONE;
    uint32_t w = rxAt - (uint32_t)b.tick * NB_TICK_MS;  // when the sender's window opened
    if (!haveW && !haveOwn) return false;
    if (live(now)) {
      if (!haveW) return false;
      int32_t e = nbGridOffset((int32_t)(w - anchor));
      if (e > (int32_t)NB_SYNC_TOL_MS || e < -(int32_t)NB_SYNC_TOL_MS) return false;  // someone else's grid
      anchor += (uint32_t)(e / 2);  // meet halfway: the two clocks drift apart slowly
      lastHit = now;
      return true;
    }
    uint32_t cand = haveW ? w : ownStart;
    if (haveW && haveOwn && (int32_t)(ownStart - w) < 0) cand = ownStart;  // the earlier of the two
    anchor = cand;
    valid = true;
    lastHit = now;
    return true;
  }

  // first window opening >= now for a device with this period
  uint32_t next(uint32_t now, uint32_t period) {
    int32_t d = (int32_t)(now - anchor);
    if (d <= 0) return anchor;
    if (d > (int32_t)(2 * NB_GRID_MS)) {  // keep the anchor recent so it never overflows
      anchor += (uint32_t)(d / (int32_t)NB_GRID_MS - 1) * NB_GRID_MS;
      d = (int32_t)(now - anchor);
    }
    uint32_t k = ((uint32_t)d + period - 1) / period;
    return anchor + k * period;
  }
};

// instants (ms from the start of the window) at which a signal is sent: on opening, and
// repeated twice in case the other opens its window midway
#define NB_BEACON_COUNT 5
inline uint32_t nbBeaconAtMs(int i) { return (uint32_t)i * 160; }
#define NB_SEARCH_BEACON_MS 500  // during the active search, one signal every so often
