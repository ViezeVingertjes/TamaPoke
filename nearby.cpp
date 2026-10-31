#include "nearby_radio.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_random.h>

// ---------------------------------------------------------------------------
// ESP-NOW radio to see the TamaPokes next to you. Designed to use little power:
//  - the radio is only on in 0.8 s windows (every 25 s in use, 50 s with the
//    screen dimmed, 5 min with the screen off) and in a 60 s active search
//    that starts when the feature is enabled;
//  - minimum transmit power (2 dBm): "nearby" means nearby;
//  - the receive callback does nothing but store the packet in a ring; validation
//    and the table are handled in the main loop;
//  - everyone uses channel 1 and broadcast (no pairing, no access point, unencrypted: the
//    packet only carries species, level and a random id, nothing personal).
// It lives in the main loop and not in a task: the firmware's light sleep freezes the FreeRTOS
// clock and a task with vTaskDelay would be delayed as long as the CPU sleeps.
// ---------------------------------------------------------------------------

#define NB_CHANNEL 1
#define NB_RING 8

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

struct RxPacket {
  uint8_t len;
  int8_t rssi;
  uint32_t at;  // millis() when it arrived
  uint8_t data[NB_BEACON_LEN];
};
static RxPacket ring[NB_RING];
static volatile uint8_t ringHead = 0, ringTail = 0;  // the callback writes head, the loop reads tail
static portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t gSelfId = 0;
static uint8_t gMac[6];
static NbTable gTable;
static bool gRadioOn = false;
static uint32_t gWindowEnd = 0;       // end of the current window
static uint32_t gWindowStart = 0;
static uint32_t gNextWindow = 0;      // start of the next window
static uint32_t gSearchUntil = 0;     // end of the active search (0 = not searching)
static uint32_t gNextBeacon = 0;      // next signal within the window
static int gBeaconIdx = 0;
static uint32_t gLastReply = 0;
static uint32_t gLastPrune = 0;
static uint32_t gFailUntil = 0;       // after a radio failure, do not retry until this time (if gFailed)
static bool gFailed = false;
static NbMode gMode = NB_OFF;
static NbSync gSync;                  // shared window grid with a neighbor (see nearby.h)
static bool gWinSearch = false;       // the current/last radio session was the continuous search (no window)
static uint32_t gEpoch = 0;
static uint32_t gKnownMask = 0;       // fingerprint of the neighbors shown (to tell whether they change)

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != NB_BEACON_LEN) return;  // nothing that is not exactly one packet long is stored
  portENTER_CRITICAL(&ringMux);
  uint8_t next = (ringHead + 1) % NB_RING;
  if (next != ringTail) {  // ring full: drop it
    ring[ringHead].len = (uint8_t)len;
    ring[ringHead].at = millis();
    ring[ringHead].rssi = info && info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : -80;
    memcpy(ring[ringHead].data, data, len);
    ringHead = next;
  }
  portEXIT_CRITICAL(&ringMux);
}

static bool radioStart() {
  WiFi.mode(WIFI_STA);  // starts Wi-Fi without connecting to anything
  esp_wifi_set_mac(WIFI_IF_STA, gMac);
  esp_wifi_set_channel(NB_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_max_tx_power(8);  // 2 dBm, the minimum
  if (esp_now_init() != ESP_OK) { WiFi.mode(WIFI_OFF); return false; }
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = NB_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) { esp_now_deinit(); WiFi.mode(WIFI_OFF); return false; }
  return true;
}

static void radioStop() {
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);  // turns the radio fully off (this is what saves power)
}

static void sendBeacon(uint32_t now, const NbSelf &self) {
  if (!self.valid) return;
  // tell the receiver how long our window has been open, so it can line up with it
  int tick = NB_TICK_NONE;
  if (!gWinSearch) {
    uint32_t t = (now - gWindowStart + NB_TICK_MS / 2) / NB_TICK_MS;
    tick = t > 6 ? 6 : (int)t;
  }
  uint8_t pkt[NB_BEACON_LEN];
  nbEncode(pkt, gSelfId, self.species, self.shiny, self.sleeping, self.level, tick);
  esp_now_send(BCAST, pkt, NB_BEACON_LEN);
}

void nearbyBegin() {
  // the id and MAC change on every boot and are not saved: a device cannot be tracked from one day
  // to the next (the id only serves to avoid counting the same neighbor twice for a while)
  do gSelfId = esp_random() & 0xFFFFFF; while (!gSelfId);
  esp_fill_random(gMac, 6);
  gMac[0] = (gMac[0] | 0x02) & 0xFE;  // locally administered, unicast
  gTable.clear();
  gSync.clear();
  gTable.selfId = gSelfId;
  gNextWindow = millis() + 2000 + (esp_random() % 8000);
  gSearchUntil = millis() + NB_SEARCH_MS;  // on boot it searches for a minute: with another device next to it they see each other in seconds
}

