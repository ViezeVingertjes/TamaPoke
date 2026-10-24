// TamaPokes cercanos (nearby.h): paquete, tabla de vecinos y horario de la radio.
#include "framework.h"
#include "../nearby.h"
#include <vector>
#include <algorithm>
#include <stdint.h>

static NbBeacon beacon(uint32_t id, int species = 25, int level = 12) {
  NbBeacon b;
  b.species = (uint8_t)species; b.shiny = false; b.sleeping = false; b.level = (uint16_t)level; b.id = id;
  return b;
}

TEST(nearby, el_paquete_ida_y_vuelta) {
  uint8_t buf[NB_BEACON_LEN];
  CHECK_EQ(nbEncode(buf, 0xABCDEF, 25, true, true, 321), NB_BEACON_LEN);
  NbBeacon b;
  CHECK(nbDecode(buf, NB_BEACON_LEN, &b));
  CHECK_EQ((int)b.species, 25);
  CHECK(b.shiny && b.sleeping);
  CHECK_EQ((int)b.level, 321);
  CHECK_EQ((long)b.id, 0xABCDEFL);
}

TEST(nearby, el_decodificador_rechaza_todo_lo_que_no_sea_un_paquete_valido) {
  uint8_t ok[NB_BEACON_LEN];
  nbEncode(ok, 0x123456, 7, false, false, 5);
  NbBeacon b;
  CHECK(nbDecode(ok, NB_BEACON_LEN, &b));
  CHECK(!nbDecode(ok, NB_BEACON_LEN - 1, &b));          // corto
  CHECK(!nbDecode(ok, NB_BEACON_LEN + 1, &b));          // largo
  CHECK(!nbDecode(ok, 0, &b));
  for (int i = 0; i < NB_BEACON_LEN; i++) {              // cualquier byte cambiado rompe firma, crc o rango
    uint8_t bad[NB_BEACON_LEN];
    memcpy(bad, ok, sizeof(bad));
    bad[i] ^= 0x5A;
    CHECK(!nbDecode(bad, NB_BEACON_LEN, &b));
  }
  // rangos con crc valido: la especie, el nivel, el id y los flags se comprueban aparte
  auto reject = [&](int species, int level, uint32_t id, uint8_t flags) {
    uint8_t d[NB_BEACON_LEN];
    nbEncode(d, id, species, false, false, level);
    d[4] = flags;
    d[10] = nbCrc8(d, 10);
    NbBeacon x;
    return !nbDecode(d, NB_BEACON_LEN, &x);
  };
  CHECK(reject(0, 5, 1, 0));
  CHECK(reject(152, 5, 1, 0));
  CHECK(reject(255, 5, 1, 0));
  CHECK(reject(25, 0, 1, 0));
  CHECK(reject(25, 1000, 1, 0));
  CHECK(reject(25, 5, 0, 0));
  CHECK(reject(25, 5, 1, 4));                            // flag desconocido
}

TEST(nearby, el_decodificador_aguanta_basura_aleatoria) {
  uint32_t x = 12345;
  int accepted = 0;
  for (int i = 0; i < 200000; i++) {
    uint8_t d[NB_BEACON_LEN];
    for (auto &c : d) { x = x * 1664525u + 1013904223u; c = (uint8_t)(x >> 24); }
    NbBeacon b;
    if (nbDecode(d, NB_BEACON_LEN, &b)) accepted++;       // lo que pase tiene que ser coherente
  }
  CHECK_EQ(accepted, 0);                                  // la firma + version + crc dejan pasar ~nada
}

TEST(nearby, la_tabla_ignora_el_propio_aparato) {
  NbTable t; t.clear(); t.selfId = 99;
  CHECK_EQ((int)t.accept(beacon(99), -40, 1000), (int)NB_IGNORED);
  CHECK_EQ((int)t.accept(beacon(5), -40, 1000), (int)NB_NEW);
}

TEST(nearby, una_senal_repetida_no_cuenta_antes_del_intervalo) {
  NbTable t; t.clear(); t.selfId = 1;
  CHECK_EQ((int)t.accept(beacon(7), -50, 1000), (int)NB_NEW);
  CHECK_EQ((int)t.accept(beacon(7), -50, 1200), (int)NB_IGNORED);
  CHECK_EQ((int)t.accept(beacon(7, 26, 13), -50, 1600), (int)NB_UPDATED);  // actualiza especie y nivel
  const NbEntry *s[NB_MAX_SHOWN];
  CHECK_EQ(t.shown(s), 1);
  CHECK_EQ((int)s[0]->species, 26);
}

TEST(nearby, limite_global_de_paquetes_por_segundo) {
  NbTable t; t.clear(); t.selfId = 1;
  int taken = 0;
  for (uint32_t id = 10; id < 200; id++)                  // 190 aparatos distintos en el mismo segundo
    if (t.accept(beacon(id), -60, 5000) != NB_IGNORED) taken++;
  CHECK_EQ(taken, NB_MAX_PPS);
  CHECK_EQ((int)t.accept(beacon(500), -60, 6001), (int)NB_NEW);  // el segundo siguiente vuelve a aceptar
}

