// Tests of the pet logic (pet.cpp) on top of the host shims.
#include "framework.h"
#include "shim/Arduino.h"
#include "../pet.h"
#include "../dex.h"
#include <string.h>

// ---------------------------------------------------------------- helpers
static void advance(Pet &p, uint32_t minutes) {
  for (uint32_t i = 0; i < minutes; i++) {
    mockAdvanceMillis(PET_TICK_MS);
    p.update(millis());
  }
}

// freshly hatched pet of the requested species, with a clean NVS
static void makePet(Pet &p, int16_t dex) {
  mockNvsReset();
  mockSetMillis(0);
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(dex);
  p.eggTap();
  p.eggTap();
  p.eggTap();  // 3 taps = instant hatch
}

static void setStats(Pet &p, uint8_t f, uint8_t j, uint8_t e, uint8_t h) {
  p.fullness = f; p.joy = j; p.energy = e; p.hygiene = h;
}

// ---------------------------------------------------------------- egg
TEST(egg, primera_partida_pide_inicial) {
  mockNvsReset();
  Pet p;
  p.begin();
  CHECK(p.isEgg());
  CHECK(p.awaitingStarter());
  CHECK_EQ(p.eggCracks(), (uint8_t)0);
  CHECK_EQ(p.registeredCount(), (uint16_t)0);
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
}

// v1.3 regression: the egg hatched by itself after 3 min while the player
// was still choosing a starter, and the choice was lost
TEST(egg, no_eclosiona_mientras_se_elige_inicial) {
  mockNvsReset();
  Pet p;
  p.begin();
  CHECK(p.awaitingStarter());
  advance(p, 600);  // 10 hours of play
  CHECK_MSG(p.isEgg(), "el huevo NO debe eclosionar durante la eleccion");
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
  p.chooseStarter(7);
  CHECK(!p.awaitingStarter());
  advance(p, 3);
  CHECK(!p.isEgg());
  CHECK_EQ(p.speciesId, (int16_t)7);
}

TEST(egg, eclosiona_sola_a_los_3_minutos) {
  mockNvsReset();
  Pet p;
  p.begin();
  p.chooseStarter(1);
  advance(p, 2);
  CHECK(p.isEgg());
  advance(p, 1);
  CHECK(!p.isEgg());
  CHECK_EQ(p.speciesId, (int16_t)1);
}

TEST(egg, tres_toques_eclosionan) {
  mockNvsReset();
  Pet p;
  p.begin();
  p.chooseStarter(4);
  p.eggTap();
  CHECK_EQ(p.eggCracks(), (uint8_t)1);
  CHECK(p.isEgg());
  p.eggTap();
  CHECK_EQ(p.eggCracks(), (uint8_t)2);
  CHECK(p.isEgg());
  p.eggTap();
  CHECK(!p.isEgg());
  CHECK_EQ(p.speciesId, (int16_t)4);
}

TEST(egg, eclosionar_sortea_genes_y_resetea_al_individuo) {
  Pet p;
  makePet(p, 4);
  CHECK_RANGE(p.geneAtk, 90, 110);
  CHECK_RANGE(p.geneDef, 90, 110);
  CHECK_RANGE(p.geneSpe, 90, 110);
  CHECK_EQ(p.trAtk, (uint8_t)0);
  CHECK_EQ(p.trDef, (uint8_t)0);
  CHECK_EQ(p.trSpe, (uint8_t)0);
  CHECK_EQ(p.bond, (uint8_t)0);
  CHECK_EQ(p.berryKnown, false);
  CHECK_STREQ(p.nick, "");
  CHECK_MSG(p.isRegistered(4), "criar = registrar en la pokedex");
  CHECK_EQ(p.registeredCount(), (uint16_t)1);
}

TEST(egg, tocar_huevo_no_hace_nada_si_ya_eclosiono) {
  Pet p;
  makePet(p, 4);
  uint8_t cracks = p.eggCracks();
  p.eggTap();
  CHECK_EQ(p.eggCracks(), cracks);
  CHECK_EQ(p.speciesId, (int16_t)4);
}

TEST(egg, rareza_del_huevo_es_valida) {
  mockNvsReset();
  Pet p;
  p.begin();
  CHECK_RANGE(p.eggRarity(), (uint8_t)R_COMUN, (uint8_t)R_LEGENDARIO);
}

// ---------------------------------------------------------------- food
TEST(feed, baya_normal_sube_25_y_topa_en_100) {
  Pet p;
  makePet(p, 4);
  p.fullness = 50;
  uint8_t notLoved = (uint8_t)((p.speciesId + 1) % 3);
  p.feedBerry(notLoved);
  CHECK_EQ(p.fullness, (uint8_t)75);
  p.feedBerry(notLoved);
  CHECK_EQ(p.fullness, (uint8_t)100);  // 100 instead of 105
}

TEST(feed, baya_favorita_sube_mas_y_da_corazon) {
  Pet p;
  makePet(p, 4);  // 4 % 3 == 1 -> it likes the blue one
  CHECK(p.lovesBerry(1));
  CHECK(!p.lovesBerry(0));
  CHECK(!p.lovesBerry(2));
  p.fullness = 10;
  p.joy = 10;
  p.feedBerry(1);
  CHECK_EQ(p.fullness, (uint8_t)45);
  CHECK_EQ(p.joy, (uint8_t)20);
  CHECK(p.berryKnown);
  CHECK(p.showHeart());
  CHECK(p.eating());
}

TEST(feed, chuche_engorda) {
  Pet p;
  makePet(p, 4);
  p.fullness = 50;
  p.joy = 50;
  p.feedCandy();
  CHECK_EQ(p.fullness, (uint8_t)60);
  CHECK_EQ(p.joy, (uint8_t)62);
  CHECK_EQ(p.weight, (uint8_t)12);
}

TEST(feed, dormido_o_huevo_no_come) {
  Pet p;
  makePet(p, 4);
  p.fullness = 40;
  p.toggleLight();  // off to sleep
  CHECK(p.sleeping);
  p.feedBerry(1);
  p.feedCandy();
  CHECK_EQ(p.fullness, (uint8_t)40);

  Pet e;
  mockNvsReset();
  e.begin();
  e.chooseStarter(4);
  e.fullness = 40;
  e.feedBerry(0);
  CHECK_EQ(e.fullness, (uint8_t)40);
}

// ---------------------------------------------------------------- tick
TEST(tick, despierto_baja_comida_2_y_energia_1_por_minuto) {
  Pet p;
  makePet(p, 4);
  setStats(p, 100, 100, 100, 100);
  p.poops = 0;
  mockForceRandom(99);  // random(100) < 15 false: no poops
  advance(p, 10);
  mockClearForcedRandom();
  CHECK_EQ(p.fullness, (uint8_t)80);
  CHECK_EQ(p.energy, (uint8_t)90);
  CHECK_EQ(p.hygiene, (uint8_t)90);
  CHECK_EQ(p.joy, (uint8_t)90);
  CHECK_EQ(p.ageMinutes, (uint32_t)10);
  CHECK_EQ(p.poops, (uint8_t)0);
}

