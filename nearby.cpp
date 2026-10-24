#include "nearby_radio.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_random.h>

// ---------------------------------------------------------------------------
// Radio ESP-NOW para ver a los TamaPokes de al lado. Diseno para gastar poco:
//  - la radio solo esta encendida en ventanas de 0,8 s (cada 25 s en uso, 45 s con la
//    pantalla atenuada, 5 min con la pantalla apagada) y en una busqueda activa de 60 s
//    que empieza al activar la funcion;
//  - potencia de transmision minima (2 dBm): "cerca" quiere decir cerca;
//  - el callback de recepcion no hace nada mas que guardar el paquete en un anillo; la
//    validacion y la tabla se atienden en el bucle principal;
//  - todos usan el canal 1 y difusion (sin emparejar, sin punto de acceso, sin cifrar: el
//    paquete solo lleva especie, nivel y un id aleatorio, nada personal).
// Vive en el bucle principal y no en una tarea: el light sleep del firmware congela el reloj de
// FreeRTOS y una tarea con vTaskDelay se retrasaria tanto como duerma la CPU.
// ---------------------------------------------------------------------------

#define NB_CHANNEL 1
#define NB_RING 8

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

struct RxPacket {
  uint8_t len;
  int8_t rssi;
  uint8_t data[NB_BEACON_LEN];
};
static RxPacket ring[NB_RING];
static volatile uint8_t ringHead = 0, ringTail = 0;  // el callback escribe head, el bucle lee tail
static portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t gSelfId = 0;
static uint8_t gMac[6];
static NbTable gTable;
static bool gRadioOn = false;
static uint32_t gWindowEnd = 0;       // fin de la ventana actual
static uint32_t gWindowStart = 0;
static uint32_t gNextWindow = 0;      // inicio de la proxima ventana
static uint32_t gSearchUntil = 0;     // fin de la busqueda activa (0 = no busca)
static uint32_t gNextBeacon = 0;      // proxima senal dentro de la ventana
static int gBeaconIdx = 0;
static uint32_t gLastReply = 0;
static uint32_t gLastPrune = 0;
static uint32_t gFailUntil = 0;       // tras un fallo de la radio, sin intentarlo hasta aqui (si gFailed)
static bool gFailed = false;
static NbMode gMode = NB_OFF;
static uint32_t gEpoch = 0;
static uint32_t gKnownMask = 0;       // huella de los vecinos mostrados (para saber si cambian)

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != NB_BEACON_LEN) return;  // nada que no mida exactamente un paquete se guarda
  portENTER_CRITICAL(&ringMux);
  uint8_t next = (ringHead + 1) % NB_RING;
  if (next != ringTail) {  // anillo lleno: se descarta
    ring[ringHead].len = (uint8_t)len;
    ring[ringHead].rssi = info && info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : -80;
    memcpy(ring[ringHead].data, data, len);
    ringHead = next;
  }
  portEXIT_CRITICAL(&ringMux);
}

static bool radioStart() {
  WiFi.mode(WIFI_STA);  // arranca Wi-Fi sin conectar a nada
  esp_wifi_set_mac(WIFI_IF_STA, gMac);
  esp_wifi_set_channel(NB_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_max_tx_power(8);  // 2 dBm, el minimo
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
  WiFi.mode(WIFI_OFF);  // apaga la radio del todo (es lo que ahorra)
}

static void sendBeacon(const NbSelf &self) {
  if (!self.valid) return;
  uint8_t pkt[NB_BEACON_LEN];
  nbEncode(pkt, gSelfId, self.species, self.shiny, self.sleeping, self.level);
  esp_now_send(BCAST, pkt, NB_BEACON_LEN);
}

void nearbyBegin() {
  // el id y la MAC cambian en cada arranque y no se guardan: un aparato no se puede seguir de un dia
  // a otro (el id solo sirve para no contar dos veces al mismo vecino durante un rato)
  do gSelfId = esp_random() & 0xFFFFFF; while (!gSelfId);
  esp_fill_random(gMac, 6);
  gMac[0] = (gMac[0] | 0x02) & 0xFE;  // administrada localmente, unicast
  gTable.clear();
  gTable.selfId = gSelfId;
  gNextWindow = millis() + 2000 + (esp_random() % 8000);
  gSearchUntil = millis() + NB_SEARCH_MS;  // al arrancar se busca un minuto: con otro aparato al lado se ven en segundos
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

// atiende los paquetes recibidos desde el ultimo pase
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
    // uno nuevo: se le contesta enseguida, para que el tambien nos vea aunque su ventana vaya a
    // cerrarse (como mucho una respuesta por segundo)
    if (r == NB_NEW && gRadioOn && now - gLastReply > 1000) {
      gLastReply = now;
      sendBeacon(self);
    }
  }
}

void nearbyPoll(uint32_t now, const PowerCtx &c, const NbSelf &self) {
  bool searching = nearbySearching();
  if (!searching) gSearchUntil = 0;
  NbMode mode = self.valid ? nbModeFor(c, true, searching) : NB_OFF;
  gMode = mode;
  drainRing(now, self);  // tambien con la radio apagada: lo que llego en la ultima ventana

  if (gRadioOn) {
    // la busqueda dura hasta que se acaba; el resto de ventanas, NB_WINDOW_MS
    bool end = mode == NB_OFF || (mode != NB_SEARCH && (int32_t)(now - gWindowEnd) >= 0);
    if (!end && (int32_t)(now - gNextBeacon) >= 0) {
      sendBeacon(self);
      if (mode == NB_SEARCH) gNextBeacon = now + NB_SEARCH_BEACON_MS;
      else if (++gBeaconIdx < NB_BEACON_COUNT) gNextBeacon = gWindowStart + nbBeaconAtMs(gBeaconIdx);
      else gNextBeacon = gWindowEnd;
    }
    if (end) {
      radioStop();
      gRadioOn = false;
      NbMode dm = (mode == NB_SEARCH || mode == NB_OFF) ? NB_ACTIVE : mode;
      gNextWindow = now + nbNextDelayMs(dm, esp_random());
    }
  } else if (mode != NB_OFF) {
    // si la pantalla se enciende, la proxima ventana no puede estar a minutos
    if (mode != NB_SEARCH) {
      uint32_t maxWait = nbPeriodMs(mode) * 5 / 4;
      if ((int32_t)(gNextWindow - now) > (int32_t)maxWait) gNextWindow = now + esp_random() % (maxWait + 1);
    }
    bool due = mode == NB_SEARCH || (int32_t)(now - gNextWindow) >= 0;
    if (due && (!gFailed || (int32_t)(now - gFailUntil) >= 0)) {
      if (radioStart()) {
        gRadioOn = true;
        gWindowStart = now;
        gWindowEnd = now + NB_WINDOW_MS;
        gBeaconIdx = 0;
        gNextBeacon = now;
      } else {
        gFailed = true;
        gFailUntil = now + 600000;  // la radio no arranco: se reintenta en 10 min
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
