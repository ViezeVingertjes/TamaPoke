// Renderer del host: compila el sketch tal cual (TamaPoke.ino) contra un lienzo
// por software, lleva el juego a cada pantalla y guarda la captura como PPM.
//   render_main <carpeta_salida> [idioma 0-7] [filtro]
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include "shim/Arduino.h"
#include "shim/SD_MMC.h"
// las escenas ponen directamente los temporizadores y contadores internos del bicho
// (comer, evolucionar, medallas...): se abre pet.h solo aqui
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
    int r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;  // 5/6/5 bits -> 8, sin oscurecer
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

// Los datos son los de un bicho adulto bien cuidado; todo fijo para que la imagen
// no cambie entre ejecuciones.
static void adult(int dex) {
  pet.speciesId = dex;
  pet.ageMinutes = 60 * 40;
  pet.fullness = 70; pet.joy = 80; pet.energy = 60; pet.hygiene = 90;
  pet.sleeping = false;
  pet.streak = 12; pet.bond = 60;
  pet.medals = MED_LV10 | MED_LV25 | MED_BERRY;
  memset(pet.dexReg, 0, sizeof(pet.dexReg));
  for (int d = 1; d <= 151; d++)  // todos criados menos el #3 y el #151: salen "???" y siluetas
    if (d != 3 && d != 151) pet.dexReg[(d - 1) >> 3] |= 1 << ((d - 1) & 7);
}

// primer Pokemon de cada bioma, para ver los paisajes
static int dexInBiome(int b) {
  for (int d = 1; d <= 151; d++)
    if (DEX_TBL[d].biome == b) return d;
  return 1;
}

static void atHour(int h) { pet.lastSeenEpoch = 1767225600UL + h * 3600UL; }

struct Scene {
  const char *name;
  void (*setup)();
  bool coreLangs;  // true: se renderiza en todos los idiomas; false: solo en ingles
  bool noPmd;      // el sprite sale de flash (sin PMD de la SD)
};

static const Scene SCENES[] = {
  { "starter", [] { closeAll(); }, true, false },  // primera partida: aun no ha elegido inicial
  { "egg", [] { closeAll(); pet.speciesId = -1; }, true, false },
  { "main_day", [] { closeAll(); adult(6); }, true, false },
  { "main_night", [] { closeAll(); adult(6); pet.sleeping = true; }, true, false },
  { "gallery", [] { closeAll(); adult(6); galleryOpen = true; galleryPage = 0; galleryDirty = true; }, true, false },
  { "gallery_detail", [] { closeAll(); adult(6); galleryOpen = true; galleryDetail = 25; galleryPmd.load(25, false); }, true, false },
  { "card_profile", [] { closeAll(); adult(6); cardOpen = true; cardPage = 0; }, true, false },
  { "card_stats", [] { closeAll(); adult(6); cardOpen = true; cardPage = 1; }, true, false },
  { "keyboard", [] { closeAll(); adult(6); kbOpen = true; strcpy(nameBuf, "ASH"); nameLen = 3; }, true, false },
  { "clock", [] { closeAll(); adult(6); clockOpen = true; clockH = 14; clockM = 35; }, true, false },
  { "game", [] { closeAll(); adult(25); gameOpen = true; ballX = CX; ballY = 150; ballVX = ballVY = 0; gamePetX = CX; gameScore = 7; gameMisses = 1; }, true, false },
  { "sack", [] { closeAll(); adult(25); sackOpen = true; sackUntil = millis() + 8000; sackHits = 14; }, true, false },
  { "farewell", [] { closeAll(); adult(6); pet.startFarewell(); }, true, false },
};

