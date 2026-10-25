// Host renderer: builds the sketch as is (TamaPoke.ino) against a software
// canvas, takes the game to every screen and saves the capture as PPM.
//   render_main <output_dir> [filter]
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include "shim/Arduino.h"
#include "shim/SD_MMC.h"
// scenes set the critter's internal timers and counters directly
// (eating, evolving, medals...): pet.h is opened up only here
#define private public
#include "pet.h"
#undef private
#include "TamaPoke.gen.cpp"

extern int gBatPercent;
extern bool gBatCharging;

static std::vector<uint16_t> gShot;

static void savePpm(const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); exit(1); }
  fprintf(f, "P6\n%d %d\n255\n", (int)gfx->width(), (int)gfx->height());
  for (uint16_t c : gShot) {
    int r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;  // 5/6/5 bits -> 8, without darkening
    uint8_t rgb[3] = { (uint8_t)(r << 3 | r >> 2), (uint8_t)(g << 2 | g >> 4), (uint8_t)(b << 3 | b >> 2) };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void closeAll() {
  galleryOpen = gameOpen = sackOpen = kbOpen = clockOpen = cardOpen = false;
  galleryDetail = 0;
  cardPage = 0;
  pet.ceremony = CER_NONE;
}

// The data is that of a well-cared-for adult critter; everything fixed so the image
// doesn't change between runs.
static void adult(int dex) {
  pet.speciesId = dex;
  pet.ageMinutes = 60 * 40;
  pet.fullness = 70; pet.joy = 80; pet.energy = 60; pet.hygiene = 90;
  pet.sleeping = false;
  pet.streak = 12; pet.bond = 60;
  pet.medals = MED_LV10 | MED_LV25 | MED_BERRY;
  memset(pet.dexReg, 0, sizeof(pet.dexReg));
  memset(pet.dexSeen, 0, sizeof(pet.dexSeen));
  for (int d = 1; d <= 151; d++)  // all raised except #3 and #151: they show "???" and silhouettes
    if (d != 3 && d != 151) pet.dexReg[(d - 1) >> 3] |= 1 << ((d - 1) & 7);
}

// first Pokemon of each biome, to see the landscapes
static int dexInBiome(int b) {
  for (int d = 1; d <= 151; d++)
    if (DEX_TBL[d].biome == b) return d;
  return 1;
}

// fake neighbor for the scene (id other than 0)
extern NbEntry gHostNb[NB_TABLE_SIZE];
extern int gHostNbN;
// one sprite load per second, as in the game
static void settleNeighbors() {
  uint32_t t = millis();
  for (int i = 0; i < NB_MAX_SHOWN + 1; i++) { mockSetMillis(t + 1100 * (i + 1)); ensureNeighbors(); }
  mockSetMillis(t);
}
static void nb(uint32_t id, int species, bool shiny = false, bool sleeping = false) {
  NbEntry &e = gHostNb[gHostNbN++];
  e = NbEntry{};
  e.used = true; e.id = id; e.species = species; e.shiny = shiny; e.sleeping = sleeping;
  e.level = 30; e.lastSeen = e.lastCount = 100000;
}

static void atHour(int h) { pet.lastSeenEpoch = 1767225600UL + h * 3600UL; }

struct Scene {
  const char *name;
  void (*setup)();
  bool noPmd;      // no sprites on the SD: the "missing sprites" notice
};

static const Scene SCENES[] = {
  { "starter", [] { closeAll(); }, false },  // first game: no starter chosen yet
  { "egg", [] { closeAll(); pet.speciesId = -1; }, false },
  { "main_day", [] { closeAll(); adult(6); }, false },
  { "main_night", [] { closeAll(); adult(6); pet.sleeping = true; }, false },
  { "gallery", [] { closeAll(); adult(6); galleryOpen = true; galleryPage = 0; galleryDirty = true; }, false },
  { "gallery_detail", [] { closeAll(); adult(6); galleryOpen = true; galleryDetail = 25; galleryPmd.load(25, false); }, false },
  { "card_profile", [] { closeAll(); adult(6); cardOpen = true; cardPage = 0; }, false },
  { "card_stats", [] { closeAll(); adult(6); cardOpen = true; cardPage = 1; }, false },
  { "keyboard", [] { closeAll(); adult(6); kbOpen = true; strcpy(nameBuf, "ASH"); nameLen = 3; }, false },
  { "clock", [] { closeAll(); adult(6); clockOpen = true; clockH = 14; clockM = 35; }, false },
  { "game", [] { closeAll(); adult(25); gameOpen = true; ballX = CX; ballY = 150; ballVX = ballVY = 0; gamePetX = CX; gameScore = 7; gameMisses = 1; }, false },
  { "sack", [] { closeAll(); adult(25); sackOpen = true; sackUntil = millis() + 8000; sackHits = 14; }, false },
  { "farewell", [] { closeAll(); adult(6); pet.startFarewell(); }, false },
  // dialogs, ending buttons, effects, card pages, fallback sprites...
  { "nearby_1", [] { closeAll(); adult(6); nb(1, 25); }, false },
  { "nearby_2", [] { closeAll(); adult(6); nb(1, 25); nb(2, 7); }, false },
  { "nearby_3", [] { closeAll(); adult(6); nb(1, 25); nb(2, 7); nb(3, 39); }, false },
  { "nearby_4", [] { closeAll(); adult(6); nb(1, 25); nb(2, 7); nb(3, 39); nb(4, 94, true); }, false },
  { "nearby_big", [] { closeAll(); adult(143); nb(1, 25); nb(2, 95); nb(3, 131); nb(4, 1); }, false },
  { "nearby_night", [] { closeAll(); adult(6); pet.sleeping = true; nb(1, 25, false, true); nb(2, 7, false, true); nb(3, 39); nb(4, 94, true, true); }, false },
  { "nearby_greet", [] { closeAll(); adult(6); nb(1, 25); nb(2, 7); settleNeighbors(); greetNeighbors(); mockSetMillis(millis() + 600); }, false },
  { "feed_menu", [] { closeAll(); adult(6); feedMenuUntil = millis() + 6000; }, false },
  { "release_dialog", [] { closeAll(); adult(6); confirmUntil = millis() + 10000; }, false },
  { "evolve_dialog", [] { closeAll(); adult(4); pet.ageMinutes = 60 * 20; choiceKind = 1; choiceUntil = millis() + 12000; }, false },
  { "farewell_dialog", [] { closeAll(); adult(6); pet.ageMinutes = FAREWELL_AGE_MIN + 100; choiceKind = 2; choiceUntil = millis() + 12000; }, false },
  { "cta_evolve", [] { closeAll(); adult(4); pet.ageMinutes = 60 * 20; }, false },
  { "cta_farewell", [] { closeAll(); adult(6); pet.ageMinutes = FAREWELL_AGE_MIN + 100; }, false },
  { "cta_runaway", [] { closeAll(); adult(6); pet.dbgRunawayReady(); }, false },
  { "celebration_medal", [] { closeAll(); adult(6); pet.newMedal = MED_LV10; pet.medalUntil = millis() + 4000; }, false },
  { "celebration_streak", [] { closeAll(); adult(6); pet.streak = 7; pet.milestoneUntil = millis() + 4000; }, false },
  { "eating", [] { closeAll(); adult(6); pet.eatUntil = millis() + 2000; }, false },
  { "heart", [] { closeAll(); adult(6); pet.heartUntil = millis() + 1000; }, false },
  { "poops_hungry", [] { closeAll(); adult(6); pet.poops = 3; pet.fullness = 20; }, false },
  { "battery_low", [] { closeAll(); adult(6); gBatPercent = 10; }, false },
  { "battery_charging", [] { closeAll(); adult(6); gBatCharging = true; }, false },
  { "bath", [] { closeAll(); adult(6); startBath(); bathUntil = millis() + 2000; }, false },
  { "bath_pop", [] { closeAll(); adult(6); startBath(); bathUntil = millis() + 500; }, false },
  { "evolving", [] { closeAll(); adult(6); pet.prevSpeciesId = 5; pet.evolveUntil = millis() + EVOLVE_ANIM_MS / 2; evoPmd.load(5, false); }, false },
  { "runaway", [] { closeAll(); adult(6); pet.startRunaway(); }, false },
  { "egg_rare_cracked", [] { closeAll(); pet.speciesId = -1; pet.eggTaps = 2; pet.eggTarget = 150; }, false },
  { "main_nosprites", [] { closeAll(); adult(25); }, true },
  { "card_medals", [] { closeAll(); adult(6); cardOpen = true; cardPage = 2; }, false },
  { "card_progress", [] { closeAll(); adult(6); cardOpen = true; cardPage = 3; }, false },
  // pressed look (the finger is down on it)
  { "press_deck", [] { closeAll(); adult(6); touchOn = target(P_DECK, 1, true); }, false },
  { "press_cta", [] { closeAll(); adult(4); pet.ageMinutes = 60 * 20; touchOn = target(P_CTA, 0, true); }, false },
  { "press_train", [] { closeAll(); adult(6); cardOpen = true; cardPage = 1; touchOn = target(P_TRAIN, 0, true); }, false },
  { "press_key", [] { closeAll(); adult(6); kbOpen = true; strcpy(nameBuf, "ASH"); nameLen = 3; touchOn = target(P_KEY, 8, true); }, false },
  { "press_cell", [] { closeAll(); adult(6); galleryOpen = true; galleryPage = 0; galleryDirty = true; touchOn = target(P_CELL, 5, true); }, false },
  { "press_release_yes", [] { closeAll(); adult(6); confirmUntil = millis() + 10000; touchOn = target(P_REL_YES, 0, true); }, false },
  { "press_night", [] { closeAll(); adult(6); pet.sleeping = true; touchOn = target(P_DECK, 2, true); }, false },
  { "walk_find_stone", [] { closeAll(); adult(25); pet.stepRewardIdx = Pet::STEP_FIND_STONE; pet.stepRewardUntil = millis() + 4000; }, false },
  { "card_steps", [] { closeAll(); adult(6); cardOpen = true; cardPage = 4; pet.stepsToday = 3240; pet.stepsTotal = 61250; }, false },
  { "gallery_page6", [] { closeAll(); adult(6); galleryOpen = true; galleryPage = 6; galleryDirty = true; }, false },
  { "gallery_seen", [] { closeAll(); adult(6); pet.dexReg[0] &= ~0xC0; pet.dexSeen[0] |= 0xC0; galleryOpen = true; galleryPage = 0; galleryDirty = true; }, false },  // #7, #8: met, not raised
  { "gallery_detail_seen", [] { closeAll(); adult(6); pet.dexReg[0] &= ~0x40; pet.dexSeen[0] |= 0x40; galleryOpen = true; galleryDetail = 7; galleryPmd.load(7, false); }, false },
  { "gallery_detail_unknown", [] { closeAll(); adult(6); galleryOpen = true; galleryDetail = 3; galleryPmd.load(3, false); }, false },
  { "game_over", [] { closeAll(); adult(25); gameOpen = true; gameScore = 12; gameMisses = 3; gameNewHi = true; gameOverUntil = millis() + 4000; gamePetX = CX; ballX = CX; ballY = 300; ballVX = ballVY = 0; }, false },
  { "sack_over", [] { closeAll(); adult(25); sackOpen = true; sackUntil = millis() - 1; sackOverUntil = millis() + 3000; sackHits = 31; sackGain = 2; sackNewHi = true; }, false },
  { "scene_dawn", [] { closeAll(); adult(dexInBiome(0)); atHour(7); }, false },
  { "scene_dusk", [] { closeAll(); adult(dexInBiome(0)); atHour(19); }, false },
  { "biome_beach", [] { closeAll(); adult(dexInBiome(1)); }, false },
  { "biome_forest", [] { closeAll(); adult(dexInBiome(2)); }, false },
  { "biome_volcano", [] { closeAll(); adult(dexInBiome(3)); }, false },
  { "biome_mountain", [] { closeAll(); adult(dexInBiome(4)); }, false },
  { "biome_snow", [] { closeAll(); adult(dexInBiome(5)); }, false },
  { "biome_beach_night", [] { closeAll(); adult(dexInBiome(1)); pet.sleeping = true; }, false },
};

static Pet gPristinePet;  // the critter right after setup()

// Takes the game to the scene. Every screen starts from the same state: without this the
// image would depend on which scenes ran before (critter animation,
// random() sequence).
static void prepare(const Scene &sc) {
  gBatPercent = 80; gBatCharging = false; pet.lastSeenEpoch = 0;
  feedMenuUntil = confirmUntil = bathUntil = 0; bathPending = false; choiceKind = 0;
  greetId = greetUntil = 0;
  touchOn = Press();
  pet = gPristinePet;  // the whole critter (timers, pokedex, medals...) as after boot
  screenKeyValid = false;  // a static screen is always painted the first time
  gameOverUntil = sackOverUntil = 0; gameNewHi = sackNewHi = false; sackGain = 0;
  randomSeed(1);
  monFor = -2;
  beh.mode = 0; beh.act = PMD_IDLE; beh.t0 = 0; beh.until = 0; beh.x = beh.targetX = CX;
  mockSetMillis(100000);
  gHostNbN = 0;
  for (int i = 0; i < NB_MAX_SHOWN; i++) { nbPmd[i].unload(); nbSlot[i] = NbSlot{}; }
  if (std::string(sc.name) != "starter") pet.chooseStarter(1);
  sc.setup();
  ensureMon();
  settleNeighbors();
  if (sc.noPmd) pmd.unload();  // forces the "missing sprites" notice
  mockSetMillis(100000);
}

// ---------- touch tests: the center of each drawn element is tapped ----------
static int gTapFails = 0;
static const Scene *findScene(const char *name) {
  for (const Scene &sc : SCENES) if (!strcmp(sc.name, name)) return &sc;
  fprintf(stderr, "no existe la escena '%s'\n", name);
  exit(2);
}
#define EXPECT(cond, what) do { if (!(cond)) { fprintf(stderr, "FALLA %-22s %s\n", curScene, what); gTapFails++; } } while (0)
static const char *curScene = "";
static void begin(const char *name) { curScene = name; prepare(*findScene(name)); }
static void tapAt(int x, int y) {
  // handleTouch() diverts the bag before onTap(): its own function is used
  if (sackOpen) { sackTouch((int16_t)y); return; }
  onTap((int16_t)x, (int16_t)y);
}

// one step of a scripted finger through the real handleTouch(): ms later, down or up at x,y
static void touchStep(bool down, int x, int y, uint32_t ms) {
  gShimTouchDown = down;
  gShimTouchX = (int16_t)x;
  gShimTouchY = (int16_t)y;
  mockAdvanceMillis(ms);
  gTouchIrq = true;
  handleTouch();
}
static void fingerUp() { touchStep(false, 0, 0, 40); }
static void touchReset() { gShimTouchDown = false; wasPressed = false; swallowGesture = false; holdFired = false; }

static int runTapTests() {
  // neighbors: each slot loads its sprite and releases it when the neighbor leaves
  begin("nearby_4");
  for (int i = 0; i < NB_MAX_SHOWN; i++) EXPECT(nbPmd[i].loaded && nbSlot[i].id == (uint32_t)i + 1, "cada vecino tiene su sprite");
  gHostNbN = 2;  // two leave
  settleNeighbors();
  EXPECT(nbPmd[0].loaded && nbPmd[1].loaded && !nbPmd[2].loaded && !nbPmd[3].loaded && !nbSlot[3].id, "al irse un vecino se suelta su sprite");
  gHostNbN = 0;
  settleNeighbors();
  EXPECT(!nbPmd[0].loaded && !nbPmd[1].loaded, "sin vecinos no queda ningun sprite cargado");
  // greetings: an awake neighbor on screen is greeted once, a sleeping one is not
  begin("main_day");
  nb(7, 25, false, true);
  settleNeighbors();
  greetNeighbors();
  EXPECT(!greetUntil && pet.greetsToday == 0, "un vecino dormido no se saluda");
  gHostNb[0].sleeping = false;
  greetNeighbors();
  EXPECT(greetId == 7 && timeLeft(greetUntil) && pet.greetsToday == 1, "un vecino despierto se saluda");
  greetNeighbors();
  EXPECT(pet.greetsToday == 1, "el mismo vecino solo una vez");
  begin("main_day");
  nb(8, 7);
  settleNeighbors();
  gameOpen = true;
  greetNeighbors();
  EXPECT(pet.greetsToday == 0, "no se saluda con otra pantalla encima");
  gameOpen = false;
  dimStage = 1;
  greetNeighbors();
  EXPECT(pet.greetsToday == 0, "ni con la pantalla atenuada");
  dimStage = 0;
  greetNeighbors();
  EXPECT(pet.greetsToday == 1, "en la pantalla principal si");
  begin("evolving");
  EXPECT(!pet.greet(99), "durante la evolucion no se saluda");
  // food selector: the center of each icon picks its slot
  for (int i = 0; i < 4; i++) {
    begin("feed_menu");
    uint8_t w0 = pet.weight;
    tapAt(buttons[i].cx, buttons[i].cy);
    EXPECT(feedMenuUntil == 0, "el selector se cierra");
    EXPECT(pet.eating(), "el bicho come");
    // #6 (Charizard) has the red berry as its favorite (slot 0): only that one gives the heart
    if (i == 0) EXPECT(pet.berryKnown, "la casilla 1 es la baya roja (favorita)");
    else EXPECT(!pet.berryKnown, "las casillas 2 y 3 no son la baya favorita");
    if (i == 3) EXPECT(pet.weight > w0, "la casilla 4 es la chuche (engorda)");
    else EXPECT(pet.weight == w0, "una baya no engorda");
  }
  // the edge of the last circle is still the treat
  begin("feed_menu");
  uint8_t w1 = pet.weight;
  tapAt(buttons[3].cx + BTN_R - 2, buttons[3].cy);
  EXPECT(pet.weight > w1, "el borde del ultimo circulo es la chuche");
  // outside the box: it closes without feeding
  begin("feed_menu");
  tapAt(CX, UY(250));
  EXPECT(feedMenuUntil == 0 && !pet.eating(), "tocar fuera cierra el selector sin comer");
  // release dialog: YES releases; any other tap closes it without releasing
  begin("release_dialog");
  tapAt(YES_X + YES_W / 2, YES_Y + YES_H / 2);
  EXPECT(pet.ceremony == CER_RELEASE, "SI suelta al bicho");
  begin("release_dialog");
  tapAt(NO_X + YES_W / 2, NO_Y + YES_H / 2);
  EXPECT(pet.ceremony == CER_NONE && !confirmUntil, "tocar NO cierra el dialogo sin soltar");
  // decision dialogs
  begin("evolve_dialog");
  tapAt(CHOICE_X + CHOICE_W / 2, CHOICE_B1_Y + CHOICE_BH / 2);
  EXPECT(pet.speciesId != 4, "evolucionar");
  begin("evolve_dialog");
  tapAt(CHOICE_X + CHOICE_W / 2, CHOICE_B2_Y + CHOICE_BH / 2);
  EXPECT(pet.speciesId == 4 && !choiceKind && pet.evoDeclinedLv == pet.level(), "mantener forma");
  begin("farewell_dialog");
  tapAt(CHOICE_X + CHOICE_W / 2, CHOICE_B1_Y + CHOICE_BH / 2);
  EXPECT(pet.ceremony == CER_FAREWELL, "despedirse");
  begin("farewell_dialog");
  tapAt(CHOICE_X + CHOICE_W / 2, CHOICE_B2_Y + CHOICE_BH / 2);
  EXPECT(pet.ceremony == CER_NONE && !choiceKind && pet.farDeclinedAge > pet.ageMinutes, "quedaros juntos");
  // ending buttons
  begin("cta_evolve");
  tapAt(EVO_BTN_X + EVO_BTN_W / 2, EVO_BTN_Y + EVO_BTN_H / 2);
  EXPECT(choiceKind == 1, "boton de evolucion abre el dialogo");
  begin("cta_farewell");
  tapAt(FAR_BTN_X + FAR_BTN_W / 2, FAR_BTN_Y + FAR_BTN_H / 2);
  EXPECT(choiceKind == 2, "boton de despedida abre el dialogo");
  begin("cta_runaway");
  tapAt(FAR_BTN_X + FAR_BTN_W / 2, FAR_BTN_Y + FAR_BTN_H / 2);
  EXPECT(pet.ceremony == CER_RUNAWAY, "boton de escapada");
  // action buttons
  for (int i = 0; i < 4; i++) {
    begin("main_day");
    bool slept = pet.sleeping;
    tapAt(buttons[i].cx, buttons[i].cy);
    if (i == 0) EXPECT(feedMenuUntil != 0, "comer abre el selector");
    if (i == 1) EXPECT(gameOpen, "jugar abre el minijuego");
    if (i == 2) EXPECT(pet.sleeping != slept, "luz cambia el sueno");
    if (i == 3) EXPECT(bathUntil != 0, "bano empieza");
  }
  // petting: the center of the sprite falls inside the critter zone
  begin("main_day");
  tapAt(CX, PET_CY);
  EXPECT(pet.showHeart(), "caricia al bicho");
  begin("main_day");  // and a tap on the status bars is NOT petting
  tapAt(CX, UY(332));
  EXPECT(!pet.showHeart(), "las barras no acarician");
  // starter choice: the center of each row picks its Pokemon
  for (int i = 0; i < 3; i++) {
    begin("starter");
    EXPECT(pet.awaitingStarter(), "la escena parte de la eleccion de inicial");
    tapAt(STARTER_ROW_X + STARTER_ROW_W / 2, STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP) + STARTER_ROW_H / 2);
    EXPECT(!pet.awaitingStarter() && pet.eggTarget == STARTER_DEX[i], "la fila elige su inicial");
  }
  begin("starter");  // the gap between rows and outside them picks nothing
  tapAt(STARTER_ROW_X + STARTER_ROW_W / 2, STARTER_ROW_Y + STARTER_ROW_H + STARTER_ROW_GAP / 2);
  tapAt(STARTER_ROW_X - U(10), STARTER_ROW_Y + STARTER_ROW_H / 2);
  EXPECT(pet.awaitingStarter(), "tocar fuera de las filas no elige");
  // gallery: each cell opens its detail; the header exits; the detail returns to the grid
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      begin("gallery");
      tapAt(GAL_X + c * GAL_CELL + GAL_CELL / 2, GAL_Y + r * GAL_CELL_Y + GAL_CELL_Y / 2);
      EXPECT(galleryDetail == r * 4 + c + 1, "la celda abre su Pokemon");
    }
  begin("gallery");
  tapAt(CX, HEADER_ZONE_H - 1);
  EXPECT(!galleryOpen, "tocar la cabecera cierra la galeria");
  begin("gallery");
  tapAt(GAL_X + GAL_CELL / 2, HEADER_ZONE_H + (GAL_Y - HEADER_ZONE_H) / 2);  // between the header and the grid
  EXPECT(galleryOpen && !galleryDetail, "el hueco bajo la cabecera no hace nada");
  begin("gallery_detail");
  tapAt(CX, CY);
  EXPECT(galleryOpen && !galleryDetail, "tocar el detalle vuelve a la rejilla");
  // card: the name opens the keyboard, ENTRENAR FUERZA opens the bag, only Back closes it
  begin("card_profile");
  tapAt(CX, CARD_NAME_Y + CARD_NAME_H / 2);
  EXPECT(kbOpen, "tocar el nombre abre el teclado");
  begin("card_profile");
  tapAt(CX, CARD_NAME_Y + CARD_NAME_H + UY(40));
  EXPECT(!kbOpen && cardOpen, "tocar bajo el nombre no hace nada");
  tapAt(BACK_X + BACK_W / 2, BACK_Y + BACK_H / 2);
  EXPECT(!cardOpen, "Volver cierra la ficha");
  begin("card_stats");
  tapAt(TRAIN_X + TRAIN_W / 2, TRAIN_Y + TRAIN_H / 2);
  EXPECT(sackOpen && !cardOpen, "ENTRENAR FUERZA abre el saco");
  begin("card_stats");
  tapAt(TRAIN_X + TRAIN_W / 2, TRAIN_Y - UY(12));
  EXPECT(!sackOpen && cardOpen, "tocar fuera del boton no entrena ni cierra la ficha");
  begin("card_stats");
  pet.sleeping = true;
  EXPECT(!pressTarget(TRAIN_X + TRAIN_W / 2, TRAIN_Y + TRAIN_H / 2).enabled, "dormido no se entrena");
  tapAt(TRAIN_X + TRAIN_W / 2, TRAIN_Y + TRAIN_H / 2);
  EXPECT(!sackOpen && cardOpen, "y tocarlo no hace nada");
  // what can be pressed: disabled buttons are still targets (deny sound), not the empty space
  begin("main_night");
  for (int i = 0; i < 4; i++) {
    Press p = pressTarget(buttons[i].cx, buttons[i].cy);
    EXPECT(p.id == P_DECK && p.idx == i && p.enabled == (i == 2), "dormido solo LUZ esta activo");
  }
  tapAt(buttons[0].cx, buttons[0].cy);
  EXPECT(!feedMenuUntil, "COMER dormido no abre el selector");
  begin("main_day");
  EXPECT(pressTarget(CX, UY(332)).id == P_NONE, "el hueco entre botones no es un objetivo");
  EXPECT(pressTarget(CX, PET_CY).id == P_PET, "el bicho es el objetivo de la caricia");
  begin("keyboard");
  nameLen = 0; nameBuf[0] = 0;
  EXPECT(!pressTarget(KB_X + (28 % KB_COLS) * KB_W + KB_W / 2, KB_Y + (28 / KB_COLS) * KB_H + KB_H / 2).enabled, "borrar con el nombre vacio no hace nada");
  strcpy(nameBuf, "ABCDEFGHIJK"); nameLen = 11;
  EXPECT(!pressTarget(KB_X + KB_W / 2, KB_Y + KB_H / 2).enabled, "una letra con el nombre lleno no hace nada");
  begin("gallery");
  EXPECT(pressTarget(CX, HEADER_ZONE_H / 2).id == P_CLOSE, "la cabecera cierra la galeria");
  begin("gallery_page6");
  galleryPage = 9;                         // the last page has 7 cells: the rest are not targets
  EXPECT(pressTarget(GAL_X + 3 * GAL_CELL + GAL_CELL / 2, GAL_Y + GAL_CELL_Y + GAL_CELL_Y / 2).id == P_NONE, "las celdas vacias no son objetivos");
  // pressed look through the real gesture handler: down shows it, sliding off clears it, lift fires
  touchReset();
  begin("main_day");
  touchStep(true, buttons[1].cx, buttons[1].cy, 40);
  EXPECT(isPressed(P_DECK, 1), "al apoyar el dedo el boton se ve pulsado");
  touchStep(true, buttons[1].cx + U(60), buttons[1].cy, 40);
  EXPECT(!isPressed(P_DECK, 1), "al salirse del boton deja de verse pulsado");
  fingerUp();
  EXPECT(!gameOpen, "y al soltar fuera no juega");
  touchReset();
  begin("main_day");
  touchStep(true, buttons[1].cx, buttons[1].cy, 40);
  fingerUp();
  EXPECT(gameOpen && touchOn.id == P_NONE, "al soltar dentro juega y deja de verse pulsado");
  touchReset();
  begin("main_day");
  touchStep(true, buttons[1].cx, buttons[1].cy, 40);
  touchStep(true, buttons[1].cx + U(60), buttons[1].cy, 40);
  touchStep(true, buttons[1].cx, buttons[1].cy, 40);  // and back onto the button
  EXPECT(!isPressed(P_DECK, 1), "volver al boton no lo vuelve a pulsar");
  fingerUp();
  EXPECT(!gameOpen, "salirse y volver cancela el toque");
  touchReset();
  begin("bath");
  EXPECT(!pressTarget(buttons[3].cx, buttons[3].cy).enabled, "BANO durante el bano no hace nada");
  begin("evolving");
  EXPECT(!pressTarget(buttons[1].cx, buttons[1].cy).enabled, "JUGAR durante la evolucion no hace nada");
  begin("keyboard");
  nameLen = 0; nameBuf[0] = 0;
  keyboardTap(target(P_KEY, 28, true));  // a stale enabled flag must not underflow the name
  EXPECT(nameLen == 0, "borrar con el nombre vacio no rompe nada");
  touchReset();
  begin("gallery");
  render();
  touchStep(true, GAL_X + GAL_CELL / 2, GAL_Y + GAL_CELL_Y / 2, 40);
  EXPECT(galleryDirty, "pulsar una celda repinta la rejilla");
  fingerUp();
  touchReset();
  // keyboard: each key (its center) types its letter; <- deletes; OK saves
  for (int i = 0; i < 28; i++) {
    begin("keyboard");
    nameLen = 0; nameBuf[0] = 0;
    tapAt(KB_X + (i % KB_COLS) * KB_W + (KB_W - U(6)) / 2, KB_Y + (i / KB_COLS) * KB_H + (KB_H - UY(6)) / 2);
    EXPECT(nameLen == 1 && nameBuf[0] == KB_KEYS[i], "la tecla escribe su letra");
  }
  begin("keyboard");  // "ASH" + <-  =>  "AS"
  tapAt(KB_X + (28 % KB_COLS) * KB_W + KB_W / 2, KB_Y + (28 / KB_COLS) * KB_H + KB_H / 2);
  EXPECT(nameLen == 2, "<- borra una letra");
  begin("keyboard");
  tapAt(KB_X + (29 % KB_COLS) * KB_W + KB_W / 2, KB_Y + (29 / KB_COLS) * KB_H + KB_H / 2);
  EXPECT(!kbOpen && !strcmp(pet.nick, "ASH"), "OK guarda el apodo");
  // clock: +/- buttons, sound and OK
  begin("clock");
  tapAt(CLK_X0 + CLOCK_BTN_W / 2, CLOCK_BTN_Y + CLOCK_BTN_H / 2);
  EXPECT(clockH == 13, "hora -");
  tapAt(CLK_X1 + CLOCK_BTN_W / 2, CLOCK_BTN_Y + CLOCK_BTN_H / 2);
  EXPECT(clockH == 14, "hora +");
  tapAt(CLK_X2 + CLOCK_BTN_W / 2, CLOCK_BTN_Y + CLOCK_BTN_H / 2);
  EXPECT(clockM == 34, "min -");
  tapAt(CLK_X3 + CLOCK_BTN_W / 2, CLOCK_BTN_Y + CLOCK_BTN_H / 2);
  EXPECT(clockM == 35, "min +");
  tapAt((CLK_X0 + CLOCK_BTN_W + CLK_X1) / 2, CLOCK_BTN_Y + CLOCK_BTN_H / 2);
  EXPECT(clockH == 14 && clockM == 35, "el hueco entre botones no cambia nada");
  bool snd = audioEnabled();
  tapAt(SND_PILL_X + SND_PILL_W / 2, SND_PILL_Y + SND_PILL_H / 2);
  EXPECT(audioEnabled() != snd, "interruptor de sonido");
  audioSetEnabled(snd);
  tapAt(CLOCK_OK_X + CLOCK_OK_W / 2, CLOCK_OK_Y + CLOCK_OK_H / 2);
  EXPECT(!clockOpen, "OK cierra el reloj");
  // ball minigame: tapping the ball scores, outside does not, the header quits
  begin("game");
  uint16_t sc0 = gameScore;
  tapAt((int)ballX, (int)ballY);
  EXPECT(gameScore == sc0 + 1 && ballVY < 0, "tocar la bola puntua y la lanza hacia arriba");
  begin("game");
  tapAt((int)ballX + U(120), (int)ballY + UY(120));
  EXPECT(gameScore == sc0, "tocar lejos de la bola no puntua");
  begin("game");
  tapAt(CX, UY(30));
  EXPECT(!gameOpen, "tocar la cabecera abandona el minijuego");
  begin("game");
  for (int i = 0; i < 50; i++) {  // the ball always reappears inside its zone and at its height
    respawnBall();
    EXPECT(ballX >= U(150) && ballX < U(150) + U(166) && ballY == UY(96), "la bola reaparece en su zona");
    EXPECT(fabsf(ballVX) >= 1.6f * UI_F - 0.001f && fabsf(ballVX) <= 4.0f * UI_F + 0.001f, "velocidad inicial proporcional");
  }
  // touch radius: 0.9 of U(74) hits, 1.1 does not
  begin("game");
  tapAt((int)ballX + (int)(0.9f * U(74)), (int)ballY);
  EXPECT(gameScore == sc0 + 1, "un toque a 0,9 del radio puntua");
  begin("game");
  tapAt((int)ballX + (int)(1.1f * U(74)), (int)ballY);
  EXPECT(gameScore == sc0, "un toque a 1,1 del radio no puntua");
  // proportional trajectory: the jump height of a hit ball, divided
  // by UI_F, is the design one (simulated here with the game constants at 466)
  begin("game");
  gameScore = 7; gameMisses = 0; ballX = CX; ballY = UY(250); ballVX = ballVY = 0; lastGameStep = 0;
  tapAt((int)ballX, (int)ballY);  // hit: gameScore becomes 8
  float y0 = ballY, top = ballY;
  for (int i = 0; i < 40; i++) { mockAdvanceMillis(85); stepGame(); if (ballY < top) top = ballY; }
  float v = -(6.6f + 8 * 0.22f), g = 0.40f + 8 * 0.013f, yy = 0, ref = 0;  // design at 466
  for (int i = 0; i < 40; i++) { v += g; yy += v; if (yy < ref) ref = yy; }
  float rise = (y0 - top) / UI_F, refRise = -ref;
  EXPECT(fabsf(rise - refRise) <= 0.04f * refRise + 3, "el salto de la bola es proporcional al panel");
  // the wall: a ball shot against the edge bounces and does not cross it
  begin("game");
  gameScore = 3; ballX = CX + BALL_R - U(2); ballY = CY; ballVX = 5 * UI_F; ballVY = 0; lastGameStep = 0;
  mockAdvanceMillis(85); stepGame();
  float wdx = ballX - CX, wdy = ballY - CY;
  EXPECT(sqrtf(wdx * wdx + wdy * wdy) <= BALL_R + 0.5f, "la bola no cruza la pared");
  EXPECT(ballVX < 0, "la pared devuelve la bola");