bool nearbySearching() { return gSearchUntil && (int32_t)(gSearchUntil - millis()) > 0; }
bool nearbyRadioOn() { return gRadioOn; }

uint32_t nearbyNextEventMs(uint32_t now) {
  if (gRadioOn || gMode == NB_OFF || gMode == NB_SEARCH) return 0;
  uint32_t at = gFailed && (int32_t)(gFailUntil - gNextWindow) > 0 ? gFailUntil : gNextWindow;
  int32_t d = (int32_t)(at - now);
  return d > 0 ? (uint32_t)d : 1;
}

int nearbyShown(const NbEntry **out) { return gTable.shown(out); }
uint32_t nearbyEpoch() { return gEpoch; }

static uint32_t fingerprint() {
  const NbEntry *s[NB_MAX_SHOWN];
  int n = gTable.shown(s);
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) {
    h = (h ^ s[i]->id) * 16777619u;
    h = (h ^ (s[i]->species | (s[i]->shiny << 8) | (s[i]->sleeping << 9))) * 16777619u;
  }
  return h ^ (uint32_t)n;
}

// handles the packets received since the last pass
static void drainRing(uint32_t now, const NbSelf &self) {
  while (ringTail != ringHead) {
    RxPacket pk;
    portENTER_CRITICAL(&ringMux);
    pk = ring[ringTail];
    ringTail = (ringTail + 1) % NB_RING;
    portEXIT_CRITICAL(&ringMux);
    NbBeacon b;
    if (!nbDecode(pk.data, pk.len, &b)) continue;
    NbResult r = gTable.accept(b, pk.rssi, now);
    if (r != NB_IGNORED) {
      bool haveOwn = !gWinSearch && (uint32_t)(pk.at - gWindowStart) < NB_WINDOW_MS + 500;
      gSync.hear(b, pk.at, haveOwn, gWindowStart, now);
    }
    // a new one: reply right away, so that it also sees us even if its window is about to
    // close (at most one reply per second)
    if (r == NB_NEW && gRadioOn && now - gLastReply > 1000) {
      gLastReply = now;
      sendBeacon(now, self);
    }
  }
}

void nearbyPoll(uint32_t now, const PowerCtx &c, const NbSelf &self) {
  bool searching = nearbySearching();
  if (!searching) gSearchUntil = 0;
  NbMode mode = self.valid ? nbModeFor(c, true, searching) : NB_OFF;
  gMode = mode;
  drainRing(now, self);  // also with the radio off: whatever arrived in the last window

  if (gRadioOn) {
    // the search lasts until it ends; the other windows, NB_WINDOW_MS
    bool end = mode == NB_OFF || (mode != NB_SEARCH && (int32_t)(now - gWindowEnd) >= 0);
    if (!end && (int32_t)(now - gNextBeacon) >= 0) {
      sendBeacon(now, self);
      if (mode == NB_SEARCH) gNextBeacon = now + NB_SEARCH_BEACON_MS;
      else if (++gBeaconIdx < NB_BEACON_COUNT) gNextBeacon = gWindowStart + nbBeaconAtMs(gBeaconIdx);
      else gNextBeacon = gWindowEnd;
    }
    if (end) {
      radioStop();
      gRadioOn = false;
      NbMode dm = (mode == NB_SEARCH || mode == NB_OFF) ? NB_ACTIVE : mode;
      gNextWindow = gSync.live(now) ? gSync.next(now, nbPeriodMs(dm)) : now + nbNextDelayMs(dm, esp_random());
    }
  } else if (mode != NB_OFF) {
    // if the screen turns on, the next window cannot be minutes away
    if (mode != NB_SEARCH) {
      uint32_t maxWait = nbPeriodMs(mode) * 5 / 4;
      if ((int32_t)(gNextWindow - now) > (int32_t)maxWait)
        gNextWindow = gSync.live(now) ? gSync.next(now, nbPeriodMs(mode)) : now + esp_random() % (maxWait + 1);
    }
    bool due = mode == NB_SEARCH || (int32_t)(now - gNextWindow) >= 0;
    if (due && (!gFailed || (int32_t)(now - gFailUntil) >= 0)) {
      if (radioStart()) {
        gRadioOn = true;
        gWindowStart = now;
        gWinSearch = mode == NB_SEARCH;
        gWindowEnd = now + NB_WINDOW_MS;
        gBeaconIdx = 0;
        gNextBeacon = now;
      } else {
        gFailed = true;
        gFailUntil = now + 600000;  // the radio did not start: retry in 10 min
      }
    }
  }
  if (now - gLastPrune > 5000) {
    gLastPrune = now;
    gTable.prune(now);
  }
  uint32_t fp = fingerprint();
  if (fp != gKnownMask) { gKnownMask = fp; gEpoch++; }
}