// pantallas adicionales (solo en ingles): dialogos, botones de final, efectos,
// paginas de la ficha, sprites de respaldo...
static const Scene EXTRA[] = {
  { "feed_menu", [] { closeAll(); adult(6); feedMenuUntil = millis() + 6000; }, false, false },
  { "release_dialog", [] { closeAll(); adult(6); confirmUntil = millis() + 10000; }, true, false },
  { "evolve_dialog", [] { closeAll(); adult(4); pet.ageMinutes = 60 * 20; choiceKind = 1; choiceUntil = millis() + 12000; }, true, false },
  { "farewell_dialog", [] { closeAll(); adult(6); pet.ageMinutes = FAREWELL_AGE_MIN + 100; choiceKind = 2; choiceUntil = millis() + 12000; }, true, false },
  { "cta_evolve", [] { closeAll(); adult(4); pet.ageMinutes = 60 * 20; }, true, false },
  { "cta_farewell", [] { closeAll(); adult(6); pet.ageMinutes = FAREWELL_AGE_MIN + 100; }, true, false },
  { "cta_runaway", [] { closeAll(); adult(6); pet.dbgRunawayReady(); }, true, false },
  { "celebration_medal", [] { closeAll(); adult(6); pet.newMedal = MED_LV10; pet.medalUntil = millis() + 4000; }, true, false },
  { "celebration_streak", [] { closeAll(); adult(6); pet.streak = 7; pet.milestoneUntil = millis() + 4000; }, true, false },
  { "eating", [] { closeAll(); adult(6); pet.eatUntil = millis() + 2000; }, false, false },
  { "heart", [] { closeAll(); adult(6); pet.heartUntil = millis() + 1000; }, false, false },
  { "poops_hungry", [] { closeAll(); adult(6); pet.poops = 3; pet.fullness = 20; }, false, false },
  { "battery_low", [] { closeAll(); adult(6); gBatPercent = 10; }, false, false },
  { "battery_charging", [] { closeAll(); adult(6); gBatCharging = true; }, false, false },
  { "bath", [] { closeAll(); adult(6); startBath(); bathUntil = millis() + 2000; }, false, false },
  { "bath_pop", [] { closeAll(); adult(6); startBath(); bathUntil = millis() + 500; }, false, false },
  { "evolving", [] { closeAll(); adult(6); pet.prevSpeciesId = 5; pet.evolveUntil = millis() + EVOLVE_ANIM_MS / 2; evoPmd.load(5, false); }, false, false },
  { "runaway", [] { closeAll(); adult(6); pet.startRunaway(); }, false, false },
  { "egg_rare_cracked", [] { closeAll(); pet.speciesId = -1; pet.eggTaps = 2; pet.eggTarget = 150; }, true, false },
  { "main_flash", [] { closeAll(); adult(4); }, false, true },
  { "main_nosprites", [] { closeAll(); adult(25); }, false, true },
  { "card_medals", [] { closeAll(); adult(6); cardOpen = true; cardPage = 2; }, true, false },
  { "card_progress", [] { closeAll(); adult(6); cardOpen = true; cardPage = 3; }, true, false },
  { "gallery_page6", [] { closeAll(); adult(6); galleryOpen = true; galleryPage = 6; galleryDirty = true; }, false, false },
  { "gallery_detail_unknown", [] { closeAll(); adult(6); galleryOpen = true; galleryDetail = 3; galleryPmd.load(3, false); }, false, false },
  { "game_over", [] { closeAll(); adult(25); gameOpen = true; gameScore = 12; gameMisses = 3; gameNewHi = true; gameOverUntil = millis() + 4000; gamePetX = CX; ballX = CX; ballY = 300; ballVX = ballVY = 0; }, false, false },
  { "sack_over", [] { closeAll(); adult(25); sackOpen = true; sackUntil = millis() - 1; sackOverUntil = millis() + 3000; sackHits = 31; sackGain = 2; sackNewHi = true; }, false, false },
  { "scene_dawn", [] { closeAll(); adult(dexInBiome(0)); atHour(7); }, false, false },
  { "scene_dusk", [] { closeAll(); adult(dexInBiome(0)); atHour(19); }, false, false },
  { "biome_beach", [] { closeAll(); adult(dexInBiome(1)); }, false, false },
  { "biome_forest", [] { closeAll(); adult(dexInBiome(2)); }, false, false },
  { "biome_volcano", [] { closeAll(); adult(dexInBiome(3)); }, false, false },
  { "biome_mountain", [] { closeAll(); adult(dexInBiome(4)); }, false, false },
  { "biome_snow", [] { closeAll(); adult(dexInBiome(5)); }, false, false },
  { "biome_beach_night", [] { closeAll(); adult(dexInBiome(1)); pet.sleeping = true; }, false, false },
};

static Pet gPristinePet;  // el bicho justo despues de setup()