#if !LCD_ROUND
  // square panel: ceiling and walls. A ball rising along the ceiling bounces and comes down; one that
  // exits one side comes back; one that is already outside but heading in is not reversed.
  begin("game");
  gameScore = 3; ballX = CX; ballY = CY - BALL_R + U(1); ballVX = 0; ballVY = -4 * UI_F; lastGameStep = 0;
  mockAdvanceMillis(85); stepGame();
  EXPECT(ballY >= CY - BALL_R - 0.5f && ballVY > 0, "el techo devuelve la bola hacia abajo");
  begin("game");
  gameScore = 3; ballX = CX + BALL_R - U(1); ballY = CY + UY(100); ballVX = 5 * UI_F; ballVY = 0; lastGameStep = 0;
  mockAdvanceMillis(85); stepGame();
  EXPECT(ballX <= CX + BALL_R + 0.5f && ballVX < 0, "la pared derecha devuelve la bola");
  begin("game");  // a top-left corner: both walls at once
  gameScore = 3; ballX = CX - BALL_R + U(1); ballY = CY - BALL_R + U(1); ballVX = -5 * UI_F; ballVY = -5 * UI_F; lastGameStep = 0;
  mockAdvanceMillis(85); stepGame();
  EXPECT(ballVX > 0 && ballVY > 0 && ballX >= CX - BALL_R - 0.5f && ballY >= CY - BALL_R - 0.5f, "la esquina devuelve la bola");
  begin("game");  // already outside but heading in: the position is corrected and the velocity is kept
  gameScore = 3; ballX = CX - BALL_R - U(6); ballY = CY; ballVX = 2 * UI_F; ballVY = 0; lastGameStep = 0;
  mockAdvanceMillis(85); stepGame();
  EXPECT(ballX >= CX - BALL_R - 0.5f && ballVX > 0, "dentro-hacia-dentro no invierte la velocidad");
