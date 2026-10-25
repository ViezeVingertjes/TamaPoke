#include "i18n.h"
#include "pet.h"        // MED_COUNT, Pet::TRAIL_RANKS

// String table by id. No accents or n-tildes: the firmware's bitmap font does not have them.
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
  "SET TIME", "HOUR", "MIN", "Cancel",
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
  "STEPS", "Next goal: %lu", "All goals done!", "TOTAL %lu",
  "500 steps! Snack!", "2000 steps! Energy!", "5000 steps! Workout!",
  "Found a snack!", "Found a Stone!",
  "Hi, friend!",
  "Done",
};

// Medal name (banner) and long description [medal].
static const char *const MED_NAME[] = { "Lv.10", "Lv.25", "Lv.50", "BERRY", "7 STREAK", "BOND", "TOP FORM", "IN SHAPE" };
static const char *const MED_DSC[] = {
  "LEVEL 10", "LEVEL 25", "LEVEL 50", "BERRY FOUND", "7 DAY STREAK", "MAX BOND", "FINAL FORM", "IN SHAPE",
};

static const char *const TRAIL[] = { "ROOKIE", "WALKER", "HIKER", "TREKKER", "RANGER" };

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
// with the size left undeclared, a table that is too short or too long no longer slips by silently
static_assert(COUNT_OF(STRINGS) == STR_COUNT, "STRINGS y StrId no coinciden");
static_assert(COUNT_OF(MED_NAME) == MED_COUNT, "MED_NAME y MED_COUNT no coinciden");
static_assert(COUNT_OF(TRAIL) == Pet::TRAIL_RANKS + 1, "TRAIL and Pet::TRAIL_RANKS do not match");
// statusMsg() shows S_STEP_REWARD_1 + stepRewardShown(): the finds must follow the goals
static_assert(S_FIND_SNACK == S_STEP_REWARD_1 + Pet::STEP_FIND_SNACK && S_FIND_STONE == S_STEP_REWARD_1 + Pet::STEP_FIND_STONE,
              "walk find strings out of step with Pet::STEP_FIND_*");
static_assert(COUNT_OF(MED_DSC) == MED_COUNT, "MED_DSC y MED_COUNT no coinciden");

const char *T(StrId id) { return STRINGS[id]; }
const char *medalName(int i)  { return MED_NAME[i]; }
const char *medalDesc(int i)  { return MED_DSC[i]; }
const char *trailName(int rank) { return TRAIL[rank]; }
