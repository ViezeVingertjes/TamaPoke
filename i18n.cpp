#include "i18n.h"
#include "pet.h"        // MED_COUNT

// Tabla de cadenas por id. Sin acentos ni enes: la fuente bitmap del firmware no los tiene.
static const char *const STRINGS[STR_COUNT] = {
  "Evolving!", "Yum yum!", "It likes it!", "It's hungry!", "Needs a bath!",
  "Worn out...", "Feeling sad...", "A bit chubby...", "It's SHINY!!", "It's happy",
  "THANKS! Farewell", "It ran away...", "Bye! Waving goodbye...",
  "EGG", "Legendary egg!?", "Rare egg!", "Tap the egg...", "It moves!", "Almost there!",
  "POKEDEX %u/151",
  "%s%s Lv.%u",
  "Release %s?", "YES", "NO",
  "%u HITS", "STR +%u", "NEW RECORD!", "BEST: %u", "HIT FAST!",
  "SCORE: %u", "So much fun!", "+happiness",
  "SET TIME", "HOUR", "MIN", "swipe up: cancel",
  "MEDAL!", "AWESOME!", "%u DAY STREAK!",
  "STREAK %u  best %u", "BOND", "BERRY ???", "RED BERRY", "BLUE BERRY", "GREEN BERRY",
  "%s   AGE %lud", "tap name: rename",
  "BATTLE", "ATK", "DEF", "SPD", "WGT", "TRAIN STRENGTH",
  "MEDALS %d/%d", "tap: back",
  "NAME:", "tap to go back",
  "FOOD", "JOY", "ENE", "HYG",
  "BEST %u",
  "PROGRESS", "Lv.%u", "%u min to Lv.%u", "EVOLUTION", "Final form",
  "Ready to evolve!", "All needs >=40 to evolve",
  "Evolves in %u lv.", "Slip-ups: %u",
  "SND ON", "SND OFF",
  "EVOLVE!", "%s wants to tell you...", "%s feels abandoned...",
  "Evolve?", "Keep form", "Say goodbye?", "Goodbye", "Stay together",
  "Choose your starter",
  "No sprites", "Load them onto the SD",
};

// Nombre de medalla (banner) y descripcion larga [medalla].
static const char *const MED_NAME[MED_COUNT] = { "Lv.10", "Lv.25", "Lv.50", "BERRY", "7 STREAK", "BOND", "TOP FORM", "IN SHAPE" };
static const char *const MED_DSC[MED_COUNT] = {
  "LEVEL 10", "LEVEL 25", "LEVEL 50", "BERRY FOUND", "7 DAY STREAK", "MAX BOND", "FINAL FORM", "IN SHAPE",
};

const char *T(StrId id) { return STRINGS[id]; }
const char *medalName(int i)  { return MED_NAME[i]; }
const char *medalDesc(int i)  { return MED_DSC[i]; }