TEST(nearby, la_tabla_llena_sustituye_al_mas_antiguo) {
  NbTable t; t.clear(); t.selfId = 1;
  for (uint32_t i = 0; i < NB_TABLE_SIZE; i++) t.accept(beacon(100 + i), -50, 1000 + i * 1000);
  t.accept(beacon(100), -50, 20000);                      // el primero se renueva: ya no es el mas antiguo
  CHECK_EQ((int)t.accept(beacon(999), -50, 21000), (int)NB_NEW);
  bool has100 = false, has101 = false;
  for (auto &x : t.e) { has100 |= x.used && x.id == 100; has101 |= x.used && x.id == 101; }
  CHECK(has100);
  CHECK(!has101);                                         // se fue el mas antiguo (el 101)
}

TEST(nearby, los_vecinos_se_olvidan_a_los_cinco_minutos) {
  NbTable t; t.clear(); t.selfId = 1;
  t.accept(beacon(5), -50, 1000);
  CHECK_EQ(t.prune(1000 + NB_TTL_MS, NB_TTL_MS), 0);
  CHECK_EQ(t.prune(1000 + NB_TTL_MS + 1, NB_TTL_MS), 1);
  const NbEntry *s[NB_MAX_SHOWN];
  CHECK_EQ(t.shown(s), 0);
}

TEST(nearby, solo_se_muestran_cuatro_los_mas_recientes_con_orden_estable) {
  NbTable t; t.clear(); t.selfId = 1;
  for (uint32_t i = 0; i < 6; i++) t.accept(beacon(10 + i * 10), -50, 1000 + i * 2000);
  const NbEntry *s[NB_MAX_SHOWN];
  int n = t.shown(s);
  CHECK_EQ(n, NB_MAX_SHOWN);
  for (int i = 1; i < n; i++) CHECK(s[i]->id > s[i - 1]->id);                      // por id ascendente
  for (int i = 0; i < n; i++) CHECK(s[i]->id >= 30);                              // los dos mas viejos (10 y 20) fuera
  t.accept(beacon(30), -50, 20000);                                               // llega senal de uno: nadie cambia de sitio
  const NbEntry *s2[NB_MAX_SHOWN];
  CHECK_EQ(t.shown(s2), NB_MAX_SHOWN);
  for (int i = 0; i < n; i++) CHECK_EQ((long)s2[i]->id, (long)s[i]->id);
}

TEST(nearby, el_horario_depende_de_si_se_usa_el_aparato) {
  PowerCtx c = {};
  c.batPct = 80;
  CHECK_EQ((int)nbModeFor(c, true), (int)NB_ACTIVE);
  CHECK_EQ((int)nbModeFor(c, false), (int)NB_OFF);
  c.idleMs = 100000;
  CHECK_EQ((int)nbModeFor(c, true), (int)NB_DIM);
  c.idleMs = 0; c.sleeping = true;
  CHECK_EQ((int)nbModeFor(c, true), (int)NB_DIM);
  c = {}; c.batPct = 80; c.screenOff = true;
  CHECK_EQ((int)nbModeFor(c, true), (int)NB_NIGHT);
  c = {}; c.batPct = 80;
  CHECK_EQ((int)nbModeFor(c, true, true), (int)NB_SEARCH);   // buscando con la pantalla encendida
  c.screenOff = true;
  CHECK_EQ((int)nbModeFor(c, true, true), (int)NB_NIGHT);    // con la pantalla apagada no se busca
  CHECK_EQ((int)nbModeFor(c, false, true), (int)NB_OFF);
  c = {}; c.batPct = 10;                                  // poca bateria: apagado
  CHECK_EQ((int)nbModeFor(c, true), (int)NB_OFF);
  CHECK_EQ((int)nbModeFor(c, true, true), (int)NB_OFF);
  c.usb = true;                                           // con USB no hay limite de bateria
  CHECK_EQ((int)nbModeFor(c, true), (int)NB_ACTIVE);
}

TEST(nearby, el_consumo_medio_de_la_radio_es_pequeno) {
  CHECK(nbDutyPermille(NB_ACTIVE) <= 60);                 // <= 6 % del tiempo con la radio encendida
  CHECK(nbDutyPermille(NB_DIM) <= 20);
  CHECK(nbDutyPermille(NB_NIGHT) <= 7);
  CHECK_EQ((int)nbDutyPermille(NB_SEARCH), 1000);        // la busqueda activa es continua, pero dura 60 s
  CHECK_EQ((int)nbDutyPermille(NB_OFF), 0);
  CHECK_EQ((int)nbNextDelayMs(NB_OFF, 123), 0);
  for (uint32_t r = 0; r < 100000; r += 37) {             // el retraso se mantiene en periodo +- 25 %
    uint32_t d = nbNextDelayMs(NB_ACTIVE, r);
    CHECK(d >= 15000 * 3 / 4 && d <= 15000 * 5 / 4);
  }
}

