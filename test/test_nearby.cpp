// Nearby TamaPokes (nearby.h): packet, neighbor table and radio schedule.
#include "framework.h"
#include "../nearby.h"
#include <vector>
#include <algorithm>
#include <stdint.h>

static NbBeacon beacon(uint32_t id, int species = 25, int level = 12) {
  NbBeacon b;
  b.species = (uint8_t)species; b.shiny = false; b.sleeping = false; b.level = (uint16_t)level; b.id = id; b.tick = NB_TICK_NONE;
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
  CHECK(!nbDecode(ok, NB_BEACON_LEN - 1, &b));          // too short
  CHECK(!nbDecode(ok, NB_BEACON_LEN + 1, &b));          // too long
  CHECK(!nbDecode(ok, 0, &b));
  for (int i = 0; i < NB_BEACON_LEN; i++) {              // any changed byte breaks the signature, crc or range
    uint8_t bad[NB_BEACON_LEN];
    memcpy(bad, ok, sizeof(bad));
    bad[i] ^= 0x5A;
    CHECK(!nbDecode(bad, NB_BEACON_LEN, &b));
  }
  // ranges with a valid crc: species, level, id and flags are checked separately
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
  CHECK(reject(25, 5, 1, 0x20));                         // unknown flag
}

TEST(nearby, el_decodificador_aguanta_basura_aleatoria) {
  uint32_t x = 12345;
  int accepted = 0;
  for (int i = 0; i < 200000; i++) {
    uint8_t d[NB_BEACON_LEN];
    for (auto &c : d) { x = x * 1664525u + 1013904223u; c = (uint8_t)(x >> 24); }
    NbBeacon b;
    if (nbDecode(d, NB_BEACON_LEN, &b)) accepted++;       // whatever gets through must be consistent
  }
  CHECK_EQ(accepted, 0);                                  // signature + version + crc let through ~nothing
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
  CHECK_EQ((int)t.accept(beacon(7, 26, 13), -50, 1600), (int)NB_UPDATED);  // updates species and level
  const NbEntry *s[NB_MAX_SHOWN];
  CHECK_EQ(t.shown(s), 1);
  CHECK_EQ((int)s[0]->species, 26);
}

TEST(nearby, limite_global_de_paquetes_por_segundo) {
  NbTable t; t.clear(); t.selfId = 1;
  int taken = 0;
  for (uint32_t id = 10; id < 200; id++)                  // 190 different devices in the same second
    if (t.accept(beacon(id), -60, 5000) != NB_IGNORED) taken++;
  CHECK_EQ(taken, NB_MAX_PPS);
  CHECK_EQ((int)t.accept(beacon(500), -60, 6001), (int)NB_NEW);  // the next second accepts again
}

TEST(nearby, la_tabla_llena_sustituye_al_mas_antiguo) {
  NbTable t; t.clear(); t.selfId = 1;
  for (uint32_t i = 0; i < NB_TABLE_SIZE; i++) t.accept(beacon(100 + i), -50, 1000 + i * 1000);
  t.accept(beacon(100), -50, 20000);                      // the first one is refreshed: it's no longer the oldest
  CHECK_EQ((int)t.accept(beacon(999), -50, 21000), (int)NB_NEW);
  bool has100 = false, has101 = false;
  for (auto &x : t.e) { has100 |= x.used && x.id == 100; has101 |= x.used && x.id == 101; }
  CHECK(has100);
  CHECK(!has101);                                         // the oldest one (101) is gone
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
  for (int i = 1; i < n; i++) CHECK(s[i]->id > s[i - 1]->id);                      // by ascending id
  for (int i = 0; i < n; i++) CHECK(s[i]->id >= 30);                              // the two oldest (10 and 20) are out
  t.accept(beacon(30), -50, 20000);                                               // a signal arrives from one: nobody changes place
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
  CHECK_EQ((int)nbModeFor(c, true, true), (int)NB_SEARCH);   // searching with the screen on
  c.screenOff = true;
  CHECK_EQ((int)nbModeFor(c, true, true), (int)NB_NIGHT);    // no searching with the screen off
  CHECK_EQ((int)nbModeFor(c, false, true), (int)NB_OFF);
  c = {}; c.batPct = 10;                                  // low battery: off
  CHECK_EQ((int)nbModeFor(c, true), (int)NB_OFF);
  CHECK_EQ((int)nbModeFor(c, true, true), (int)NB_OFF);
  c.usb = true;                                           // with USB there is no battery limit
  CHECK_EQ((int)nbModeFor(c, true), (int)NB_ACTIVE);
}

TEST(nearby, el_consumo_medio_de_la_radio_es_pequeno) {
  CHECK(nbDutyPermille(NB_ACTIVE) <= 40);                 // <= 4 % of the time with the radio on
  CHECK(nbDutyPermille(NB_DIM) <= 20);
  CHECK(nbDutyPermille(NB_NIGHT) <= 3);
  CHECK_EQ((int)nbDutyPermille(NB_SEARCH), 1000);        // active search is continuous, but lasts 60 s
  CHECK_EQ((int)nbDutyPermille(NB_OFF), 0);
  CHECK_EQ((int)nbNextDelayMs(NB_OFF, 123), 0);
  for (uint32_t r = 0; r < 100000; r += 37) {             // the delay stays within period +- 25 %
    uint32_t d = nbNextDelayMs(NB_ACTIVE, r);
    CHECK(d >= 25000 * 3 / 4 && d <= 25000 * 5 / 4);
  }
}