// Lleva el juego a la escena. Cada pantalla parte del mismo estado: sin esto la
// imagen dependeria de que escenas corrieron antes (animacion del bicho,
// secuencia de random()).
static void prepare(const Scene &sc) {
  gBatPercent = 80; gBatCharging = false; pet.lastSeenEpoch = 0;
  feedMenuUntil = confirmUntil = bathUntil = 0; bathPending = false; choiceKind = 0;
  pet = gPristinePet;  // el bicho entero (temporizadores, pokedex, medallas...) como tras el arranque
  gameOverUntil = sackOverUntil = 0; gameNewHi = sackNewHi = false; sackGain = 0;
  randomSeed(1);
  monFor = -2;
  beh.mode = 0; beh.act = PMD_IDLE; beh.t0 = 0; beh.until = 0; beh.x = beh.targetX = CX;
  mockSetMillis(100000);
  if (std::string(sc.name) != "starter") pet.chooseStarter(1);
  sc.setup();
  ensureMon();
  if (sc.noPmd) pmd.unload();  // fuerza el sprite de flash / el aviso de "faltan sprites"
  mockSetMillis(100000);
}

// ---------- pruebas de toque: se toca el centro de cada elemento dibujado ----------
static int gTapFails = 0;
static const Scene *findScene(const char *name) {
  for (const Scene &sc : SCENES) if (!strcmp(sc.name, name)) return &sc;
  for (const Scene &sc : EXTRA) if (!strcmp(sc.name, name)) return &sc;
  fprintf(stderr, "no existe la escena '%s'\n", name);
  exit(2);
}
#define EXPECT(cond, what) do { if (!(cond)) { fprintf(stderr, "FALLA %-22s %s\n", curScene, what); gTapFails++; } } while (0)
static const char *curScene = "";
static void begin(const char *name) { curScene = name; prepare(*findScene(name)); }
static void tapAt(int x, int y) {
  // handleTouch() desvia el saco antes de onTap(): se usa su misma funcion
  if (sackOpen) { sackTouch((int16_t)y); return; }
  onTap((int16_t)x, (int16_t)y);
}

