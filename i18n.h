#pragma once
#include <Arduino.h>

// String IDs. The order must match the STRINGS table in i18n.cpp.
enum StrId : uint8_t {
  // critter status (statusMsg)
  S_EVOLVING, S_EATING, S_LIKES, S_HUNGRY, S_NEEDS_BATH,
  S_EXHAUSTED, S_SAD, S_CHUBBY, S_IS_SHINY, S_HAPPY,
  // farewell ceremonies
  S_FAREWELL, S_RUNAWAY, S_GOODBYE,
  // egg
  S_EGG_HDR, S_EGG_LEGEND, S_EGG_RARE, S_EGG_TOUCH, S_EGG_MOVES, S_EGG_ALMOST,
  // shared formats
  S_POKEDEX_FMT,   // "POKEDEX %u/151"
  S_NAME_FMT,      // "%s%s Lv.%u"
  // release dialog
  S_RELEASE_FMT, S_YES, S_NO,
  // minigame and bag
  S_HITS_FMT, S_STR_GAIN_FMT, S_NEW_RECORD, S_RECORD_FMT, S_HIT_FAST,
  S_SCORE_FMT, S_GREAT_JOY, S_PLUS_JOY,
  // clock / settings
  S_SET_TIME, S_HOUR, S_MIN, S_CLOCK_CANCEL,
  // celebration
  S_MEDAL_BANNER, S_GREAT, S_STREAK_DAYS_FMT,
  // sheet: profile
  S_STREAK_FMT, S_VIN, S_BERRY_UNK, S_BERRY_RED, S_BERRY_BLUE, S_BERRY_GREEN,
  S_INFO_FMT, S_RENAME_HINT,
  // sheet: combat
  S_BATTLE, S_STAT_ATK, S_STAT_DEF, S_STAT_SPE, S_STAT_WGT, S_TRAIN_STR,
  // sheet: medals
  S_MEDALS_FMT, S_BACK,
  // keyboard and gallery
  S_NAME, S_DETAIL_BACK,
  // bars
  S_BAR_FOOD, S_BAR_JOY, S_BAR_ENE, S_BAR_HYG,
  // live minigame scoreboard
  S_REC_FMT,
  // sheet: progress page
  S_PROGRESS, S_LVL_FMT, S_NEXT_LVL_FMT, S_EVO_LABEL, S_FINAL_FORM,
  S_EVO_READY, S_EVO_BLOCKED, S_EVO_IN_FMT, S_MISTAKES_FMT,
  // sound switch (settings)
  S_SND_ON, S_SND_OFF,
  S_EVO_TAP,        // evolve button text
  S_FAREWELL_BTN,   // farewell button text (carries the name: "%s ...")
  S_RUNAWAY_BTN,    // run-away-from-neglect button text (sad ending)
  // decision dialogs (evolve/keep, say goodbye/stay together)
  S_EVO_Q, S_EVO_KEEP, S_FAR_Q, S_FAR_GO, S_FAR_STAY,
  S_CHOOSE_STARTER,  // title of the starter choice (first time)
  S_NO_SPRITES, S_LOAD_SPRITES,  // notice when the sprite is missing on the SD
  // walking: card page and the goal announcements
  S_STEPS, S_STEPS_NEXT_FMT, S_STEPS_ALL_DONE, S_STEPS_TOTAL_FMT,
  S_STEP_REWARD_1, S_STEP_REWARD_2, S_STEP_REWARD_3,
  STR_COUNT
};

const char *T(StrId id);       // interface text
const char *medalName(int i);  // medal banner (MED_COUNT)
const char *medalDesc(int i);
const char *trailName(int rank);  // 0 .. Pet::TRAIL_RANKS  // long medal description