// ---- discovery simulation between two devices with random phases ----
struct SimDev {
  NbMode mode;
  uint64_t start;   // start of the current window (ms)
  uint32_t rng;
  uint32_t rnd() { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
  void advance() { start += NB_WINDOW_MS + nbNextDelayMs(mode, rnd()); }
};

// ms until each has heard the other (with the immediate reply on hearing a new one).
// A device in NB_SEARCH listens and sends nonstop.
static uint64_t discover(NbMode ma, NbMode mb, uint32_t seed) {
  SimDev a = { ma, 0, seed * 2654435761u + 1 }, b = { mb, 0, seed * 40503u + 7 };
  a.start = ma == NB_SEARCH ? 0 : a.rnd() % nbPeriodMs(ma);
  b.start = mb == NB_SEARCH ? 0 : b.rnd() % nbPeriodMs(mb);
  bool aHeardB = false, bHeardA = false;
  uint64_t limit = 3u * 3600u * 1000u;
  while (a.start < limit && b.start < limit) {
    SimDev &x = a.start <= b.start ? a : b;       // earliest window first
    SimDev &y = a.start <= b.start ? b : a;
    bool &xHeardY = (&x == &a) ? aHeardB : bHeardA;
    bool &yHeardX = (&x == &a) ? bHeardA : aHeardB;
    bool xSearch = x.mode == NB_SEARCH, ySearch = y.mode == NB_SEARCH;
    uint64_t xEnd = xSearch ? NB_SEARCH_MS : x.start + NB_WINDOW_MS;
    uint64_t yEnd = ySearch ? NB_SEARCH_MS : y.start + NB_WINDOW_MS;
    int n = xSearch ? (int)(NB_SEARCH_MS / NB_SEARCH_BEACON_MS) : NB_BEACON_COUNT;
    for (int i = 0; i < n; i++) {
      uint64_t t = x.start + (xSearch ? (uint64_t)i * NB_SEARCH_BEACON_MS : nbBeaconAtMs(i));
      if (t >= y.start && t <= yEnd) {              // y is listening
        yHeardX = true;
        if (t + 20 <= xEnd) xHeardY = true;          // y replies instantly and x is still listening
      }
    }
    if (aHeardB && bHeardA) return (x.start > y.start ? x.start : y.start);
    if (xSearch) { x.start = limit; continue; }      // active search is a single window
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
  CHECK(med < 420000);        // median: ~6 min
  CHECK(p90 < 1400000);       // 9 out of 10: ~21 min
}

TEST(nearby, con_la_pantalla_atenuada_tarda_mas_pero_se_descubren) {
  uint64_t med, p90;
  stats(NB_DIM, NB_DIM, &med, &p90);
  CHECK(med < 1700000);       // median: ~25 min
  CHECK(p90 < 5800000);
  stats(NB_ACTIVE, NB_DIM, &med, &p90);
  CHECK(med < 700000);
}

TEST(nearby, la_busqueda_activa_encuentra_al_otro_en_segundos_si_los_dos_buscan) {
  uint64_t med, p90;
  stats(NB_SEARCH, NB_SEARCH, &med, &p90);
  CHECK(p90 < 1000);          // both search at once: under 1 s
}

TEST(nearby, la_busqueda_activa_encuentra_a_uno_que_solo_escucha_por_ventanas) {
  uint64_t med, p90;
  stats(NB_SEARCH, NB_ACTIVE, &med, &p90);
  CHECK(p90 <= 26000);        // as soon as the other opens a window (<= 31.25 s) + the signal time
  stats(NB_SEARCH, NB_DIM, &med, &p90);
  CHECK(p90 <= 60000);        // the search lasts a minute: it reaches a window of dim mode (<= 56 s)
}

TEST(nearby, el_tick_de_la_ventana_viaja_en_el_paquete) {
  uint8_t d[NB_BEACON_LEN];
  NbBeacon b;
  for (int t = 0; t <= 7; t++) {
    nbEncode(d, 77, 25, true, true, 9, t);
    CHECK(nbDecode(d, NB_BEACON_LEN, &b));
    CHECK_EQ((int)b.tick, t);
    CHECK(b.shiny && b.sleeping);
  }
  nbEncode(d, 77, 25, false, false, 9);
  CHECK(nbDecode(d, NB_BEACON_LEN, &b));
  CHECK_EQ((int)b.tick, (int)NB_TICK_NONE);
}

TEST(nearby, los_periodos_son_multiplos_de_la_rejilla_comun) {
  CHECK_EQ((int)(NB_GRID_MS % nbPeriodMs(NB_ACTIVE)), 0);
  CHECK_EQ((int)(NB_GRID_MS % nbPeriodMs(NB_DIM)), 0);
  CHECK_EQ((int)(NB_GRID_MS % nbPeriodMs(NB_NIGHT)), 0);
  CHECK_EQ((int)(nbPeriodMs(NB_DIM) % nbPeriodMs(NB_ACTIVE)), 0);
  CHECK_EQ((int)(nbPeriodMs(NB_NIGHT) % nbPeriodMs(NB_DIM)), 0);
}

// Two devices in windows at different instants hear each other once; from then on they must
// open their windows together (this is what keeps neighbors and their state fresh).
static void syncedWindows(NbMode ma, NbMode mb, uint32_t sa, uint32_t sb) {
  NbSync A, B;
  // exchange: A's window opened at sa, B's at sb; each hears the other's beacon (tick 0..4)
  uint32_t t = (sa > sb ? sa : sb) + 100;
  NbBeacon fromB = beacon(2); fromB.tick = (uint8_t)((t - sb) / NB_TICK_MS);
  NbBeacon fromA = beacon(1); fromA.tick = (uint8_t)((t - sa) / NB_TICK_MS);
  CHECK(A.hear(fromB, t, true, sa, t));
  CHECK(B.hear(fromA, t, true, sb, t));
  uint32_t wa = A.next(t + 800, nbPeriodMs(ma)), wb = B.next(t + 800, nbPeriodMs(mb));
  uint32_t pa0 = nbPeriodMs(ma), pb0 = nbPeriodMs(mb);
  while ((int32_t)(wa - wb) < -200) wa = A.next(wa + 800, pa0);
  while ((int32_t)(wb - wa) < -200) wb = B.next(wb + 800, pb0);
  for (int i = 0; i < 40; i++) {
    uint32_t hi = wa > wb ? wa : wb, lo = wa > wb ? wb : wa;
    CHECK(hi - lo < 200);                                  // windows (0.8 s) overlap almost fully
    uint32_t pa = nbPeriodMs(ma), pb = nbPeriodMs(mb);
    wa = A.next(wa + 800, pa); wb = B.next(wb + 800, pb);
    // the faster device opens in between: only compare the instants both use
    while ((int32_t)(wa - wb) < -200) wa = A.next(wa + 800, pa);
    while ((int32_t)(wb - wa) < -200) wb = B.next(wb + 800, pb);
  }
}

TEST(nearby, tras_oirse_los_dos_abren_la_ventana_a_la_vez) {
  syncedWindows(NB_ACTIVE, NB_ACTIVE, 10000, 10500);
  syncedWindows(NB_ACTIVE, NB_ACTIVE, 10700, 10000);
  syncedWindows(NB_ACTIVE, NB_DIM, 3000, 3600);
  syncedWindows(NB_DIM, NB_NIGHT, 123456, 123000);
}

TEST(nearby, la_busqueda_continua_y_una_ventana_se_alinean) {
  NbSync searcher, windowed;
  uint32_t winStart = 50000, t = 50320;
  NbBeacon fromW = beacon(2); fromW.tick = 2;                  // window open for 320 ms
  CHECK(searcher.hear(fromW, t, false, 0, t));                 // searching: no window of its own
  NbBeacon fromS = beacon(1);                                  // the searcher sends tick 7
  CHECK(windowed.hear(fromS, t + 5, true, winStart, t + 5));
  CHECK_EQ((long)searcher.anchor, (long)winStart);
  CHECK_EQ((long)windowed.anchor, (long)winStart);
  CHECK_EQ((long)searcher.next(t + 800, 25000), (long)(winStart + 25000));
}

TEST(nearby, sin_ventana_propia_ni_ajena_no_hay_sincronia) {
  NbSync a;
  CHECK(!a.hear(beacon(2), 1000, false, 0, 1000));             // both searching: nothing to align on
  CHECK(!a.live(1000));
}

TEST(nearby, una_rejilla_ajena_se_ignora_y_la_propia_caduca) {
  NbSync a;
  NbBeacon x = beacon(2); x.tick = 0;
  CHECK(a.hear(x, 100000, true, 100000, 100000));
  CHECK(!a.hear(x, 100000 + 12345, false, 0, 100000 + 12345));  // 12 s off the 25 s grid: other group
  CHECK(a.hear(x, 100000 + 25000 + 30, false, 0, 100000 + 25030)); // on grid (30 ms late)
  CHECK(a.live(100000 + NB_SYNC_LIFE_MS));
  CHECK(!a.live(100000 + 25030 + NB_SYNC_LIFE_MS + 1));
}

TEST(nearby, la_rejilla_sobrevive_al_desbordamiento_de_millis) {
  NbSync a;
  NbBeacon x = beacon(2); x.tick = 0;
  uint32_t t0 = 0xFFFFFFFFu - 60000u;
  CHECK(a.hear(x, t0, true, t0, t0));
  uint32_t n = a.next(t0 + 800, 25000);
  CHECK_EQ((long)(n - t0), 25000L);                              // across the wrap
}