static int runTapTests() {
  // selector de comida: el centro de cada icono elige su casilla
  for (int i = 0; i < 4; i++) {
    begin("feed_menu");
    uint8_t w0 = pet.weight;
    int is = SC(3);
    tapAt(FEED_ICON_X(i) + 8 * is, FEED_ICON_Y + 8 * is);
    EXPECT(feedMenuUntil == 0, "el selector se cierra");
    EXPECT(pet.eating(), "el bicho come");
    // el #6 (Charizard) tiene por baya favorita la roja (casilla 0): solo esa da el corazon
    if (i == 0) EXPECT(pet.berryKnown, "la casilla 1 es la baya roja (favorita)");
    else EXPECT(!pet.berryKnown, "las casillas 2 y 3 no son la baya favorita");
    if (i == 3) EXPECT(pet.weight > w0, "la casilla 4 es la chuche (engorda)");
    else EXPECT(pet.weight == w0, "una baya no engorda");
  }
  // el borde derecho del recuadro cuenta como la ultima casilla (la chuche)
  begin("feed_menu");
  uint8_t w1 = pet.weight;
  tapAt(FEED_X + FEED_W, FEED_Y + FEED_H / 2);
  EXPECT(pet.weight > w1, "el ultimo pixel del selector es la chuche");
  // fuera del recuadro: se cierra sin dar de comer
  begin("feed_menu");
  tapAt(FEED_X - 5, FEED_Y + FEED_H / 2);
  EXPECT(feedMenuUntil == 0 && !pet.eating(), "tocar fuera cierra el selector sin comer");
  // dialogo de soltar: SI suelta; cualquier otro toque lo cierra sin soltar
  begin("release_dialog");
  tapAt(YES_X + YES_W / 2, YES_Y + YES_H / 2);
  EXPECT(pet.ceremony == CER_RELEASE, "SI suelta al bicho");
  begin("release_dialog");
  tapAt(NO_X + YES_W / 2, YES_Y + YES_H / 2);
  EXPECT(pet.ceremony == CER_NONE && !confirmUntil, "tocar NO cierra el dialogo sin soltar");
  // dialogos de decision
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
  // botones de final
  begin("cta_evolve");
  tapAt(EVO_BTN_X + EVO_BTN_W / 2, EVO_BTN_Y + EVO_BTN_H / 2);
  EXPECT(choiceKind == 1, "boton de evolucion abre el dialogo");
  begin("cta_farewell");
  tapAt(FAR_BTN_X + FAR_BTN_W / 2, FAR_BTN_Y + FAR_BTN_H / 2);
  EXPECT(choiceKind == 2, "boton de despedida abre el dialogo");
  begin("cta_runaway");
  tapAt(FAR_BTN_X + FAR_BTN_W / 2, FAR_BTN_Y + FAR_BTN_H / 2);
  EXPECT(pet.ceremony == CER_RUNAWAY, "boton de escapada");
  // botones de accion
  for (int i = 0; i < 4; i++) {
    begin("main_day");
    bool slept = pet.sleeping;
    tapAt(buttons[i].cx, buttons[i].cy);
    if (i == 0) EXPECT(feedMenuUntil != 0, "comer abre el selector");
    if (i == 1) EXPECT(gameOpen, "jugar abre el minijuego");
    if (i == 2) EXPECT(pet.sleeping != slept, "luz cambia el sueno");
    if (i == 3) EXPECT(bathUntil != 0, "bano empieza");
  }
  // caricia: el centro del sprite cae dentro de la zona del bicho
  begin("main_day");
  tapAt(CX, PET_CY);
  EXPECT(pet.showHeart(), "caricia al bicho");
  begin("main_day");  // y un toque sobre las barras de estado NO es una caricia
  tapAt(CX, UY(332));
  EXPECT(!pet.showHeart(), "las barras no acarician");
  // eleccion de inicial: el centro de cada fila elige su Pokemon
  for (int i = 0; i < 3; i++) {
    begin("starter");
    EXPECT(pet.awaitingStarter(), "la escena parte de la eleccion de inicial");
    tapAt(STARTER_ROW_X + STARTER_ROW_W / 2, STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP) + STARTER_ROW_H / 2);
    EXPECT(!pet.awaitingStarter() && pet.eggTarget == STARTER_DEX[i], "la fila elige su inicial");
  }
  begin("starter");  // el hueco entre filas y fuera de ellas no elige nada
  tapAt(STARTER_ROW_X + STARTER_ROW_W / 2, STARTER_ROW_Y + STARTER_ROW_H + STARTER_ROW_GAP / 2);
  tapAt(STARTER_ROW_X - U(10), STARTER_ROW_Y + STARTER_ROW_H / 2);
  EXPECT(pet.awaitingStarter(), "tocar fuera de las filas no elige");
  // galeria: cada celda abre su detalle; la cabecera sale; el detalle vuelve a la rejilla
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
  tapAt(GAL_X + GAL_CELL / 2, HEADER_ZONE_H + (GAL_Y - HEADER_ZONE_H) / 2);  // entre la cabecera y la rejilla
  EXPECT(galleryOpen && !galleryDetail, "el hueco bajo la cabecera no hace nada");
  begin("gallery_detail");
  tapAt(CX, CY);
  EXPECT(galleryOpen && !galleryDetail, "tocar el detalle vuelve a la rejilla");
  // ficha: el nombre abre el teclado, ENTRENAR FUERZA abre el saco
  begin("card_profile");
  tapAt(CX, CARD_NAME_ZONE_H - 1);
  EXPECT(kbOpen, "tocar el nombre abre el teclado");
  begin("card_profile");
  tapAt(CX, CARD_NAME_ZONE_H + UY(40));
  EXPECT(!kbOpen && !cardOpen, "tocar bajo el nombre cierra la ficha");
  begin("card_stats");
  tapAt(TRAIN_X + TRAIN_W / 2, TRAIN_Y + TRAIN_H / 2);
  EXPECT(sackOpen && !cardOpen, "ENTRENAR FUERZA abre el saco");
  begin("card_stats");
  tapAt(TRAIN_X + TRAIN_W / 2, TRAIN_Y - UY(12));
  EXPECT(!sackOpen && !cardOpen, "tocar fuera del boton cierra la ficha sin entrenar");
  // teclado: cada tecla (su centro) escribe su letra; <- borra; OK guarda
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
  // reloj: botones +/-, sonido, idioma y OK
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
  tapAt(SND_PILL_X + SND_PILL_W / 2, LANG_PILL_Y + LANG_PILL_H / 2);
  EXPECT(audioEnabled() != snd, "interruptor de sonido");
  audioSetEnabled(snd);
  Lang lg = gLang;
  tapAt(LANG_PILL_X + LANG_PILL_W / 2, LANG_PILL_Y + LANG_PILL_H / 2);
  EXPECT(gLang != lg, "cicla el idioma");
  setLang(lg); applyLangFont();
  tapAt(CLOCK_OK_X + CLOCK_OK_W / 2, CLOCK_OK_Y + CLOCK_OK_H / 2);
  EXPECT(!clockOpen, "OK cierra el reloj");
  // minijuego de la bola: tocar la bola puntua, fuera no, la cabecera abandona
  begin("game");
  uint8_t sc0 = gameScore;
  tapAt((int)ballX, (int)ballY);
  EXPECT(gameScore == sc0 + 1 && ballVY < 0, "tocar la bola puntua y la lanza hacia arriba");
  begin("game");
  tapAt((int)ballX + U(120), (int)ballY + UY(120));
  EXPECT(gameScore == sc0, "tocar lejos de la bola no puntua");
  begin("game");
  tapAt(CX, UY(30));
  EXPECT(!gameOpen, "tocar la cabecera abandona el minijuego");
  begin("game");
  for (int i = 0; i < 50; i++) {  // la bola reaparece siempre dentro de su zona y a su altura
    respawnBall();
    EXPECT(ballX >= U(150) && ballX < U(150) + U(166) && ballY == UY(96), "la bola reaparece en su zona");
    EXPECT(fabsf(ballVX) >= 1.6f * UI_F - 0.001f && fabsf(ballVX) <= 4.0f * UI_F + 0.001f, "velocidad inicial proporcional");
  }
  // radio de toque: 0,9 de U(74) da, 1,1 no
  begin("game");
  tapAt((int)ballX + (int)(0.9f * U(74)), (int)ballY);
  EXPECT(gameScore == sc0 + 1, "un toque a 0,9 del radio puntua");
  begin("game");
  tapAt((int)ballX + (int)(1.1f * U(74)), (int)ballY);
  EXPECT(gameScore == sc0, "un toque a 1,1 del radio no puntua");
  // trayectoria proporcional: la altura del salto de una bola golpeada, dividida
  // por UI_F, es la de diseno (simulada aqui con las constantes del juego a 466)
  begin("game");
  gameScore = 7; gameMisses = 0; ballX = CX; ballY = UY(250); ballVX = ballVY = 0; lastGameStep = 0;
  tapAt((int)ballX, (int)ballY);  // golpe: gameScore pasa a 8
  float y0 = ballY, top = ballY;
  for (int i = 0; i < 40; i++) { mockAdvanceMillis(85); stepGame(); if (ballY < top) top = ballY; }
  float v = -(6.6f + 8 * 0.22f), g = 0.40f + 8 * 0.013f, yy = 0, ref = 0;  // diseno a 466
  for (int i = 0; i < 40; i++) { v += g; yy += v; if (yy < ref) ref = yy; }
  float rise = (y0 - top) / UI_F, refRise = -ref;
  EXPECT(fabsf(rise - refRise) <= 0.04f * refRise + 3, "el salto de la bola es proporcional al panel");
  // la pared: una bola que sale disparada contra el borde rebota y no lo cruza
  begin("game");
  gameScore = 3; ballX = CX + BALL_R - U(2); ballY = CY; ballVX = 5 * UI_F; ballVY = 0; lastGameStep = 0;
  mockAdvanceMillis(85); stepGame();
  float wdx = ballX - CX, wdy = ballY - CY;
  EXPECT(sqrtf(wdx * wdx + wdy * wdy) <= BALL_R + 0.5f, "la bola no cruza la pared");
  EXPECT(ballVX < 0, "la pared devuelve la bola");
  // saco de entrenamiento: tocar golpea, la cabecera abandona
  begin("sack");
  uint16_t h0 = sackHits;
  tapAt(CX, UY(200));
  EXPECT(sackHits == h0 + 1, "tocar el saco cuenta un golpe");
  begin("sack");
  tapAt(CX, UY(30));
  EXPECT(!sackOpen, "tocar la cabecera abandona el saco");
  fprintf(stderr, "%d fallos de toque\n", gTapFails);
  return gTapFails ? 1 : 0;
}