TEST(tick, las_barras_nunca_bajan_de_cero) {
  Pet p;
  makePet(p, 4);
  setStats(p, 5, 3, 2, 1);
  advance(p, 30);
  CHECK_EQ(p.fullness, (uint8_t)0);
  CHECK_EQ(p.joy, (uint8_t)0);
  CHECK_EQ(p.energy, (uint8_t)0);
  CHECK_EQ(p.hygiene, (uint8_t)0);
}

TEST(tick, dormir_recupera_energia_con_suelos) {
  Pet p;
  makePet(p, 4);
  setStats(p, 100, 100, 10, 100);
  p.toggleLight();
  advance(p, 20);
  CHECK_MSG(p.energy == 100, "durmiendo la energia sube 6/min hasta 100");
  CHECK_MSG(p.fullness > 80, "durmiendo el hambre baja ~4x mas lento");
  advance(p, 2000);  // sleeping a very long time: the floors protect
  CHECK_MSG(p.fullness >= 30, "suelo de comida durmiendo = 30");
  CHECK_MSG(p.joy >= 35, "suelo de felicidad durmiendo = 35");
  CHECK_MSG(p.hygiene >= 45, "suelo de higiene durmiendo = 45");
  CHECK_EQ(p.careMistakes, (uint8_t)0);
}

TEST(tick, sobrepeso_se_quema_solo) {
  Pet p;
  makePet(p, 4);
  p.weight = 30;
  advance(p, 30);
  CHECK_EQ(p.weight, (uint8_t)20);  // -1 every 3 minutes
}

TEST(tick, descuido_cuenta_una_vez_por_hora) {
  Pet p;
  makePet(p, 4);
  setStats(p, 0, 0, 0, 0);
  advance(p, 1);
  CHECK_EQ(p.careMistakes, (uint8_t)1);
  advance(p, 50);
  CHECK_MSG(p.careMistakes == 1, "el enfriamiento evita contar el mismo descuido cada minuto");
  advance(p, 11);
  CHECK_EQ(p.careMistakes, (uint8_t)2);
}

// v1.4 regression: neglect took 3 bond every 30 min and it was impossible
// to recover; now it takes 1 and never leaves it at 0
TEST(tick, descuido_enfria_el_vinculo_sin_arrasarlo) {
  Pet p;
  makePet(p, 4);
  p.bond = 50;
  setStats(p, 0, 0, 0, 0);
  advance(p, 1);
  CHECK_EQ(p.bond, (uint8_t)49);
  p.bond = 1;
  advance(p, 61);
  CHECK_MSG(p.bond == 1, "el vinculo no cae por debajo de 1");
}

TEST(tick, buen_cuidado_12h_forja_defensa) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.trDef, (uint8_t)0);
  for (int h = 0; h < 720; h++) {  // keep everything >= 40 for 12 h
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  CHECK_EQ(p.trDef, (uint8_t)1);
}

// ---------------------------------------------------------------- levels
TEST(level, un_nivel_por_hora_de_juego) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.level(), (uint8_t)1);
  p.ageMinutes = 59;
  CHECK_EQ(p.level(), (uint8_t)1);
  p.ageMinutes = 60;
  CHECK_EQ(p.level(), (uint8_t)2);
  p.ageMinutes = 24 * 60;
  CHECK_EQ(p.level(), (uint8_t)25);
}

// Fixed in v1.8: level() was uint8_t and wrapped around to 0 after ~10.6 days
TEST(level, no_desborda_con_mascotas_longevas) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 254 * 60;
  uint8_t before = p.level();
  CHECK_EQ(before, (uint8_t)255);
  p.ageMinutes = 255 * 60;  // level 256 -> should keep rising (or saturate)
  CHECK_MSG(p.level() >= before, "el nivel nunca deberia bajar al envejecer");
}

// ---------------------------------------------------------------- evolution
TEST(evolve, requiere_nivel_y_buen_cuidado) {
  Pet p;
  makePet(p, 4);  // CHARMANDER evolves at level 16
  p.ageMinutes = 14 * 60;  // level 15
  CHECK(!p.canEvolveNow());
  p.ageMinutes = 15 * 60;  // level 16
  CHECK(p.canEvolveNow());
  p.fullness = 39;  // one bar below 40 blocks it
  CHECK(!p.canEvolveNow());
  p.fullness = 40;
  CHECK(p.canEvolveNow());
}

TEST(evolve, cada_descuido_retrasa_un_nivel) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 15 * 60;  // level 16
  p.careMistakes = 1;
  CHECK(!p.canEvolveNow());
  p.ageMinutes = 16 * 60;  // level 17
  CHECK(p.canEvolveNow());
}

// Fixed in v1.8: the uint8_t cast of (evolveLevel + careMistakes) overflowed
TEST(evolve, muchos_descuidos_no_adelantan_la_evolucion) {
  Pet p;
  makePet(p, 4);       // CHARMANDER: evolves at level 16
  p.careMistakes = 250;  // 250 mistakes: should be extremely far from evolving
  p.ageMinutes = 9 * 60;  // level 10
  CHECK_MSG(!p.canEvolveNow(), "con 250 descuidos no puede evolucionar a nivel 10");
}

TEST(evolve, no_evoluciona_dormido_ni_de_huevo_ni_en_ceremonia) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 20 * 60;
  CHECK(p.canEvolveNow());
  p.toggleLight();
  CHECK(!p.canEvolveNow());
  p.toggleLight();
  p.startFarewell();
  CHECK(!p.canEvolveNow());

  Pet e;
  mockNvsReset();
  e.begin();
  CHECK(!e.canEvolveNow());
}

TEST(evolve, transforma_registra_y_anima) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 20 * 60;
  p.evolve();
  CHECK_EQ(p.speciesId, (int16_t)5);
  CHECK_EQ(p.prevSpeciesId, (int16_t)4);
  CHECK(p.isRegistered(5));
  CHECK(p.evolving());
  CHECK_RANGE(p.evolveT(), 0.0f, 1.0f);
  mockAdvanceMillis(EVOLVE_ANIM_MS + 1);
  CHECK(!p.evolving());
}

TEST(evolve, forma_final_no_evoluciona) {
  Pet p;
  makePet(p, 6);  // CHARIZARD
  p.ageMinutes = 200 * 60;
  CHECK(!p.canEvolveNow());
  p.evolve();
  CHECK_EQ(p.speciesId, (int16_t)6);
}

