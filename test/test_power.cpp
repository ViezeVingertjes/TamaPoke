// Power policy (power.h).
#include "framework.h"
#include "../power.h"

static PowerCtx ctx(uint32_t idle) {
  PowerCtx c = {};
  c.idleMs = idle;
  c.usb = false;
  c.batPct = 80;
  return c;
}

TEST(power, idleSince_no_desborda_si_el_toque_es_posterior_al_reloj_leido) {
  CHECK_EQ((int)idleSince(1000, 400), 600);
  CHECK_EQ((int)idleSince(1000, 1000), 0);
  CHECK_EQ((int)idleSince(1000, 1002), 0);               // the touch was handled 2 ms after reading the clock
  CHECK_EQ((int)dimStageFor(idleSince(1000, 1002)), (int)DIM_AWAKE);
  CHECK_EQ((int)idleSince(5, 0xFFFFFFF0u), 21);
  CHECK(idleSince(0x90000000u, 0) == 0x90000000u);       // 28 days untouched: not mistaken for a future touch          // the 49-day clock wraparound
}

TEST(power, los_escalones_de_atenuacion_son_los_de_siempre) {
  CHECK_EQ((int)dimStageFor(0), (int)DIM_AWAKE);
  CHECK_EQ((int)dimStageFor(90000), (int)DIM_AWAKE);
  CHECK_EQ((int)dimStageFor(90001), (int)DIM_LOW);
  CHECK_EQ((int)dimStageFor(300000), (int)DIM_LOW);
  CHECK_EQ((int)dimStageFor(300001), (int)DIM_OFF);
}

TEST(power, el_brillo_no_cambia_con_bateria_normal) {
  PowerCtx c = ctx(0);
  CHECK_EQ((int)brightnessFor(c), 145);       // battery
  c.usb = true;
  CHECK_EQ((int)brightnessFor(c), 180);       // USB
  c = ctx(100000);
  CHECK_EQ((int)brightnessFor(c), 60);        // dimmed
  c.sleeping = true;
  CHECK_EQ((int)brightnessFor(c), 10);
  c = ctx(400000);
  CHECK_EQ((int)brightnessFor(c), 8);
  c = ctx(0);
  c.screenOff = true;
  CHECK_EQ((int)brightnessFor(c), 0);
}

TEST(power, con_poca_bateria_el_brillo_tiene_tope_y_no_sube_nunca) {
  PowerCtx c = ctx(0);
  c.batPct = 15;
  CHECK_EQ((int)brightnessFor(c), 100);
  c.batPct = 5;
  CHECK_EQ((int)brightnessFor(c), 60);
  c.charging = true;                           // charging: no restrictions
  CHECK_EQ((int)brightnessFor(c), 145);
  c = ctx(100000);
  c.batPct = 5;
  CHECK_EQ((int)brightnessFor(c), 60);         // the cap doesn't raise a brightness that is already lower
  c = ctx(400000);
  c.batPct = 5;
  CHECK_EQ((int)brightnessFor(c), 8);
}

TEST(power, el_periodo_de_frame_crece_con_la_inactividad_y_nunca_es_cero_con_pantalla_encendida) {
  uint32_t prev = 0;
  for (uint32_t idle = 0; idle <= 600000; idle += 1000) {
    PowerCtx c = ctx(idle);
    uint32_t ms = frameIntervalMs(c);
    CHECK(ms >= 100);
    CHECK(ms >= prev);
    prev = ms;
  }
  CHECK_EQ((int)frameIntervalMs(ctx(0)), 100);        // active: 10 fps as always
  CHECK_EQ((int)frameIntervalMs(ctx(70000)), 150);
  CHECK_EQ((int)frameIntervalMs(ctx(100000)), 250);
  CHECK_EQ((int)frameIntervalMs(ctx(400000)), 1000);
}