#endif
  // training bag: tapping hits, the header quits
  begin("sack");
  uint16_t h0 = sackHits;
  tapAt(CX, UY(200));
  EXPECT(sackHits == h0 + 1, "tocar el saco cuenta un golpe");
  begin("sack");
  tapAt(CX, UY(30));
  EXPECT(!sackOpen, "tocar la cabecera abandona el saco");
  // ---- hardening: sequences through the real gesture handler ----
  touchReset();
  begin("card_stats");
  touchStep(true, TRAIN_X + TRAIN_W / 2, TRAIN_Y + TRAIN_H / 2, 40);
  fingerUp();
  EXPECT(sackOpen, "tocar ENTRENAR FUERZA abre el saco");
  {
    uint8_t joy0 = pet.joy;
    touchStep(true, CX, UY(20), 40);      // the header quits the bag
    EXPECT(!sackOpen, "la cabecera cierra el saco");
    touchStep(true, CX, UY(20), 100);     // the finger is still down
    fingerUp();
    EXPECT(!sackOpen && !cardOpen && !gameOpen && !kbOpen && pet.joy == joy0,
           "salir del saco con la cabecera no dispara un gesto al soltar");
  }
  touchReset();
  begin("game");
  {
    uint16_t s0 = gameScore;
    touchStep(true, (int)ballX, (int)ballY, 40);
    EXPECT(gameScore == s0 + 1, "un golpe a la bola cuenta al tocar");
    fingerUp();
    EXPECT(gameScore == s0 + 1 && gameOpen, "y no cuenta otra vez al soltar");
    touchStep(true, CX, UY(10), 40);
    EXPECT(gameOpen, "tocar la cabecera no abandona el juego hasta soltar");
    fingerUp();
    EXPECT(!gameOpen, "soltar en la cabecera abandona el juego");
  }
  touchReset();
  begin("game");
  gameScore = 255;                         // a skilled run: the next hit must not wrap to 0
  touchStep(true, (int)ballX, (int)ballY, 40);
  EXPECT(gameScore == 256, "la puntuacion no da la vuelta a 0 en el golpe 256");
  fingerUp();
  touchReset();
  begin("evolve_dialog");
  onSwipeV(-1);
  EXPECT(!cardOpen && !clockOpen && choiceKind == 1, "un deslizamiento vertical no abre nada tras el dialogo de evolucion");
  onSwipeV(1);
  EXPECT(!cardOpen && !clockOpen && choiceKind == 1, "ni hacia abajo");
  onSwipe(1);
  EXPECT(!galleryOpen && choiceKind == 1, "un deslizamiento lateral no abre la galeria con el dialogo abierto");
  touchStep(true, CX, PET_CY, 40);
  for (int i = 0; i < 80; i++) touchStep(true, CX, PET_CY, 40);  // 3.2 s held on the pet
  EXPECT(!confirmUntil, "mantener pulsado no abre soltar encima de un dialogo de decision");
  fingerUp();
  begin("main_day");
  {
    uint32_t day = 1767225600UL + 5 * 86400;
    pet.lastSeenEpoch = day + 9 * 3600 + 59 * 60 + 45;   // the cached time lags up to 30 s
    rtcSetEpoch(day + 10 * 3600 + 0 * 60 + 10);
    openClock();
    EXPECT(clockH == 10 && clockM == 0, "el ajuste de hora muestra la hora del RTC, no la guardada hace unos segundos");
    clockOpen = false;
  }
  touchReset();
  begin("evolving");
  startGame();
  EXPECT(!gameOpen, "no se puede jugar durante la evolucion");
  startSack();
  EXPECT(!sackOpen, "ni entrenar durante la evolucion");
  begin("evolve_dialog");
  pet.fullness = 0;                        // no longer ready: the button must not evolve it
  evoPmd.unload();
  tapAt(CHOICE_X + CHOICE_W / 2, CHOICE_B1_Y + CHOICE_BH / 2);
  EXPECT(pet.speciesId == 4 && !evoPmd.loaded, "el boton de evolucion comprueba que sigue lista");
  begin("farewell_dialog");
  pet.ageMinutes = 10;                     // too young to say goodbye
  tapAt(CHOICE_X + CHOICE_W / 2, CHOICE_B1_Y + CHOICE_BH / 2);
  EXPECT(pet.ceremony == CER_NONE, "el boton de despedida comprueba que sigue lista");
  fprintf(stderr, "%d fallos de toque\n", gTapFails);
  return gTapFails ? 1 : 0;
}