TEST(evolve, eevee_se_ramifica_en_134_136) {
  for (int seed = 1; seed <= 12; seed++) {
    randomSeed(seed);
    Pet p;
    makePet(p, DEX_EEVEE);
    p.ageMinutes = 40 * 60;
    CHECK(p.canEvolveNow());
    p.evolve();
    CHECK_RANGE(p.speciesId, (int16_t)134, (int16_t)136);
    CHECK(p.isRegistered(p.speciesId));
  }
}

TEST(evolve, boton_de_evolucion_se_pospone_al_declinar) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 15 * 60;  // level 16
  CHECK(p.wantEvolveButton());
  p.declineEvolve();
  CHECK_MSG(!p.wantEvolveButton(), "'mantener forma' quita el boton");
  CHECK_MSG(p.canEvolveNow(), "pero la evolucion sigue disponible");
  p.ageMinutes = 16 * 60;  // levels up: it is offered again
  CHECK(p.wantEvolveButton());
}

// ---------------------------------------------------------------- stats
TEST(stats, formula_base_por_genes_mas_nivel_y_entreno) {
  Pet p;
  makePet(p, 4);  // CHARMANDER: atk 52, def 43, spe 65
  p.geneAtk = p.geneDef = p.geneSpe = 100;
  p.trAtk = p.trDef = p.trSpe = 0;
  p.ageMinutes = 0;  // level 1
  CHECK_EQ(p.atkStat(), (uint16_t)(52 + 1));
  CHECK_EQ(p.defStat(), (uint16_t)(43 + 1));
  CHECK_EQ(p.speStat(), (uint16_t)(65 + 1));
  p.ageMinutes = 9 * 60;  // level 10
  p.trAtk = 20;
  CHECK_EQ(p.atkStat(), (uint16_t)(52 + 10 + 20));
  p.geneAtk = 110;
  CHECK_EQ(p.atkStat(), (uint16_t)(52 * 110 / 100 + 10 + 20));
}

TEST(stats, el_huevo_no_tiene_stats) {
  mockNvsReset();
  Pet p;
  p.begin();
  CHECK_EQ(p.atkStat(), (uint16_t)0);
  CHECK_EQ(p.defStat(), (uint16_t)0);
  CHECK_EQ(p.speStat(), (uint16_t)0);
}

TEST(train, saco_da_un_punto_cada_4_golpes_con_tope) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.trainStrength(0), (uint8_t)0);
  CHECK_EQ(p.trainStrength(40), (uint8_t)10);
  CHECK_EQ(p.trAtk, (uint8_t)10);
  CHECK_MSG(p.trainStrength(1000) == 18, "tope de 18 por sesion");
  CHECK_EQ(p.strHi, (uint16_t)1000);
}

// Fixed when porting the battery: trainStrength() returned the theoretical gain
TEST(train, el_saco_anuncia_lo_que_de_verdad_sube) {
  Pet p;
  makePet(p, 4);
  p.trAtk = 95;
  uint8_t before = p.trAtk;
  uint8_t gain = p.trainStrength(72);  // 72/4 = 18, but only 5 fit
  CHECK_EQ(gain, (uint8_t)(p.trAtk - before));
}

TEST(train, entrenamiento_topa_en_100) {
  Pet p;
  makePet(p, 4);
  for (int i = 0; i < 20; i++) p.trainStrength(80);
  CHECK_EQ(p.trAtk, (uint8_t)100);
}

TEST(train, el_saco_cansa_y_quema_peso) {
  Pet p;
  makePet(p, 4);
  p.weight = 40;
  p.energy = 80;
  p.fullness = 80;
  p.trainStrength(30);
  CHECK_EQ(p.energy, (uint8_t)68);
  CHECK_EQ(p.fullness, (uint8_t)75);
  CHECK_EQ(p.weight, (uint8_t)30);
  p.energy = 3;  // the floor keeps it from reaching zero
  p.trainStrength(30);
  CHECK_EQ(p.energy, (uint8_t)3);
}

TEST(play, minijuego_entrena_velocidad_y_guarda_record) {
  Pet p;
  makePet(p, 4);
  p.joy = 10;
  p.weight = 50;
  p.playResult(20);
  CHECK_EQ(p.trSpe, (uint8_t)4);
  CHECK_EQ(p.joy, (uint8_t)45);   // +5 +30 for scoring over 15
  CHECK_EQ(p.weight, (uint8_t)10);  // 50 - 20*2
  CHECK_EQ(p.gameHi, (uint16_t)20);
  p.playResult(5);
  CHECK_MSG(p.gameHi == 20, "un resultado peor no baja el record");
}

TEST(play, jugar_sube_felicidad_y_cansa) {
  Pet p;
  makePet(p, 4);
  setStats(p, 50, 50, 50, 50);
  p.play();
  CHECK_EQ(p.joy, (uint8_t)75);
  CHECK_EQ(p.energy, (uint8_t)40);
  CHECK_EQ(p.fullness, (uint8_t)45);
  CHECK(p.showHeart());
}

TEST(care, limpiar_quita_cacas_y_deja_higiene_a_tope) {
  Pet p;
  makePet(p, 4);
  p.poops = 3;
  p.hygiene = 10;
  p.clean();
  CHECK_EQ(p.poops, (uint8_t)0);
  CHECK_EQ(p.hygiene, (uint8_t)100);
}

TEST(care, mimar_sube_felicidad_y_vinculo) {
  Pet p;
  makePet(p, 4);
  p.joy = 50;
  p.caress();
  CHECK_EQ(p.joy, (uint8_t)55);
  CHECK_EQ(p.bond, (uint8_t)1);
  CHECK(p.showHeart());
}

TEST(care, el_vinculo_tiene_tope_diario) {
  Pet p;
  makePet(p, 4);
  for (int i = 0; i < 50; i++) p.play();  // +2 bond each time
  CHECK_MSG(p.bond <= 20, "tope de 20 puntos de vinculo al dia");
  CHECK_EQ(p.bond, (uint8_t)20);
}

TEST(mood, refleja_el_estado) {
  Pet p;
  makePet(p, 4);
  setStats(p, 80, 80, 80, 80);
  CHECK_EQ((int)p.mood(), (int)MOOD_HAPPY);
  p.joy = 20;
  CHECK_EQ((int)p.mood(), (int)MOOD_SAD);
  p.joy = 80;
  p.feedBerry(0);
  CHECK_EQ((int)p.mood(), (int)MOOD_EATING);
  mockAdvanceMillis(EAT_ANIM_MS + 1);
  p.toggleLight();
  CHECK_EQ((int)p.mood(), (int)MOOD_SLEEPING);
}

