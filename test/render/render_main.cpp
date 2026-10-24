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
  { "release_dialog", [] { closeAll(); adult(6); confirmUntil = millis() + 10000; }, false, false },
  { "evolve_dialog", [] { closeAll(); adult(4); pet.ageMinutes = 60 * 20; choiceKind = 1; choiceUntil = millis() + 12000; }, false, false },
  { "farewell_dialog", [] { closeAll(); adult(6); pet.ageMinutes = FAREWELL_AGE_MIN + 100; choiceKind = 2; choiceUntil = millis() + 12000; }, false, false },
  { "cta_evolve", [] { closeAll(); adult(4); pet.ageMinutes = 60 * 20; }, false, false },
  { "cta_farewell", [] { closeAll(); adult(6); pet.ageMinutes = FAREWELL_AGE_MIN + 100; }, false, false },
  { "cta_runaway", [] { closeAll(); adult(6); pet.dbgRunawayReady(); }, false, false },
  { "celebration_medal", [] { closeAll(); adult(6); pet.newMedal = MED_LV10; pet.medalUntil = millis() + 4000; }, false, false },
  { "celebration_streak", [] { closeAll(); adult(6); pet.streak = 7; pet.milestoneUntil = millis() + 4000; }, false, false },
  { "eating", [] { closeAll(); adult(6); pet.eatUntil = millis() + 2000; }, false, false },
  { "heart", [] { closeAll(); adult(6); pet.heartUntil = millis() + 1000; }, false, false },
  { "poops_hungry", [] { closeAll(); adult(6); pet.poops = 3; pet.fullness = 20; }, false, false },
  { "battery_low", [] { closeAll(); adult(6); gBatPercent = 10; }, false, false },
  { "battery_charging", [] { closeAll(); adult(6); gBatCharging = true; }, false, false },
  { "bath", [] { closeAll(); adult(6); startBath(); bathUntil = millis() + 2000; }, false, false },
  { "bath_pop", [] { closeAll(); adult(6); startBath(); bathUntil = millis() + 500; }, false, false },
  { "evolving", [] { closeAll(); adult(6); pet.prevSpeciesId = 5; pet.evolveUntil = millis() + EVOLVE_ANIM_MS / 2; evoPmd.load(5, false); }, false, false },
  { "runaway", [] { closeAll(); adult(6); pet.startRunaway(); }, false, false },
  { "egg_rare_cracked", [] { closeAll(); pet.speciesId = -1; pet.eggTaps = 2; pet.eggTarget = 150; }, false, false },
  { "main_flash", [] { closeAll(); adult(4); }, false, true },
  { "main_nosprites", [] { closeAll(); adult(25); }, false, true },
  { "card_medals", [] { closeAll(); adult(6); cardOpen = true; cardPage = 2; }, false, false },
  { "card_progress", [] { closeAll(); adult(6); cardOpen = true; cardPage = 3; }, false, false },
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
  int drawn = 0;
  if (lang < 0 || lang >= LANG_COUNT) { fprintf(stderr, "idioma fuera de rango: %d\n", lang); return 2; }

  mockSerialSilence(true);
  mockSetMillis(100000);
  gOnFrame = [](const uint16_t *px, int w, int h) { gShot.assign(px, px + (size_t)w * h); };
  setup();
  gLang = (Lang)lang;
  applyLangFont();
  gPristinePet = pet;

  std::vector<const Scene *> all;
  for (const Scene &sc : SCENES) all.push_back(&sc);
  if (lang == LANG_EN)
    for (const Scene &sc : EXTRA) all.push_back(&sc);
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