TEST(power, el_juego_y_la_pantalla_apagada_mandan_sobre_el_resto) {
  PowerCtx g = ctx(200000);
  g.gameOpen = true;
  CHECK_EQ((int)frameIntervalMs(g), 85);               // the physics is tuned for 85 ms
  g.batPct = 5;
  CHECK_EQ((int)frameIntervalMs(g), 85);
  PowerCtx off = ctx(0);
  off.screenOff = true;
  CHECK_EQ((int)frameIntervalMs(off), 0);
}

TEST(power, dormido_y_con_poca_bateria_se_dibuja_menos) {
  PowerCtx c = ctx(0);
  c.sleeping = true;
  CHECK_EQ((int)frameIntervalMs(c), 100);              // asleep but in use: smooth
  c.idleMs = 70000;
  CHECK_EQ((int)frameIntervalMs(c), 500);              // asleep and untouched: almost nothing moves
  c = ctx(0);
  c.batPct = 15;
  CHECK_EQ((int)frameIntervalMs(c), 200);
  c.usb = true;                                        // with USB there is no battery saving
  CHECK_EQ((int)frameIntervalMs(c), 100);
}

TEST(power, el_amplificador_solo_esta_encendido_con_el_bicho_despierto_y_activo) {
  PowerCtx c = ctx(1000);
  CHECK(amplifierWanted(c));
  c.sleeping = true;
  CHECK(!amplifierWanted(c));
  c = ctx(100000);
  CHECK(!amplifierWanted(c));
  c = ctx(0);
  c.screenOff = true;
  CHECK(!amplifierWanted(c));
}

TEST(power, light_sleep_con_la_pantalla_apagada_o_casi_apagada_y_sin_usb) {
  PowerCtx c = ctx(0);
  CHECK(!lightSleepAllowed(c));                        // in use: no
  c.idleMs = 100000;
  CHECK(!lightSleepAllowed(c));                        // dimmed: not yet
  c.idleMs = 400000;
  CHECK(lightSleepAllowed(c));                         // almost off: yes
  c.usb = true;
  CHECK(!lightSleepAllowed(c));                        // never with USB
  c = ctx(0);
  c.screenOff = true;
  CHECK(lightSleepAllowed(c));
  c.usb = true;
  CHECK(!lightSleepAllowed(c));
  CHECK_EQ((int)idleDelayMs(c), 20);
  c.screenOff = false;
  CHECK_EQ((int)idleDelayMs(c), 1);
}

TEST(power, la_bateria_se_lee_menos_veces_con_la_pantalla_apagada) {
  CHECK_EQ((int)powerCacheMs(ctx(0)), 2000);
  CHECK_EQ((int)powerCacheMs(ctx(400000)), 10000);
  PowerCtx off = ctx(0);
  off.screenOff = true;
  CHECK_EQ((int)powerCacheMs(off), 10000);
}

// Scenario: the critter is touched and left for 10 minutes. Flushed frames are counted (each one
// is 466*466*2 bytes over QSPI) along with the loop wakeups, with the old policy
// (always 10 fps, 1 ms pause) and with the new one.
TEST(power, diez_minutos_sin_tocar_vuelcan_mucho_menos_que_antes) {
  const uint32_t frameBytes = 466u * 466u * 2u;
  uint64_t before = 0, after = 0;
  uint32_t lastAfter = 0;
  for (uint32_t t = 0; t < 600000; t += 10) {
    if (t % 100 == 0) before += frameBytes;           // before: one frame every 100 ms
    uint32_t iv = frameIntervalMs(ctx(t));
    if (iv && t - lastAfter >= iv) { after += frameBytes; lastAfter = t; }
  }
  double ratio = (double)after / (double)before;
  CHECK(ratio < 0.35);                                 // at least 65 % less data over QSPI (comes out ~0.32)
}

TEST(power, con_la_pantalla_apagada_y_sin_usb_el_bucle_despierta_cinco_veces_por_segundo) {
  PowerCtx c = ctx(0);
  c.screenOff = true;
  CHECK(lightSleepAllowed(c));
  CHECK_EQ(1000 / POWER_SLEEP_MS, 5);                  // before: 1000 wakeups per second
}