// ---------------------------------------------------------------- pokedex
TEST(dexreg, bitmap_cubre_1_a_151) {
  Pet p;
  makePet(p, 4);
  memset(p.dexReg, 0, sizeof(p.dexReg));
  CHECK_EQ(p.registeredCount(), (uint16_t)0);
  for (int16_t d = 1; d <= 151; d++) {
    p.dexReg[(d - 1) >> 3] |= (uint8_t)(1 << ((d - 1) & 7));
    CHECK(p.isRegistered(d));
  }
  CHECK_EQ(p.registeredCount(), (uint16_t)151);
}

TEST(dexreg, fuera_de_rango_no_esta_registrado) {
  Pet p;
  makePet(p, 4);
  memset(p.dexReg, 0xFF, sizeof(p.dexReg));
  CHECK(!p.isRegistered(0));
  CHECK(!p.isRegistered(-1));
  CHECK(!p.isRegistered(152));
  CHECK(!p.isRegistered(32767));
  CHECK_EQ(p.registeredCount(), (uint16_t)151);
}

TEST(dexreg, linea_incompleta_se_detecta) {
  Pet p;
  makePet(p, 1);  // BULBASAUR -> IVYSAUR -> VENUSAUR
  CHECK(p.lineHasUnregistered(1));
  p.dexReg[(2 - 1) >> 3] |= (uint8_t)(1 << ((2 - 1) & 7));
  p.dexReg[(3 - 1) >> 3] |= (uint8_t)(1 << ((3 - 1) & 7));
  CHECK_MSG(!p.lineHasUnregistered(1), "linea 1-2-3 completa");
}

TEST(dexreg, la_rama_de_eevee_cuenta_las_tres) {
  Pet p;
  makePet(p, DEX_EEVEE);
  CHECK(p.lineHasUnregistered(DEX_EEVEE));
  for (int16_t d = 134; d <= 136; d++)
    p.dexReg[(d - 1) >> 3] |= (uint8_t)(1 << ((d - 1) & 7));
  CHECK(!p.lineHasUnregistered(DEX_EEVEE));
}

TEST(egg, el_sorteo_siempre_da_una_especie_valida) {
  Pet p;
  makePet(p, 4);
  for (int i = 0; i < 400; i++) {
    randomSeed(i * 7919 + 1);
    int16_t d = p.pickEggSpecies();
    CHECK_RANGE(d, (int16_t)1, (int16_t)151);
    CHECK_MSG(DEX_TBL[d].rarity != R_EVO, "de un huevo nunca sale una forma evolucionada");
  }
}

TEST(egg, el_primer_huevo_es_un_inicial_clasico) {
  for (int seed = 1; seed <= 20; seed++) {
    randomSeed(seed);
    mockNvsReset();
    Pet p;
    p.begin();
    int16_t d = p.pickEggSpecies();
    bool classic = false;
    for (int i = 0; i < NUM_CLASSIC_DEX; i++)
      if (CLASSIC_DEX[i] == d) classic = true;
    CHECK_MSG(classic, "sin pokedex, solo salen iniciales clasicos");
  }
}

TEST(egg, una_escapada_castiga_la_rareza) {
  Pet p;
  makePet(p, 4);
  p.lastEnd = CER_RUNAWAY;
  for (int seed = 1; seed <= 60; seed++) {
    randomSeed(seed);
    int16_t d = p.pickEggSpecies();
    CHECK_MSG(DEX_TBL[d].rarity == R_COMUN, "tras una escapada solo salen comunes");
  }
}

// ---------------------------------------------------------------- streak
TEST(streak, el_primer_cuidado_del_dia_suma) {
  Pet p;
  makePet(p, 4);
  p.setClock(100u * 86400);
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)1);
  CHECK_EQ(p.bestStreak, (uint16_t)1);
  p.caress();
  CHECK_MSG(p.streak == 1, "el segundo cuidado del mismo dia no suma");
  p.setClock(101u * 86400);
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)2);
}

TEST(streak, un_dia_perdido_reinicia) {
  Pet p;
  makePet(p, 4);
  p.setClock(100u * 86400);
  p.caress();
  p.setClock(101u * 86400);
  p.caress();
  p.setClock(105u * 86400);  // gap of days
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)1);
  CHECK_MSG(p.bestStreak == 2, "el record de racha se conserva");
}

TEST(streak, sin_reloj_no_hay_racha) {
  Pet p;
  makePet(p, 4);
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)0);
}

TEST(streak, celebra_los_hitos) {
  Pet p;
  makePet(p, 4);
  for (uint32_t d = 100; d < 103; d++) {
    p.setClock(d * 86400);
    p.caress();
  }
  CHECK_EQ(p.streak, (uint16_t)3);
  CHECK_MSG(p.showMilestone(), "hito de 3 dias celebrado");
  CHECK_EQ(p.lastMilestone, (uint16_t)3);
}

TEST(bonus, racha_y_vinculo_mejoran_el_huevo) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.careBonus(), 0);
  p.streak = 30;
  p.bond = 100;
  CHECK_EQ(p.careBonus(), 14);
  p.streak = 300;  // the streak is capped at 30
  CHECK_EQ(p.careBonus(), 14);
}

// ---------------------------------------------------------------- medals
TEST(medals, nivel_10_da_medalla) {
  Pet p;
  makePet(p, 4);
  CHECK(!p.hasMedal(MED_LV10));
  p.ageMinutes = 9 * 60 - 1;
  advance(p, 1);
  CHECK(p.hasMedal(MED_LV10));
  CHECK(p.showMedal());
}

// not overweight, level 5 and zero mistakes
TEST(medals, en_forma_llega_al_nivel_5) {
  Pet p;
  makePet(p, 4);
  p.weight = 0;
  p.ageMinutes = 4 * 60 - 1;
  advance(p, 1);
  CHECK(p.hasMedal(MED_FIT));
  CHECK_EQ(p.totalMedals, (uint16_t)1);

  Pet q;
  makePet(q, 4);
  q.careMistakes = 1;   // one mistake rules it out
  q.ageMinutes = 4 * 60 - 1;
  advance(q, 1);
  CHECK(!q.hasMedal(MED_FIT));
}

TEST(medals, cada_medalla_nueva_suma_al_total) {
  Pet p;
  makePet(p, 4);
  p.weight = 0;
  p.ageMinutes = 9 * 60 - 1;
  advance(p, 1);  // crosses level 10: MED_LV10 + MED_FIT at once
  CHECK(p.hasMedal(MED_LV10));
  CHECK(p.hasMedal(MED_FIT));
  CHECK_MSG(p.totalMedals == 2, "el total cuenta cada bit ganado, no cada evento");
}

TEST(medals, forma_final_al_nacer) {
  Pet p;
  makePet(p, 143);  // SNORLAX is already a final form
  CHECK(p.hasMedal(MED_FINAL));
  CHECK_EQ(p.totalMedals, (uint16_t)1);
}