// ---------- text fitting: every real string fits where it is drawn ----------
static int gFitFails = 0;
// text at scale `size` (TS already applied) on line y: must fit in maxW (px)
static void fitBox(const char *what, const char *text, int size, int maxW) {
  setSize(size);
  int w = textW(text, size);
  if (w > maxW) {
    fprintf(stderr, "FALLA %-22s %3d px > %3d  '%s'\n", what, w, maxW, text);
    gFitFails++;
  }
}
// ... or in the visible chord of the circle at the height of that line (top and bottom)
static void fitChord(const char *what, const char *text, int size, int y) {
  setSize(size);
  int top = chordW(y), bot = chordW(y + textH());
  fitBox(what, text, size, (top < bot ? top : bot) - U(8));
}
// text split into <= maxLines lines (like drawWrapped): each line fits and nothing is lost
static void fitWrapped(const char *what, const char *text, int size, int maxW, int maxLines, int maxH = 1 << 20) {
  setSize(size);
  int st[4], ln[4];
  int n = wrapText(text, size, maxW, maxLines, st, ln);
  int L = (int)strlen(text);
  while (L > 0 && text[L - 1] == ' ') L--;
  bool ok = n > 0 && st[n - 1] + ln[n - 1] == L;
  if (n * textH() + (n - 1) * U(4) > maxH) ok = false;  // the block must also fit in its box
  for (int i = 0; i < n; i++) {
    char line[128]; memcpy(line, text + st[i], ln[i]); line[ln[i]] = 0;
    if (textW(line, size) > maxW) ok = false;
  }
  if (!ok) {
    fprintf(stderr, "FALLA %-22s no cabe en %d lineas de %d px  '%s'\n", what, maxLines, maxW, text);
    gFitFails++;
  }
}
// the worst species name
static const char *longestName() {
  const char *best = "";
  setSize(TS(2));
  for (int d = 1; d <= 151; d++)
    if (textW(dexName(d), TS(2)) > textW(best, TS(2))) best = dexName(d);
  return best;
}