// ---------- medida de texto: textW() debe ser lo que de verdad avanza el cursor ----------
static int runMeasureTests() {
  int fails = 0, checked = 0;
  gfx->setTextWrap(false);
  for (Lang lg : { LANG_JA, LANG_KO }) {
    setLang(lg);
    applyLangFont();
    for (int size = 1; size <= 4; size++) {
      std::vector<const char *> all;
      for (int id = 0; id < STR_COUNT; id++) all.push_back(T((StrId)id));
      for (int i = 0; i < MED_COUNT; i++) { all.push_back(medalName(i)); all.push_back(medalLabel(i)); all.push_back(medalDesc(i)); }
      for (int d = 1; d <= 151; d++) all.push_back(dexName(d));
      for (const char *t : all) {
        if (!t || !*t) continue;
        setSize(size);
        int w = textW(t, size);
        gfx->setCursor(0, 100);
        gfx->print(t);
        int adv = gfx->getCursorX();
        checked++;
        if (w != adv) {
          if (fails < 100000) fprintf(stderr, "FALLA %s tam %d: textW=%d, el cursor avanza %d  '%s'\n", lg == LANG_JA ? "JA" : "KO", size, w, adv, t);
          fails++;
        }
      }
    }
  }
  // UTF-8 mal formado o fuera de la fuente: textW() debe coincidir tambien con lo que pinta la libreria
  // (los caracteres que no estan en la fuente avanzan 0 en la libreria y textW() los cuenta:
  // solo afecta a texto fuera de las tablas de traduccion, que no se usa)
  static const char *const oddJa[] = {
    "\xE3", "\xE3\x81", "ab\xE3", "\xE3\x81\x82\xE3\x81", "\xE3\x81\x82!\xEF\xBC\x81", "a\xE3\x81\x82",
  };
  static const char *const oddKo[] = {  // el mismo patron con una silaba hangul (U+AC00)
    "\xEA", "\xEA\xB0", "ab\xEA", "\xEA\xB0\x80\xEA\xB0", "\xEA\xB0\x80!?", "a\xEA\xB0\x80",
  };
  for (Lang lg : { LANG_JA, LANG_KO }) {
    setLang(lg);
    applyLangFont();
    for (const char *t : (lg == LANG_JA ? oddJa : oddKo)) {
      for (int size = 1; size <= 4; size++) {
        setSize(size);
        // el decodificador de la libreria conserva su estado UTF-8 entre llamadas: una
        // secuencia truncada se traga los bytes siguientes. Se vacia antes de cada caso.
        gfx->setCursor(0, 100);
        gfx->print("aaaaaaa");
        gfx->setCursor(0, 100);
        gfx->print(t);
        checked++;
        if ((int)textW(t, size) != gfx->getCursorX()) {
          fprintf(stderr, "FALLA %s tam %d (cadena rara %d bytes, primero 0x%02X): textW=%d, el cursor avanza %d\n", lg == LANG_JA ? "JA" : "KO", size, (int)strlen(t), (unsigned char)t[0], (int)textW(t, size), (int)gfx->getCursorX());
          fails++;
        }
      }
    }
  }
  fprintf(stderr, "%d fallos de medida (%d cadenas)\n", fails, checked);
  return fails ? 1 : 0;
}