TEST(medals, la_baya_favorita_da_medalla) {
  Pet p;
  makePet(p, 4);
  p.feedBerry(1);  // its favorite
  CHECK(p.berryKnown);
  advance(p, 1);
  CHECK(p.hasMedal(MED_BERRY));
}

TEST(medals, el_total_acumula_entre_crianzas) {
  Pet p;
  makePet(p, 143);
  CHECK_EQ(p.totalMedals, (uint16_t)1);
  p.newEgg();
  p.chooseStarter(144);  // ARTICUNO, another final form
  p.eggTap(); p.eggTap(); p.eggTap();
  CHECK_MSG(p.medals == MED_FINAL, "las medallas del individuo se reinician");
  CHECK_MSG(p.totalMedals == 2, "el total del jugador se conserva");
}

// ---------------------------------------------------------------- ceremonies
TEST(ceremony, despedida_solo_en_forma_final_y_a_los_3_dias) {
  Pet p;
  makePet(p, 6);  // CHARIZARD, final form
  p.ageMinutes = FAREWELL_AGE_MIN - 1;
  CHECK(!p.canFarewellNow());
  p.ageMinutes = FAREWELL_AGE_MIN;
  CHECK(p.canFarewellNow());

  Pet q;
  makePet(q, 4);  // CHARMANDER is not a final form
  q.ageMinutes = FAREWELL_AGE_MIN * 3;
  CHECK(!q.canFarewellNow());
}

TEST(ceremony, la_despedida_deja_un_huevo_nuevo) {
  Pet p;
  makePet(p, 6);
  p.ageMinutes = FAREWELL_AGE_MIN;
  p.startFarewell();
  CHECK_EQ(p.ceremony, (uint8_t)CER_FAREWELL);
  CHECK_RANGE(p.ceremonyT(), 0.0f, 1.0f);
  mockAdvanceMillis(CEREMONY_MS + 1);
  p.update(millis());
  CHECK(p.isEgg());
  CHECK_EQ(p.ceremony, (uint8_t)CER_NONE);
  CHECK_EQ(p.lastEnd, (uint8_t)CER_FAREWELL);
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
}

TEST(ceremony, escaparse_pide_una_hora_de_abandono_total) {
  Pet p;
  makePet(p, 4);
  setStats(p, 0, 0, 0, 0);
  CHECK(!p.canRunawayNow());
  advance(p, RUNAWAY_TICKS - 1);
  CHECK(!p.canRunawayNow());
  advance(p, 1);
  CHECK(p.canRunawayNow());
}

TEST(ceremony, un_solo_cuidado_salva_del_abandono) {
  Pet p;
  makePet(p, 4);
  p.dbgRunawayReady();
  CHECK(p.canRunawayNow());
  p.feedBerry(0);
  advance(p, 1);
  CHECK_MSG(!p.canRunawayNow(), "comer resetea el contador de abandono");
}

TEST(ceremony, durante_la_ceremonia_no_se_puede_interactuar) {
  Pet p;
  makePet(p, 4);
  setStats(p, 50, 50, 50, 50);
  p.poops = 2;
  p.startRunaway();
  p.feedBerry(0);
  p.feedCandy();
  p.play();
  p.clean();
  p.caress();
  p.toggleLight();
  CHECK_EQ(p.fullness, (uint8_t)50);
  CHECK_EQ(p.joy, (uint8_t)50);
  CHECK_EQ(p.poops, (uint8_t)2);
  CHECK(!p.sleeping);
}

TEST(ceremony, soltar_marca_el_final_como_liberacion) {
  Pet p;
  makePet(p, 4);
  p.release();
  CHECK_EQ(p.ceremony, (uint8_t)CER_RELEASE);
  CHECK_EQ(p.lastEnd, (uint8_t)CER_RELEASE);
  mockAdvanceMillis(CEREMONY_MS + 1);
  p.update(millis());
  CHECK(p.isEgg());
}

TEST(ceremony, un_huevo_no_se_despide_ni_se_suelta) {
  mockNvsReset();
  Pet p;
  p.begin();
  p.startFarewell();
  p.release();
  p.startRunaway();
  CHECK_EQ(p.ceremony, (uint8_t)CER_NONE);
}

// ---------------------------------------------------------------- offline
TEST(offline, aplica_el_tiempo_apagado_con_suelo_de_15) {
  Pet p;
  makePet(p, 4);
  setStats(p, 80, 80, 80, 100);
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 600 * 60);  // 600 minutes powered off
  CHECK_EQ(p.ageMinutes, (uint32_t)600);
  CHECK_MSG(p.fullness == 15, "suelo de comida offline = 15");
  CHECK_MSG(p.energy == 15, "suelo de energia offline = 15");
  CHECK_MSG(p.joy == 15, "suelo de felicidad offline = 15");
  CHECK_MSG(p.hygiene == 15, "suelo de higiene offline = 15");
  CHECK_MSG(p.careMistakes == 0, "en ausencia no se acumulan descuidos");
  CHECK_EQ(p.poops, (uint8_t)2);  // one poop every 4 h
}

TEST(offline, tope_de_dos_semanas) {
  Pet p;
  makePet(p, 4);
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 90u * 86400);  // 90 days
  CHECK_EQ(p.ageMinutes, (uint32_t)(14u * 24 * 60));
}

TEST(offline, menos_de_dos_minutos_no_hace_nada) {
  Pet p;
  makePet(p, 4);
  setStats(p, 80, 80, 80, 100);
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 90);
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
  CHECK_EQ(p.fullness, (uint8_t)80);
}

TEST(offline, el_huevo_eclosiona_en_tu_ausencia) {
  Pet p;
  makePet(p, 4);
  p.newEgg();  // the pokedex already exists: no starter prompt
  CHECK(!p.awaitingStarter());
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 600);  // 10 minutes
  CHECK(!p.isEgg());
  CHECK_RANGE(p.speciesId, (int16_t)1, (int16_t)151);
}

TEST(offline, durmiendo_se_descansa_tambien_apagado) {
  Pet p;
  makePet(p, 4);
  setStats(p, 90, 90, 20, 90);
  p.toggleLight();
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 120 * 60);
  CHECK_EQ(p.energy, (uint8_t)100);
  CHECK_MSG(p.fullness >= 30, "suelos de sueno tambien offline");
  CHECK_MSG(p.poops == 0, "durmiendo no ensucia");
}

TEST(offline, sin_reloj_no_se_aplica_nada) {
  Pet p;
  makePet(p, 4);
  setStats(p, 80, 80, 80, 100);
  p.syncClock(0);
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
  CHECK_EQ(p.fullness, (uint8_t)80);
}

