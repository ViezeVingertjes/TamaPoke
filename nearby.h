#pragma once
#include <stdint.h>
#include <string.h>
#include "power.h"

// TamaPokes cercanos por ESP-NOW (opcional). Esta cabecera es el protocolo, la tabla de
// vecinos y el horario de la radio como funciones puras (se prueban en el PC, ver
// test/test_nearby.cpp). La radio de verdad esta en nearby.cpp.
//
// Consumo: escuchar es lo que cuesta (~100 mA con la radio encendida), enviar un
// paquete de 11 bytes casi nada. Por eso la radio solo se enciende en ventanas cortas y
// el horario depende de que se este usando el aparato: ver nbPeriodMs().

#define NB_MAGIC0 'T'
#define NB_MAGIC1 'P'
#define NB_VERSION 1
#define NB_BEACON_LEN 11
#define NB_TABLE_SIZE 8       // vecinos recordados
#define NB_MAX_SHOWN 4        // vecinos que se dibujan junto al bicho
#define NB_TTL_MS 300000UL    // un vecino sin senales durante 5 min se olvida
#define NB_MAX_PPS 30         // paquetes aceptados por segundo (el resto se descarta)
#define NB_REPEAT_MS 500      // dos senales del mismo vecino con menos de esto de diferencia no cuentan

// ---- formato del paquete (11 bytes) ----
//  0 'T'   1 'P'   2 version   3 especie (1-151)   4 flags (bit 0 shiny, bit 1 duerme)
//  5-6 nivel (little endian)   7-9 id del aparato (24 bits, distinto de 0)   10 crc8 de los 10 anteriores
// Solo viajan datos que el otro aparato ya veria si estuviera al lado: ni apodo ni nada personal.
struct NbBeacon {
  uint8_t species;
  bool shiny, sleeping;
  uint16_t level;
  uint32_t id;  // 24 bits, distinto de 0
};