// wrapText: breaks at the exact limit (the word ends right at maxW)
static int runWrapTests() {
  int fails = 0;
  setSize(1);  // 6 px per character
  struct Case { const char *text; int maxW; int lines; const char *l0; const char *l1; };
  static const Case cases[] = {
    { "AA AA BBBBB", 30, 2, "AA AA", "BBBBB" },       // the first line fills exactly; the space no longer fits
    { "AA BB", 100, 1, "AA BB", "" },
    { "ABCDEFGHIJ", 30, 2, "ABCDE", "FGHIJ" },         // no spaces: breaks by characters
    { "  HOLA   MUNDO  ", 60, 2, "HOLA", "MUNDO" },    // extra spaces
    { "", 60, 0, "", "" },
  };
  // cases that must always advance and not exceed the line limit
  {
    int st[4], ln[4];
    int n = wrapText("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 1, 30, 2, st, ln);  // 40 letters: they don't fit in 2 lines of 5
    if (n != 2 || ln[0] != 5 || ln[1] != 5) { fprintf(stderr, "FALLA wrapText recorta en maxLines: %d lineas (%d, %d)\n", n, ln[0], ln[1]); fails++; }
    n = wrapText("ABC", 1, 2, 4, st, ln);  // not even one character fits: one per line, without hanging
    if (n != 3 || ln[0] != 1 || ln[1] != 1 || ln[2] != 1) { fprintf(stderr, "FALLA wrapText con maxW menor que un caracter: %d lineas\n", n); fails++; }
    n = wrapText("AB   ", 1, 100, 2, st, ln);
    if (n != 1 || ln[0] != 2) { fprintf(stderr, "FALLA wrapText espacios finales: %d lineas, %d\n", n, ln[0]); fails++; }
  }
  for (const Case &c : cases) {
    int st[4], ln[4];
    int n = wrapText(c.text, 1, c.maxW, 2, st, ln);
    char a0[32] = "", a1[32] = "";
    if (n > 0) { memcpy(a0, c.text + st[0], ln[0]); a0[ln[0]] = 0; }
    if (n > 1) { memcpy(a1, c.text + st[1], ln[1]); a1[ln[1]] = 0; }
    if (n != c.lines || strcmp(a0, c.l0) || strcmp(a1, c.l1)) {
      fprintf(stderr, "FALLA wrapText '%s' (%d px): %d lineas '%s' | '%s'; esperaba %d '%s' | '%s'\n", c.text, c.maxW, n, a0, a1, c.lines, c.l0, c.l1);
      fails++;
    }
  }
  return fails;
}

static int runFitTests() {
  char b[96];
  const char *ln = longestName();
  // --- main scene (full width) ---
  const char *worst[] = { ln, "ABCDEFGHIJK" };  // the longest name and an 11-letter nickname (nick[12])
  for (const char *nm : worst) {
    snprintf(b, sizeof(b), T(S_NAME_FMT), "*", nm, 999);
    int hs = fitSize(b, TS(3), TS(2), UY(52));
    if (!fitsChord(b, hs, UY(52))) { fprintf(stderr, "FALLA cabecera de la escena no cabe en el circulo: '%s'\n", b); gFitFails++; }
  }
  for (StrId id : { S_EVOLVING, S_EATING, S_LIKES, S_HUNGRY, S_NEEDS_BATH, S_EXHAUSTED, S_SAD, S_CHUBBY, S_IS_SHINY, S_HAPPY,
                    S_FAREWELL, S_RUNAWAY, S_GOODBYE, S_EGG_TOUCH, S_EGG_MOVES, S_EGG_ALMOST })
    fitBox("mensaje de estado", T(id), TS(2), UI_W - U(40));
  snprintf(b, sizeof(b), T(S_POKEDEX_FMT), 151);
  fitBox("pokedex del huevo", b, TS(2), UI_W - U(40));
  // --- choose starter ---
  fitChord("titulo de inicial", T(S_CHOOSE_STARTER), TS(2), UY(68));
  for (int i = 0; i < 3; i++) fitBox("nombre de inicial en su fila", dexName(STARTER_DEX[i]), TS(3), STARTER_ROW_X + STARTER_ROW_W - U(178) - U(8));
  // --- dialogs ---
  for (const char *nm : worst) {
    char q[48];  // same size as in the game: snprintf truncates it
    snprintf(q, sizeof(q), T(S_RELEASE_FMT), nm);
    fitWrapped("soltar?", q, TS(2), U(278) - U(16), 2, UY(60));
  }
  fitBox("SI", T(S_YES), TS(2), YES_W - U(8));
  fitBox("NO", T(S_NO), TS(2), YES_W - U(8));
  for (StrId id : { S_EVO_Q, S_EVO_TAP, S_EVO_KEEP, S_FAR_Q, S_FAR_GO, S_FAR_STAY }) fitBox("dialogo de decision", T(id), TS(2), CHOICE_W - U(16));
  fitBox("boton evolucionar", T(S_EVO_TAP), TS(3), EVO_BTN_W - U(16));
  for (const char *nm : worst) {
    snprintf(b, sizeof(b), T(S_FAREWELL_BTN), nm);
    fitWrapped("boton despedida", b, TS(2), FAR_BTN_W - U(16), 2, FAR_BTN_H - UY(8));
    snprintf(b, sizeof(b), T(S_RUNAWAY_BTN), nm);
    fitWrapped("boton escapada", b, TS(2), FAR_BTN_W - U(16), 2, FAR_BTN_H - UY(8));
  }
  setSize(TS(2));
  fitBox("cancelar del reloj", T(S_CLOCK_CANCEL), TS(3), CLOCK_OK_W - U(16));
  // loose strings that were not covered
  { int hs = fitSize(T(S_EGG_HDR), TS(3), TS(2), UY(52));
    if (!fitsChord(T(S_EGG_HDR), hs, UY(52))) { fprintf(stderr, "FALLA cabecera del huevo no cabe en el circulo\n"); gFitFails++; } }
  fitChord("huevo legendario", T(S_EGG_LEGEND), TS(2), UY(316));
  fitChord("huevo raro", T(S_EGG_RARE), TS(2), UY(316));
  for (StrId id : { S_BERRY_RED, S_BERRY_BLUE, S_BERRY_GREEN }) {
    snprintf(b, sizeof(b), T(S_INFO_FMT), T(id), 9999UL);
    fitChord("baya y edad", b, TS(2), UY(296));
  }
  snprintf(b, sizeof(b), T(S_LVL_FMT), 999);
  fitChord("nivel grande", b, TS(5), UY(86));
  fitBox("sin sprites", T(S_NO_SPRITES), TS(2), UI_W - U(40));
  fitBox("cargar sprites", T(S_LOAD_SPRITES), TS(2), UI_W - U(40));
  fitBox("medalla (banner)", T(S_MEDAL_BANNER), TS(3), U(320) - U(16));
  fitBox("hito (banner)", T(S_GREAT), TS(3), U(320) - U(16));
  snprintf(b, sizeof(b), T(S_STREAK_DAYS_FMT), 365);
  fitBox("racha (banner)", b, TS(2), U(320) - U(16));
  for (int i = 0; i < MED_COUNT; i++) fitBox("nombre de medalla (banner)", medalName(i), TS(2), U(320) - U(16));
  // --- card ---
  snprintf(b, sizeof(b), T(S_NAME_FMT), "*", ln, 99);
  { setSize(TS(2)); fitChord("cabecera de la ficha (tamano pequeno)", b, TS(2), UY(40)); }
  snprintf(b, sizeof(b), "(%s)", ln);
  fitChord("especie bajo el apodo", b, TS(2), UY(64));
  snprintf(b, sizeof(b), T(S_STREAK_FMT), 365, 365);
  fitBox("racha con llama", b, TS(2), (CX + chordW(UY(224)) / 2) - (U(138) + U(24)) - U(8));
  snprintf(b, sizeof(b), T(S_INFO_FMT), T(S_BERRY_UNK), 9999UL);
  fitChord("baya y edad", b, TS(2), UY(296));
  fitChord("pista de renombrar", T(S_RENAME_HINT), TS(2), UY(332));
  fitChord("pista de volver", T(S_BACK), TS(2), UY(398));
  fitChord("combate", T(S_BATTLE), TS(3), UY(48));
  fitBox("entrenar fuerza", T(S_TRAIN_STR), TS(2), TRAIN_W - U(16));
  snprintf(b, sizeof(b), T(S_MEDALS_FMT), 8, 8);
  fitChord("medallas", b, TS(3), UY(48));
  fitChord("progreso", T(S_PROGRESS), TS(3), UY(44));
  fitChord("pasos", T(S_STEPS), TS(3), UY(44));
  snprintf(b, sizeof(b), T(S_STEPS_NEXT_FMT), 5000UL);
  fitChord("siguiente meta", b, TS(2), UY(190));
  fitChord("metas hechas", T(S_STEPS_ALL_DONE), TS(2), UY(190));
  snprintf(b, sizeof(b), T(S_STEPS_TOTAL_FMT), 4294967295UL);
  fitChord("pasos totales", b, TS(2), UY(284));
  for (int r = 0; r <= Pet::TRAIL_RANKS; r++) fitChord("rango", trailName(r), TS(2), UY(308));
  for (StrId id : { S_STEP_REWARD_1, S_STEP_REWARD_2, S_STEP_REWARD_3 }) fitBox("mensaje de pasos", T(id), TS(2), UI_W - U(40));
  snprintf(b, sizeof(b), T(S_NEXT_LVL_FMT), 60, 999);
  fitChord("siguiente nivel", b, TS(2), UY(190));
  fitChord("etiqueta de evolucion", T(S_EVO_LABEL), TS(2), UY(230));
  for (StrId id : { S_FINAL_FORM, S_EVO_READY, S_EVO_BLOCKED }) fitChord("estado de evolucion", T(id), TS(2), UY(256));
  snprintf(b, sizeof(b), T(S_EVO_IN_FMT), 99);
  fitChord("evolucion en", b, TS(2), UY(256));
  snprintf(b, sizeof(b), T(S_MISTAKES_FMT), 99);
  fitChord("descuidos", b, TS(2), UY(312));
  // --- gallery, keyboard, clock ---
  snprintf(b, sizeof(b), T(S_POKEDEX_FMT), 151);
  fitChord("pokedex", b, TS(2), UY(48));  // the title drops to this size if it doesn't fit at the large one
  snprintf(b, sizeof(b), "N.%03d %s%s", 151, "*", ln);
  fitChord("detalle de galeria (tamano pequeno)", b, TS(2), UY(60));
  fitChord("volver del detalle", T(S_DETAIL_BACK), TS(2), UY(408));
  fitChord("nombre (teclado)", T(S_NAME), TS(2), UY(56));
  fitChord("ajustar hora", T(S_SET_TIME), TS(3), UY(44));
  fitBox("HORA", T(S_HOUR), TS(2), CLOCK_BTN_W * 2 + (CLK_X1 - CLK_X0 - CLOCK_BTN_W));
  fitBox("MIN", T(S_MIN), TS(2), CLOCK_BTN_W * 2 + (CLK_X3 - CLK_X2 - CLOCK_BTN_W));
  fitBox("sonido activado", T(S_SND_ON), TS(2), SND_PILL_W - U(8));
  fitBox("sonido desactivado", T(S_SND_OFF), TS(2), SND_PILL_W - U(8));
  // --- minigames ---
  snprintf(b, sizeof(b), T(S_SCORE_FMT), 999);
  fitBox("puntuacion", b, TS(4), UI_W - U(40));
  snprintf(b, sizeof(b), T(S_RECORD_FMT), 999);
  fitBox("record", b, TS(2), UI_W - U(40));
  snprintf(b, sizeof(b), T(S_REC_FMT), 999);
  fitBox("record en vivo", b, TS(2), UI_W - U(40));
  for (StrId id : { S_NEW_RECORD, S_GREAT_JOY, S_PLUS_JOY, S_HIT_FAST }) fitBox("texto de minijuego", T(id), TS(2), UI_W - U(40));
  snprintf(b, sizeof(b), T(S_HITS_FMT), 999);
  fitBox("golpes", b, TS(4), UI_W - U(40));
  snprintf(b, sizeof(b), T(S_STR_GAIN_FMT), 9);
  fitBox("ganancia de fuerza", b, TS(3), UI_W - U(40));
  gFitFails += runWrapTests();
  fprintf(stderr, "%d textos que no caben\n", gFitFails);
  return gFitFails ? 1 : 0;
}

// ---------- power use: what repaints and when ----------
static int runPowerTests() {
  int fails = 0;
#define PCHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FALLA %-26s %s\n", curScene, what); fails++; } } while (0)
  curScene = "";
  // static screens: once drawn they are not repainted until something changes
  begin("clock");
  PCHECK(render(), "el reloj se dibuja la primera vez");
  PCHECK(!render(), "el reloj no se repinta sin cambios");
  clockH = (clockH + 1) % 24;
  PCHECK(render(), "el reloj se repinta al cambiar la hora");
  PCHECK(!render(), "y vuelve a quedarse quieto");
  begin("keyboard");
  PCHECK(render() && !render(), "el teclado: una vez y quieto");
  nameBuf[nameLen++] = 'Z'; nameBuf[nameLen] = 0;
  PCHECK(render(), "el teclado se repinta al escribir");
  begin("card_stats");
  PCHECK(render() && !render(), "la ficha de combate: una vez y quieta");
  pet.weight += 3;
  PCHECK(render(), "la ficha se repinta si cambia un dato");
  begin("card_medals");
  PCHECK(render() && !render(), "las medallas: una vez y quietas");
  begin("card_steps");
  PCHECK(render() && !render(), "los pasos: una vez y quietos");
  pet.stepsToday += 10;
  PCHECK(render(), "los pasos se repintan al caminar");
  begin("starter");
  PCHECK(render() && !render(), "la eleccion de inicial: una vez y quieta");
  // animated screens: always drawn
  for (const char *n : { "main_day", "card_profile", "game", "sack", "farewell" }) {
    begin(n);
    PCHECK(render() && render(), "las pantallas animadas se dibujan en cada frame");
  }
  // switching screens invalidates the key: going back to the clock draws it again
  begin("clock");
  render();
  begin("main_day");
  render();
  begin("clock");
  PCHECK(render(), "al volver a una pantalla estatica se vuelve a dibujar");
  // the critter's walk advances the same at 10 fps as at 5 fps (by time, not per frame)
  float x10 = 0, x4 = 0;
  for (int fps : { 10, 5 }) {
    begin("main_day");
    beh.mode = 1; beh.act = PMD_WALKR; beh.x = CX; beh.targetX = CX + U(400); beh.until = millis() + 60000; beh.t0 = millis();
    walkLast = 0;
    uint32_t t0 = 100000;
    for (uint32_t t = 0; t <= 4000; t += 1000 / fps) {
      mockSetMillis(t0 + t);
      render();
    }
    (fps == 10 ? x10 : x4) = beh.x - CX;
  }
  PCHECK(x10 > U(60), "el bicho camina");
  PCHECK(fabsf(x10 - x4) <= 0.08f * x10 + 3, "el paseo no depende de los frames por segundo");
  // after a long gap without drawing the critter (another screen in between) it doesn't jump
  {
    float before = beh.x;
    mockSetMillis(100000 + 4000 + 5000);
    render();
    PCHECK(beh.x - before <= 40.0f * UI_F * 0.3f, "un hueco largo sin dibujar no da un paso largo");
  }
  fprintf(stderr, "%d fallos de consumo\n", fails);
  return fails ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "uso: %s carpeta [filtro]\n", argv[0]); return 2; }
  std::string out = argv[1];
  std::string filter = argc > 2 ? argv[2] : "";
  bool tapsMode = filter == "--taps";
  bool fitMode = filter == "--fit";
  bool powerMode = filter == "--power";
  if (tapsMode || fitMode || powerMode) filter = "";
  int drawn = 0;

  mockSerialSilence(true);
  mockSetMillis(100000);
  gOnFrame = [](const uint16_t *px, int w, int h) { gShot.assign(px, px + (size_t)w * h); };
  setup();
  gPristinePet = pet;

  if (tapsMode) return runTapTests();
  if (fitMode) return runFitTests();
  if (powerMode) return runPowerTests();
  for (const Scene &sc : SCENES) {
    if (!filter.empty() && filter != sc.name) continue;
    prepare(sc);
    gShot.clear();
    render();
    if (gShot.empty()) {  // the screen did not flush: what the previous one left is not saved
      fprintf(stderr, "la pantalla '%s' no dibujo nada\n", sc.name);
      return 1;
    }
    // the SD with the sprites is required, otherwise the capture shows the "missing sprites" notice
    bool petScene = !pet.isEgg() && !pet.awaitingStarter() && !galleryOpen && !sc.noPmd;
    if ((petScene && !pmd.loaded) || (galleryDetail && !galleryPmd.loaded) || (galleryOpen && !thumbs.loaded) ||
        (pet.awaitingStarter() && !thumbs.loaded)) {
      fprintf(stderr, "la pantalla '%s' no cargo los sprites de la SD (%s)\n", sc.name, SD_MMC.root().c_str());
      return 1;
    }
    savePpm(out + "/" + sc.name + ".ppm");
    drawn++;
  }
  if (!drawn) { fprintf(stderr, "ninguna pantalla coincide con '%s'\n", filter.c_str()); return 1; }
  return 0;
}
