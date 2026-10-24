#include "i18n.h"
#include "pet.h"        // MED_COUNT

// Tabla de cadenas por id. Sin acentos ni enes: la fuente bitmap del firmware no los tiene.
static const char *const STRINGS[] = {
  "Evolving!", "Yum yum!", "It likes it!", "It's hungry!", "Needs a bath!",
  "Worn out...", "Feeling sad...", "A bit chubby...", "It's SHINY!!", "It's happy",
  "THANKS! Farewell", "It ran away...", "Bye! Waving goodbye...",
  "EGG", "Legendary egg!?", "Rare egg!", "Tap the egg...", "It moves!", "Almost there!",
  "POKEDEX %u/151",
  "%s%s Lv.%u",
  "Release %s?", "Release", "Keep",
  "%u HITS", "STR +%u", "NEW RECORD!", "BEST: %u", "HIT FAST!",
  "SCORE: %u", "So much fun!", "+happiness",
  "Set time", "HOUR", "MIN", "Cancel",
  "MEDAL!", "AWESOME!", "%u DAY STREAK!",
  "STREAK %u  best %u", "BOND", "BERRY ???", "RED BERRY", "BLUE BERRY", "GREEN BERRY",
  "%s   AGE %lud", "tap name: rename",
  "BATTLE", "ATK", "DEF", "SPD", "WGT", "TRAIN STRENGTH",
  "MEDALS %d/%d", "Back",
  "NAME:", "Back",
  "FOOD", "JOY", "ENE", "HYG",
  "BEST %u",
  "PROGRESS", "Lv.%u", "%u min to Lv.%u", "EVOLUTION", "Final form",
  "Ready to evolve!", "All needs >=40 to evolve",
  "Evolves in %u lv.", "Slip-ups: %u",
  "Sound on", "Sound off",
  "EVOLVE!", "%s wants to tell you...", "%s feels abandoned...",
  "Evolve?", "Keep form", "Say goodbye?", "Goodbye", "Stay together",
  "Choose your starter",
  "No sprites", "Load them onto the SD",
};

// Nombre de medalla (banner) y descripcion larga [medalla].
static const char *const MED_NAME[] = { "Lv.10", "Lv.25", "Lv.50", "BERRY", "7 STREAK", "BOND", "TOP FORM", "IN SHAPE" };
static const char *const MED_DSC[] = {
  "LEVEL 10", "LEVEL 25", "LEVEL 50", "BERRY FOUND", "7 DAY STREAK", "MAX BOND", "FINAL FORM", "IN SHAPE",
};

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
// con el tamano sin declarar, una tabla corta o larga ya no pasa en silencio
static_assert(COUNT_OF(STRINGS) == STR_COUNT, "STRINGS y StrId no coinciden");
static_assert(COUNT_OF(MED_NAME) == MED_COUNT, "MED_NAME y MED_COUNT no coinciden");
static_assert(COUNT_OF(MED_DSC) == MED_COUNT, "MED_DSC y MED_COUNT no coinciden");

const char *T(StrId id) { return STRINGS[id]; }
const char *medalName(int i)  { return MED_NAME[i]; }
const char *medalDesc(int i)  { return MED_DSC[i]; }