inline uint8_t nbCrc8(const uint8_t *d, int n) {
  uint8_t c = 0;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

inline int nbEncode(uint8_t *out, uint32_t id, int species, bool shiny, bool sleeping, int level) {
  out[0] = NB_MAGIC0;
  out[1] = NB_MAGIC1;
  out[2] = NB_VERSION;
  out[3] = (uint8_t)species;
  out[4] = (uint8_t)((shiny ? 1 : 0) | (sleeping ? 2 : 0));
  out[5] = (uint8_t)(level & 0xFF);
  out[6] = (uint8_t)(level >> 8);
  out[7] = (uint8_t)(id & 0xFF);
  out[8] = (uint8_t)((id >> 8) & 0xFF);
  out[9] = (uint8_t)((id >> 16) & 0xFF);
  out[10] = nbCrc8(out, 10);
  return NB_BEACON_LEN;
}

// Valida un paquete recibido de cualquiera (es difusion sin cifrar): longitud, firma,
// version, crc y rangos. Solo lo que pasa esto llega a la tabla ni al dibujo.
inline bool nbDecode(const uint8_t *d, int len, NbBeacon *out) {
  if (len != NB_BEACON_LEN || d[0] != NB_MAGIC0 || d[1] != NB_MAGIC1 || d[2] != NB_VERSION) return false;
  if (nbCrc8(d, 10) != d[10]) return false;
  int species = d[3];
  int level = d[5] | (d[6] << 8);
  uint32_t id = (uint32_t)d[7] | ((uint32_t)d[8] << 8) | ((uint32_t)d[9] << 16);
  if (species < 1 || species > 151 || level < 1 || level > 999 || id == 0 || (d[4] & ~3)) return false;
  out->species = (uint8_t)species;
  out->shiny = d[4] & 1;
  out->sleeping = d[4] & 2;
  out->level = (uint16_t)level;
  out->id = id;
  return true;
}

// ---- tabla de vecinos ----
struct NbEntry {
  bool used;
  bool shiny, sleeping;
  uint8_t species;
  int8_t rssi;
  uint16_t level;
  uint32_t id;
  uint32_t lastSeen;  // ms
  uint32_t lastCount; // ms de la ultima senal aceptada
};

enum NbResult : uint8_t { NB_IGNORED = 0, NB_UPDATED, NB_NEW };

struct NbTable {
  NbEntry e[NB_TABLE_SIZE];
  uint32_t selfId = 0;
  uint32_t secStart = 0;
  uint16_t secCount = 0;

  void clear() { memset(e, 0, sizeof(e)); secStart = 0; secCount = 0; }

  // Procesa una senal valida. Descarta las propias, las que llegan demasiado seguidas (global y
  // por vecino) y, si la tabla esta llena, sustituye al mas antiguo.
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

  // olvida a los que llevan mas de ttl sin senales; devuelve cuantos se fueron
  int prune(uint32_t now, uint32_t ttl = NB_TTL_MS) {
    int gone = 0;
    for (auto &x : e)
      if (x.used && (int32_t)(now - x.lastSeen) > (int32_t)ttl) { x.used = false; gone++; }
    return gone;
  }

  // hasta NB_MAX_SHOWN vecinos: los vistos mas recientemente, ordenados por id para que cada
  // uno conserve su sitio aunque lleguen senales de otros
  int shown(const NbEntry **out) const {
    const NbEntry *pick[NB_TABLE_SIZE];
    int n = 0;
    for (auto &x : e) if (x.used) pick[n++] = &x;
    for (int i = 1; i < n; i++)  // por lastSeen descendente (insercion)
      for (int j = i; j > 0 && (int32_t)(pick[j]->lastSeen - pick[j - 1]->lastSeen) > 0; j--) {
        const NbEntry *t = pick[j]; pick[j] = pick[j - 1]; pick[j - 1] = t;
      }
    if (n > NB_MAX_SHOWN) n = NB_MAX_SHOWN;
    for (int i = 1; i < n; i++)  // por id ascendente
      for (int j = i; j > 0 && pick[j]->id < pick[j - 1]->id; j--) {
        const NbEntry *t = pick[j]; pick[j] = pick[j - 1]; pick[j - 1] = t;
      }
    for (int i = 0; i < n; i++) out[i] = pick[i];
    return n;
  }
};

// ---- horario de la radio ----
enum NbMode : uint8_t { NB_OFF = 0, NB_ACTIVE, NB_DIM, NB_NIGHT, NB_SEARCH };

// ms entre ventanas de radio: mas seguido mientras alguien usa el aparato, y casi nada con la
// pantalla apagada. Cada ventana dura NB_WINDOW_MS. NB_SEARCH es la busqueda activa: la radio
// escucha seguido durante NB_SEARCH_MS al activar la funcion o al tocar su boton (con otro
// aparato al lado, se ven en segundos); cuesta ~100 mA durante ese minuto y nada despues.
#define NB_WINDOW_MS 800
#define NB_SEARCH_MS 60000UL
inline uint32_t nbPeriodMs(NbMode m) {
  switch (m) {
    case NB_ACTIVE: return 15000;
    case NB_DIM: return 45000;
    case NB_NIGHT: return 120000;
    default: return 0;  // NB_OFF no tiene ventanas; NB_SEARCH es una sola continua
  }
}
// cuanto tiempo esta la radio encendida, en milesimas del tiempo total (para el consumo medio)
inline uint32_t nbDutyPermille(NbMode m) {
  return m == NB_SEARCH ? 1000 : m ? NB_WINDOW_MS * 1000UL / nbPeriodMs(m) : 0;
}

// searching: hay una busqueda activa en curso (solo con la pantalla encendida)
inline NbMode nbModeFor(const PowerCtx &c, bool enabled, bool searching = false) {
  if (!enabled || batteryLow(c)) return NB_OFF;   // con poca bateria no se gasta en esto
  if (c.screenOff) return NB_NIGHT;
  if (searching) return NB_SEARCH;
  if (c.sleeping || dimStageFor(c.idleMs) != DIM_AWAKE) return NB_DIM;
  return NB_ACTIVE;
}

// ms hasta la proxima ventana: el periodo +-25 % al azar, para que dos aparatos no queden
// sincronizados para siempre sin oirse (ni se pisen siempre)
inline uint32_t nbNextDelayMs(NbMode m, uint32_t rnd) {
  uint32_t p = nbPeriodMs(m);
  return p ? p - p / 4 + rnd % (p / 2 + 1) : 0;
}

// instantes (ms desde el inicio de la ventana) en los que se envia una senal: al abrir, y
// repetida dos veces por si el otro abre su ventana a medias
#define NB_BEACON_COUNT 5
inline uint32_t nbBeaconAtMs(int i) { return (uint32_t)i * 160; }
#define NB_SEARCH_BEACON_MS 500  // en la busqueda activa, una senal cada tanto