// ---------------------------------------------------------------- saving
TEST(save, el_estado_sobrevive_a_un_reinicio) {
  Pet a;
  makePet(a, 25);  // PIKACHU
  a.ageMinutes = 1234;
  a.streak = 9;
  a.bestStreak = 11;
  a.bond = 44;
  a.trAtk = 17;
  a.weight = 33;
  a.gameHi = 21;
  a.rename("SPARKY");  // rename persists the whole state

  Pet b;
  b.begin();  // same "NVS": simulates a reboot
  CHECK_EQ(b.speciesId, (int16_t)25);
  CHECK_EQ(b.ageMinutes, (uint32_t)1234);
  CHECK_EQ(b.streak, (uint16_t)9);
  CHECK_EQ(b.bestStreak, (uint16_t)11);
  CHECK_EQ(b.bond, (uint8_t)44);
  CHECK_EQ(b.trAtk, (uint8_t)17);
  CHECK_EQ(b.weight, (uint8_t)33);
  CHECK_EQ(b.gameHi, (uint16_t)21);
  CHECK_STREQ(b.nick, "SPARKY");
  CHECK(b.isRegistered(25));
  CHECK(!b.awaitingStarter());
}

TEST(save, el_apodo_se_recorta_a_11_caracteres) {
  Pet p;
  makePet(p, 4);
  p.rename("NOMBRE-LARGUISIMO-QUE-NO-CABE");
  CHECK_EQ((int)strlen(p.nick), 11);
  CHECK_STREQ(p.nick, "NOMBRE-LARG");
}

TEST(save, borrado_de_fabrica_deja_todo_como_nuevo) {
  Pet a;
  makePet(a, 4);
  a.factoryReset();
  Pet b;
  b.begin();
  CHECK(b.isEgg());
  CHECK(b.awaitingStarter());
  CHECK_EQ(b.registeredCount(), (uint16_t)0);
}

TEST(save, el_autoguardado_se_marca_y_se_vuelca) {
  Pet p;
  makePet(p, 4);
  CHECK(!p.savePending());
  advance(p, 5);
  CHECK_MSG(p.savePending(), "cada 5 ticks queda un guardado pendiente");
  p.flushSave();
  CHECK(!p.savePending());
}

// ---------------------------------------------------------------- save backup
TEST(backup, ida_y_vuelta_conserva_el_estado) {
  Pet a;
  makePet(a, 4);
  a.fullness = 47; a.joy = 33; a.bond = 12; a.streak = 5; a.gameHi = 321;
  a.rename("BACKUP");
  a.dexReg[3] = 0xA5;
  uint8_t blob[Pet::BACKUP_MAX_BYTES];
  size_t n = a.exportBackup(blob, sizeof(blob));
  CHECK(n > 16 && n <= sizeof(blob));

  Pet b;
  makePet(b, 7);
  CHECK(b.importBackup(blob, n));
  CHECK_EQ((int)b.speciesId, (int)a.speciesId);
  CHECK_EQ((int)b.fullness, 47);
  CHECK_EQ((int)b.joy, 33);
  CHECK_EQ((int)b.bond, 12);
  CHECK_EQ((int)b.streak, 5);
  CHECK_EQ((int)b.gameHi, 321);
  CHECK_EQ((int)b.dexReg[3], 0xA5);
  CHECK(strcmp(b.nick, "BACKUP") == 0);
}

TEST(backup, rechaza_datos_cortados_o_alterados_y_no_toca_el_estado) {
  Pet a;
  makePet(a, 4);
  uint8_t blob[Pet::BACKUP_MAX_BYTES];
  size_t n = a.exportBackup(blob, sizeof(blob));

  Pet b;
  makePet(b, 7);
  b.fullness = 61;
  CHECK(!b.importBackup(blob, n - 1));        // cut off
  uint8_t bad[Pet::BACKUP_MAX_BYTES];
  memcpy(bad, blob, n);
  bad[10] ^= 0x01;                            // flipped bit
  CHECK(!b.importBackup(bad, n));
  memcpy(bad, blob, n);
  bad[0] = 'X';                               // not a backup
  CHECK(!b.importBackup(bad, n));
  CHECK_EQ((int)b.fullness, 61);
  CHECK_EQ((int)b.speciesId, 7);
}

TEST(backup, un_valor_fuera_de_rango_con_checksum_valido_se_deshace) {
  Pet a;
  makePet(a, 4);
  a.fullness = 250;                           // impossible; export does not clamp
  uint8_t blob[Pet::BACKUP_MAX_BYTES];
  size_t n = a.exportBackup(blob, sizeof(blob));

  Pet b;
  makePet(b, 7);
  b.fullness = 61;
  CHECK(!b.importBackup(blob, n));
  CHECK_EQ((int)b.fullness, 61);              // rolled back
  CHECK_EQ((int)b.speciesId, 7);
}

TEST(backup, un_valor_absurdo_con_checksum_valido_se_rechaza_y_se_deshace) {
  Pet a;
  makePet(a, 4);
  uint8_t blob[Pet::BACKUP_MAX_BYTES];
  size_t n = a.exportBackup(blob, sizeof(blob));
  Pet b;
  makePet(b, 7);
  b.bond = 33;
  a.bond = 150;                                // impossible value; the export does not clamp
  size_t n2 = a.exportBackup(blob, sizeof(blob));
  CHECK(n2 == n);
  CHECK(!b.importBackup(blob, n2));
  CHECK_EQ((int)b.bond, 33);                   // rolled back
}

// ---------------------------------------------------------------- walking
// midday of a given day, to anchor the pet's clock
static void setDay(Pet &p, uint32_t day) { p.setClock(day * 86400UL + 43200); }

TEST(steps, los_pasos_se_acumulan_y_cambian_de_dia) {
  Pet p;
  makePet(p, 4);
  setDay(p, 20000);
  p.applyWalk(300);
  p.applyWalk(250);
  CHECK_EQ((int)p.stepsToday, 550);
  CHECK_EQ((int)p.stepsTotal, 550);
  setDay(p, 20001);
  advance(p, 1);                       // the next minute notices the new day
  CHECK_EQ((int)p.stepsToday, 0);
  CHECK_EQ((int)p.stepsTotal, 550);
  CHECK_EQ((int)p.stepRewardMask, 0);
}

TEST(steps, cada_meta_diaria_da_su_premio_una_sola_vez) {
  Pet p;
  makePet(p, 4);
  setDay(p, 20000);
  setStats(p, 50, 50, 50, 50);
  p.trAtk = 0;
  p.applyWalk(500);
  CHECK_EQ((int)p.fullness, 65);       // snack
  CHECK_EQ((int)p.stepRewardShown(), 0);
  p.applyWalk(100);
  CHECK_EQ((int)p.fullness, 65);       // not again
  p.energy = 50;
  p.applyWalk(1400);                   // 2000 total
  CHECK_EQ((int)p.energy, 75);
  p.applyWalk(3000);                   // 5000 total
  CHECK_EQ((int)p.trAtk, 5);
  CHECK_EQ((int)p.stepRewardMask, 7);
}