// ---------- ajuste de textos: cada cadena real de cada idioma cabe donde se dibuja ----------
static int gFitFails = 0;
static const char *gFitLang = "";
// texto a escala `size` (TS ya aplicado) en la linea y: debe caber en maxW (px)
static void fitBox(const char *what, const char *text, int size, int maxW) {
  setSize(size);
  int w = textW(text, size);
  if (w > maxW) {
    fprintf(stderr, "FALLA %-3s %-22s %3d px > %3d  '%s'\n", gFitLang, what, w, maxW, text);
    gFitFails++;
  }
}
// ... o en la cuerda visible del circulo a la altura de esa linea (arriba y abajo)
static void fitChord(const char *what, const char *text, int size, int y) {
  setSize(size);
  int top = chordW(y), bot = chordW(y + textH());
  fitBox(what, text, size, (top < bot ? top : bot) - U(8));
}
// texto partido en <= maxLines lineas (como drawWrapped): cada linea cabe y no se pierde nada
static void fitWrapped(const char *what, const char *text, int size, int maxW, int maxLines, int maxH = 1 << 20) {
  setSize(size);
  int st[4], ln[4];
  int n = wrapText(text, size, maxW, maxLines, st, ln);
  int L = (int)strlen(text);
  while (L > 0 && text[L - 1] == ' ') L--;
  bool ok = n > 0 && st[n - 1] + ln[n - 1] == L;
  if (n * textH() + (n - 1) * U(4) > maxH) ok = false;  // el bloque tambien tiene que caber en su caja
  for (int i = 0; i < n; i++) {
    char line[96]; memcpy(line, text + st[i], ln[i]); line[ln[i]] = 0;
    if (textW(line, size) > maxW) ok = false;
  }
  if (!ok) {
    fprintf(stderr, "FALLA %-3s %-22s no cabe en %d lineas de %d px  '%s'\n", gFitLang, what, maxLines, maxW, text);
    gFitFails++;
  }
}
// el peor nombre de especie del idioma
static const char *longestName() {
  const char *best = "";
  setSize(TS(2));
  for (int d = 1; d <= 151; d++)
    if (textW(dexName(d), TS(2)) > textW(best, TS(2))) best = dexName(d);
  return best;
}