// ---- simulacion de descubrimiento entre dos aparatos con fases al azar ----
struct SimDev {
  NbMode mode;
  uint64_t start;   // inicio de la ventana actual (ms)
  uint32_t rng;
  uint32_t rnd() { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
  void advance() { start += NB_WINDOW_MS + nbNextDelayMs(mode, rnd()); }
};

// ms hasta que cada uno ha oido al otro (con la respuesta inmediata al oir a uno nuevo).
// Un aparato en NB_SEARCH escucha y envia sin parar.
static uint64_t discover(NbMode ma, NbMode mb, uint32_t seed) {
  SimDev a = { ma, 0, seed * 2654435761u + 1 }, b = { mb, 0, seed * 40503u + 7 };
  a.start = ma == NB_SEARCH ? 0 : a.rnd() % nbPeriodMs(ma);
  b.start = mb == NB_SEARCH ? 0 : b.rnd() % nbPeriodMs(mb);
  bool aHeardB = false, bHeardA = false;
  uint64_t limit = 3u * 3600u * 1000u;
  while (a.start < limit && b.start < limit) {
    SimDev &x = a.start <= b.start ? a : b;       // la ventana mas temprana primero
    SimDev &y = a.start <= b.start ? b : a;
    bool &xHeardY = (&x == &a) ? aHeardB : bHeardA;
    bool &yHeardX = (&x == &a) ? bHeardA : aHeardB;
    bool xSearch = x.mode == NB_SEARCH, ySearch = y.mode == NB_SEARCH;
    uint64_t xEnd = xSearch ? NB_SEARCH_MS : x.start + NB_WINDOW_MS;
    uint64_t yEnd = ySearch ? NB_SEARCH_MS : y.start + NB_WINDOW_MS;
    int n = xSearch ? (int)(NB_SEARCH_MS / NB_SEARCH_BEACON_MS) : NB_BEACON_COUNT;
    for (int i = 0; i < n; i++) {
      uint64_t t = x.start + (xSearch ? (uint64_t)i * NB_SEARCH_BEACON_MS : nbBeaconAtMs(i));
      if (t >= y.start && t <= yEnd) {              // y esta escuchando
        yHeardX = true;
        if (t + 20 <= xEnd) xHeardY = true;          // y responde al instante y x aun escucha
      }
    }
    if (aHeardB && bHeardA) return (x.start > y.start ? x.start : y.start);
    if (xSearch) { x.start = limit; continue; }      // la busqueda activa es una sola ventana
    x.advance();
  }
  return limit;
}

static void stats(NbMode ma, NbMode mb, uint64_t *median, uint64_t *p90) {
  std::vector<uint64_t> v;
  for (uint32_t s = 1; s <= 2000; s++) v.push_back(discover(ma, mb, s));
  std::sort(v.begin(), v.end());
  *median = v[v.size() / 2];
  *p90 = v[v.size() * 9 / 10];
}

TEST(nearby, dos_aparatos_en_uso_se_descubren_en_pocos_minutos) {
  uint64_t med, p90;
  stats(NB_ACTIVE, NB_ACTIVE, &med, &p90);
  CHECK(med < 200000);        // mediana: ~3 min
  CHECK(p90 < 640000);        // 9 de cada 10: ~10 min
}

TEST(nearby, con_la_pantalla_atenuada_tarda_mas_pero_se_descubren) {
  uint64_t med, p90;
  stats(NB_DIM, NB_DIM, &med, &p90);
  CHECK(med < 1700000);       // mediana: ~25 min
  CHECK(p90 < 5800000);
  stats(NB_ACTIVE, NB_DIM, &med, &p90);
  CHECK(med < 520000);
}

TEST(nearby, la_busqueda_activa_encuentra_al_otro_en_segundos_si_los_dos_buscan) {
  uint64_t med, p90;
  stats(NB_SEARCH, NB_SEARCH, &med, &p90);
  CHECK(p90 < 1000);          // los dos buscan a la vez: menos de 1 s
}

TEST(nearby, la_busqueda_activa_encuentra_a_uno_que_solo_escucha_por_ventanas) {
  uint64_t med, p90;
  stats(NB_SEARCH, NB_ACTIVE, &med, &p90);
  CHECK(p90 <= 20000);        // en cuanto el otro abre una ventana (<= 18,75 s) + el tiempo de la senal
  stats(NB_SEARCH, NB_DIM, &med, &p90);
  CHECK(p90 <= 60000);        // la busqueda dura un minuto: alcanza a una ventana del modo atenuado (<= 56 s)
}