TEST(steps, caminar_anima_y_une_pero_con_tope_diario) {
  Pet p;
  makePet(p, 4);
  setDay(p, 20000);
  setStats(p, 50, 10, 50, 50);
  p.bond = 0;
  p.applyWalk(1000);
  CHECK_EQ((int)p.joy, 20);            // +1 per 100 steps
  CHECK_EQ((int)p.bond, 2);            // +1 per 500 steps
  p.applyWalk(4000);
  CHECK_EQ((int)p.joy, 30);            // beyond 2000 steps only the 5000 goal's workout counts
  CHECK_EQ((int)p.bond, 4);
}

TEST(steps, el_rango_del_sendero_sale_de_los_pasos_totales) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ((int)p.trailRank(), 0);
  p.stepsTotal = 9999;
  CHECK_EQ((int)p.trailRank(), 0);
  p.stepsTotal = 10000;
  CHECK_EQ((int)p.trailRank(), 1);
  p.stepsTotal = 250000;
  CHECK_EQ((int)p.trailRank(), Pet::TRAIL_RANKS);
}

TEST(steps, un_huevo_cuenta_pasos_pero_no_recibe_premios) {
  Pet p;
  mockNvsReset();
  mockSetMillis(0);
  p.begin();
  p.chooseStarter(4);
  setDay(p, 20000);
  p.fullness = 40;
  p.applyWalk(600);
  CHECK_EQ((int)p.stepsToday, 600);
  CHECK_EQ((int)p.fullness, 40);
  CHECK_EQ((int)p.stepRewardMask, 0);  // still owed: given once it hatches
}

TEST(steps, se_guardan_y_van_en_la_copia) {
  Pet a;
  makePet(a, 4);
  setDay(a, 20000);
  a.applyWalk(1234);
  a.flushSave();
  uint8_t blob[Pet::BACKUP_MAX_BYTES];
  size_t n = a.exportBackup(blob, sizeof(blob));
  CHECK(n > 0);
  Pet b;
  makePet(b, 7);
  CHECK(b.importBackup(blob, n));
  CHECK_EQ((int)b.stepsToday, 1234);
  CHECK_EQ((int)b.stepsTotal, 1234);
}

TEST(steps, los_pasos_de_antes_de_tener_reloj_cuentan_hoy) {
  Pet p;
  makePet(p, 4);
  p.applyWalk(400);                    // no clock yet: the day is unknown
  CHECK_EQ((int)p.stepsToday, 400);
  setDay(p, 20000);
  advance(p, 1);                       // the first minute with a clock must not drop them
  CHECK_EQ((int)p.stepsToday, 400);
  CHECK_EQ((int)p.stepsTotal, 400);
}

// ---------------------------------------------------------------- console saves
TEST(save, saveNow_escribe_aunque_nada_este_pendiente) {
  Pet a;
  makePet(a, 4);
  a.flushSave();                       // nothing pending now
  CHECK(!a.savePending());
  a.shiny = true;                      // what the SHINY console command does
  a.flushSave();                       // the old call: writes nothing
  Pet lost;
  lost.begin();
  CHECK(!lost.shiny);
  a.saveNow();
  Pet kept;
  kept.begin();
  CHECK(kept.shiny);
}

// ---------------------------------------------------------------- creatures met nearby
TEST(dexSeen, un_vecino_se_ve_pero_no_cuenta_como_criado) {
  Pet p;
  makePet(p, 4);
  int raised = p.registeredCount();
  CHECK(!p.isKnown(25));
  p.markSeen(25);
  CHECK(p.isSeen(25));
  CHECK(p.isKnown(25));
  CHECK(!p.isRegistered(25));
  CHECK_EQ(p.registeredCount(), raised);
  p.markSeen(0); p.markSeen(152); p.markSeen(-1);   // out of range: ignored
  CHECK(!p.isSeen(0) && !p.isSeen(152));
}

TEST(dexSeen, se_guarda_y_sobrevive_a_un_huevo_nuevo) {
  Pet p;
  makePet(p, 4);
  p.markSeen(150);
  CHECK(p.savePending());               // flushed when the screen dims
  p.flushSave();
  Pet q;
  q.begin();
  CHECK(q.isSeen(150));
}

// FNV-1a as exportBackup() computes it, to build backups the way an older firmware would
static uint32_t fnv(const uint8_t *d, size_t n) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619UL;
  return h;
}

TEST(backup, incluye_lo_visto_y_acepta_una_copia_mas_corta_de_otra_version) {
  Pet a;
  makePet(a, 4);
  a.markSeen(94);
  a.stepsToday = 77;
  uint8_t blob[Pet::BACKUP_MAX_BYTES];
  size_t n = a.exportBackup(blob, sizeof(blob));
  Pet b;
  makePet(b, 7);
  CHECK(b.importBackup(blob, n));
  CHECK(b.isSeen(94));

  // an older firmware wrote fewer fields (no dexSeen, bondDay, bondToday at the end): cut those
  // 24 bytes off and re-sign; the restore must work and leave the missing fields as they were
  size_t m = n - 4 - 24;
  uint8_t old[Pet::BACKUP_MAX_BYTES];
  memcpy(old, blob, m);
  uint32_t sum = fnv(old, m);
  memcpy(old + m, &sum, 4);
  Pet c;
  makePet(c, 7);
  c.markSeen(5);
  CHECK(c.importBackup(old, m + 4));
  CHECK_EQ((int)c.speciesId, (int)a.speciesId);
  CHECK_EQ((int)c.stepsToday, 77);
  CHECK(c.isSeen(5));                    // untouched
  CHECK(!c.isSeen(94));

  // a cut in the middle of a field (stepsTotal, 4 bytes) is not a layout any firmware wrote
  size_t mid = n - 4 - 24 - 1 - 2;
  memcpy(old, blob, mid);
  uint32_t s3 = fnv(old, mid);
  memcpy(old + mid, &s3, 4);
  CHECK(!c.importBackup(old, mid + 4));
  CHECK(c.isSeen(5));                    // and nothing was applied
  CHECK(!c.importBackup(old, 40));       // far too short
  uint8_t longer[Pet::BACKUP_MAX_BYTES + 8] = {};
  memcpy(longer, blob, n - 4);
  uint32_t s2 = fnv(longer, n + 4 - 4);
  memcpy(longer + n + 4 - 4, &s2, 4);
  CHECK(!c.importBackup(longer, n + 4));  // a newer firmware's longer backup is refused
}