// wrapText: cortes en el limite exacto (la palabra acaba justo en maxW)
static int runWrapTests() {
  int fails = 0;
  setLang(LANG_EN);
  applyLangFont();
  setSize(1);  // 6 px por caracter
  struct Case { const char *text; int maxW; int lines; const char *l0; const char *l1; };
  static const Case cases[] = {
    { "AA AA BBBBB", 30, 2, "AA AA", "BBBBB" },       // la primera linea llena justo; el espacio ya no cabe
    { "AA BB", 100, 1, "AA BB", "" },
    { "ABCDEFGHIJ", 30, 2, "ABCDE", "FGHIJ" },         // sin espacios: corta por caracteres
    { "  HOLA   MUNDO  ", 60, 2, "HOLA", "MUNDO" },    // espacios sobrantes
    { "", 60, 0, "", "" },
  };
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
  static const Lang langs[] = { LANG_ES, LANG_EN, LANG_FR, LANG_DE, LANG_IT, LANG_PT, LANG_JA, LANG_KO };
  static const char *const names[] = { "ES", "EN", "FR", "DE", "IT", "PT", "JA", "KO" };
  for (int li = 0; li < 8; li++) {
    setLang(langs[li]);
    applyLangFont();
    gFitLang = names[li];
    char b[96];
    const char *ln = longestName();
    // --- escena principal (todo el ancho) ---
    const char *worst[] = { ln, "ABCDEFGHIJK" };  // el nombre mas largo y un apodo de 11 letras (nick[12])
    for (const char *nm : worst) {
      snprintf(b, sizeof(b), T(S_NAME_FMT), "*", nm, 999);
      int hs = fitSize(b, TS(3), TS(2), UY(52));
      if (!fitsChord(b, hs, UY(52))) { fprintf(stderr, "FALLA %-3s cabecera de la escena no cabe en el circulo: '%s'\n", gFitLang, b); gFitFails++; }
    }
    for (StrId id : { S_EVOLVING, S_EATING, S_LIKES, S_HUNGRY, S_NEEDS_BATH, S_EXHAUSTED, S_SAD, S_CHUBBY, S_IS_SHINY, S_HAPPY,
                      S_FAREWELL, S_RUNAWAY, S_GOODBYE, S_EGG_TOUCH, S_EGG_MOVES, S_EGG_ALMOST })
      fitBox("mensaje de estado", T(id), TS(2), UI_W - U(40));
    snprintf(b, sizeof(b), T(S_POKEDEX_FMT), 151);
    fitBox("pokedex del huevo", b, TS(2), UI_W - U(40));
    // --- elegir inicial ---
    fitChord("titulo de inicial", T(S_CHOOSE_STARTER), TS(2), UY(68));
    for (int i = 0; i < 3; i++) fitBox("nombre de inicial en su fila", dexName(STARTER_DEX[i]), TS(3), STARTER_ROW_X + STARTER_ROW_W - U(178) - U(8));
    // --- dialogos ---
    for (const char *nm : worst) {
      snprintf(b, sizeof(b), T(S_RELEASE_FMT), nm);
      fitWrapped("soltar?", b, TS(2), U(278) - U(16), 2, UY(60));
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
    for (int i = 0; i < MED_COUNT; i++) {
      fitWrapped("descripcion de medalla", medalDesc(i), TS(2), MED_W - U(44) - U(6), 2, UY(44) - UY(8));
      fitBox("etiqueta de medalla", medalLabel(i), TS(2), U(100));
    }
    setSize(TS(2));
    fitWrapped("cancelar del reloj", T(S_CLOCK_CANCEL), TS(2), chordAvail(UY(424)), 2);
    // cadenas sueltas que no estaban cubiertas
    { int hs = fitSize(T(S_EGG_HDR), TS(3), TS(2), UY(52));
      if (!fitsChord(T(S_EGG_HDR), hs, UY(52))) { fprintf(stderr, "FALLA %-3s cabecera del huevo no cabe en el circulo\n", gFitLang); gFitFails++; } }
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
    // --- ficha ---
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
    snprintf(b, sizeof(b), T(S_NEXT_LVL_FMT), 60, 999);
    fitChord("siguiente nivel", b, TS(2), UY(190));
    fitChord("etiqueta de evolucion", T(S_EVO_LABEL), TS(2), UY(230));
    for (StrId id : { S_FINAL_FORM, S_EVO_READY, S_EVO_BLOCKED }) fitChord("estado de evolucion", T(id), TS(2), UY(256));
    snprintf(b, sizeof(b), T(S_EVO_IN_FMT), 99);
    fitChord("evolucion en", b, TS(2), UY(256));
    snprintf(b, sizeof(b), T(S_MISTAKES_FMT), 99);
    fitChord("descuidos", b, TS(2), UY(312));
    // --- galeria, teclado, reloj ---
    snprintf(b, sizeof(b), T(S_POKEDEX_FMT), 151);
    fitChord("pokedex", b, TS(2), UY(48));  // el titulo cae a este tamano si no cabe al grande
    snprintf(b, sizeof(b), "N.%03d %s%s", 151, "*", ln);
    fitChord("detalle de galeria (tamano pequeno)", b, TS(2), UY(60));
    fitChord("volver del detalle", T(S_DETAIL_BACK), TS(2), UY(408));
    fitChord("nombre (teclado)", T(S_NAME), TS(2), UY(56));
    fitChord("ajustar hora", T(S_SET_TIME), TS(3), UY(44));
    fitBox("HORA", T(S_HOUR), TS(2), CLOCK_BTN_W * 2 + (CLK_X1 - CLK_X0 - CLOCK_BTN_W));
    fitBox("MIN", T(S_MIN), TS(2), CLOCK_BTN_W * 2 + (CLK_X3 - CLK_X2 - CLOCK_BTN_W));
    fitBox("sonido activado", T(S_SND_ON), TS(2), SND_PILL_W - U(8));
    fitBox("sonido desactivado", T(S_SND_OFF), TS(2), SND_PILL_W - U(8));
    // --- minijuegos ---
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
  }
  gFitFails += runWrapTests();
  fprintf(stderr, "%d textos que no caben\n", gFitFails);
  return gFitFails ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "uso: %s carpeta [idioma] [filtro]\n", argv[0]); return 2; }
  std::string out = argv[1];
  int lang = LANG_EN;
  if (argc > 2) {
    char *end = nullptr;
    long v = strtol(argv[2], &end, 10);
    if (!*argv[2] || *end) { fprintf(stderr, "el idioma debe ser un numero: '%s'\n", argv[2]); return 2; }
    lang = (int)v;
  }
  std::string filter = argc > 3 ? argv[3] : "";
  bool tapsMode = filter == "--taps";
  bool measureMode = filter == "--measure";
  bool fitMode = filter == "--fit";
  if (tapsMode || measureMode || fitMode) filter = "";
  int drawn = 0;
  if (lang < 0 || lang >= LANG_COUNT) { fprintf(stderr, "idioma fuera de rango: %d\n", lang); return 2; }

  mockSerialSilence(true);
  mockSetMillis(100000);
  gOnFrame = [](const uint16_t *px, int w, int h) { gShot.assign(px, px + (size_t)w * h); };
  setup();
  gLang = (Lang)lang;
  applyLangFont();
  gPristinePet = pet;

  if (tapsMode) return runTapTests();
  if (measureMode) return runMeasureTests();
  if (fitMode) return runFitTests();
  std::vector<const Scene *> all;
  for (const Scene &sc : SCENES) all.push_back(&sc);
  for (const Scene &sc : EXTRA)
    if (lang == LANG_EN || sc.coreLangs) all.push_back(&sc);
  for (const Scene *scp : all) {
    const Scene &sc = *scp;
    if (!filter.empty() && filter != sc.name) continue;
    prepare(sc);
    gShot.clear();
    render();
    if (gShot.empty()) {  // la pantalla no hizo flush: no se guarda lo que dejo la anterior
      fprintf(stderr, "la pantalla '%s' no dibujo nada\n", sc.name);
      return 1;
    }
    // sin SD el juego cae a los sprites de flash y la captura engana: se exige la SD
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