// ---------------------------------------------------------------- hardening
TEST(hardening, un_reloj_que_vuelve_atras_no_rompe_la_racha) {
  Pet p;
  makePet(p, 4);
  setDay(p, 20000);
  p.caress();
  setDay(p, 20001);
  p.caress();
  CHECK_EQ((int)p.streak, 2);
  setDay(p, 20000);                    // the RTC reads an earlier day (lost time, manual change)
  p.caress();
  CHECK_EQ((int)p.streak, 2);
  CHECK_EQ((int)p.lastCareDay, 20001);
}

TEST(hardening, un_reloj_que_vuelve_atras_no_repite_los_premios_de_pasos) {
  Pet p;
  makePet(p, 4);
  setStats(p, 50, 50, 50, 50);
  setDay(p, 20001);
  p.applyWalk(600);                    // goal 1 rewarded: +15 food
  CHECK_EQ((int)p.fullness, 65);
  setDay(p, 20000);
  advance(p, 1);                       // the next minute notices a different day
  CHECK_EQ((int)p.stepsToday, 600);
  p.applyWalk(10);
  CHECK(p.fullness < 70);              // no second snack (a minute of decay may take a little)
  CHECK_EQ((int)p.stepRewardMask, 1);
}

TEST(hardening, un_huevo_nuevo_olvida_las_ofertas_rechazadas) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 60 * 50;              // level 51
  p.declineEvolve();
  p.declineFarewell();
  p.newEgg();
  p.chooseStarter(4);
  p.eggTap(); p.eggTap(); p.eggTap();  // hatches a fresh Charmander
  setStats(p, 80, 80, 80, 80);
  p.ageMinutes = 60 * 16;              // level 17: ready to evolve
  CHECK(p.wantEvolveButton());
  p.speciesId = 6;                     // a final form, three days old
  p.ageMinutes = 3 * 1440 + 5;
  CHECK(p.canFarewellNow());
  CHECK(p.wantFarewellButton());
}

TEST(hardening, mil_golpes_en_el_saco_dan_el_maximo_y_no_cero) {
  Pet p;
  makePet(p, 4);
  p.trAtk = 0;
  CHECK_EQ((int)p.trainStrength(1024), 18);
  CHECK_EQ((int)p.trAtk, 18);
}

TEST(hardening, los_descuidos_no_dan_la_vuelta_a_cero) {
  Pet p;
  makePet(p, 4);
  setStats(p, 0, 0, 0, 0);
  p.careMistakes = 255;
  advance(p, 1);
  CHECK_EQ((int)p.careMistakes, 255);
}

TEST(hardening, el_tope_diario_de_vinculo_se_renueva_con_el_dia_aunque_el_primer_gesto_sea_un_juego) {
  Pet p;
  makePet(p, 4);
  p.bond = 0;
  setDay(p, 20000);
  for (int i = 0; i < 12; i++) p.playResult(10);   // reaches the daily cap
  int capped = p.bond;
  p.playResult(10);
  CHECK_EQ((int)p.bond, capped);                   // still capped the same day
  setDay(p, 20001);
  int before = p.bond;
  p.playResult(10);                                // first act of the next day: +2 and the +4 daily care
  CHECK_EQ((int)p.bond, before + 6);
}

TEST(hardening, el_tope_diario_de_vinculo_sobrevive_a_un_reinicio) {
  Pet p;
  makePet(p, 4);
  p.bond = 0;
  setDay(p, 20000);
  for (int i = 0; i < 12; i++) p.playResult(10);
  int capped = p.bond;
  p.flushSave();
  p.saveNow();
  Pet q;
  q.begin();
  q.setClock(20000UL * 86400 + 43200);
  q.playResult(10);
  CHECK_EQ((int)q.bond, capped);
}

TEST(hardening, una_partida_sin_puntos_no_da_vinculo_ni_cuidado) {
  Pet p;
  makePet(p, 4);
  p.bond = 0;
  setDay(p, 20000);
  p.playResult(0);
  CHECK_EQ((int)p.bond, 0);
  CHECK_EQ((int)p.streak, 0);
  CHECK_EQ((int)p.lastCareDay, 0);
}

TEST(hardening, una_puntuacion_mayor_de_255_no_se_trunca) {
  Pet p;
  makePet(p, 4);
  p.trSpe = 0;
  p.playResult(300);
  CHECK_EQ((int)p.trSpe, 60);          // 300 / 5, not (300 % 256) / 5
  CHECK_EQ((int)p.gameHi, 300);
}

TEST(hardening, tocar_el_huevo_no_eclosiona_mientras_se_elige_el_inicial) {
  Pet p;
  mockNvsReset();
  mockSetMillis(0);
  p.begin();
  CHECK(p.awaitingStarter());
  p.eggTap(); p.eggTap(); p.eggTap();
  CHECK(p.isEgg());
  CHECK(p.awaitingStarter());
}

// every impossible value below is exported as is (the export does not clamp) and re-signed
static void expectRefused(void (*mutate)(Pet &), const char *why) {
  Pet a;
  makePet(a, 4);
  mutate(a);
  uint8_t blob[Pet::BACKUP_MAX_BYTES];
  size_t n = a.exportBackup(blob, sizeof(blob));
  Pet b;
  makePet(b, 7);
  b.fullness = 61;
  CHECK_MSG(n > 0 && !b.importBackup(blob, n), why);
  CHECK_EQ((int)b.fullness, 61);       // and nothing was applied
}

TEST(hardening, una_copia_con_valores_imposibles_se_rechaza) {
  expectRefused([](Pet &p) { p.speciesId = 0; }, "species 0 is neither an egg nor a creature");
  expectRefused([](Pet &p) { p.geneAtk = 255; }, "a gene outside 90..110");
  expectRefused([](Pet &p) { p.geneSpe = 10; }, "a gene outside 90..110");
  expectRefused([](Pet &p) { p.weight = 250; }, "weight over 100");
  expectRefused([](Pet &p) { p.nick[0] = 1; p.nick[1] = 0; }, "a control character in the nickname");
  expectRefused([](Pet &p) { p.fullness = 255; }, "need over 100");
}

TEST(hardening, una_copia_de_una_criatura_que_aun_elige_inicial_se_rechaza) {
  Pet a;
  mockNvsReset();
  mockSetMillis(0);
  a.begin();                           // first game: starterPick is set
  a.speciesId = 4;                     // but it is not an egg: the pet would never advance
  uint8_t blob[Pet::BACKUP_MAX_BYTES];
  size_t n = a.exportBackup(blob, sizeof(blob));
  Pet b;
  makePet(b, 7);
  CHECK(!b.importBackup(blob, n));
}
