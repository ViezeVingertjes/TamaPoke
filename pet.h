#pragma once
#include <Arduino.h>
#include <Preferences.h>

// 1 tick = 1 game minute. Lower this value to test faster
// (e.g. 5000UL = stats drop 12x faster).
#define PET_TICK_MS 60000UL
// Game minutes per level. With 60, CHARMANDER evolves at ~16 h
// of play with perfect care. Lower to 1 to see evolutions instantly.
#define MINUTES_PER_LEVEL 60
#define EAT_ANIM_MS 2500UL
#define HEART_MS 1500UL
#define EVOLVE_ANIM_MS 5200UL              // evolution animation (longer = more epic)
#define CEREMONY_MS 10000UL                // duration of the on-screen farewell
#define FAREWELL_AGE_MIN (3UL * 24 * 60)   // says farewell after 3 game days (in final form)
#define RUNAWAY_TICKS 60                   // runs away after 1 h with EVERYTHING at zero

// milliseconds left until `deadline` (0 if it has passed, or if deadline==0:
// "timer inactive"). All game timers (dialogs,
// animations) are stored as an absolute instant "deadline = millis() +
// duration" and used to be compared with millis() < deadline / millis() > deadline;
// that breaks when millis() wraps around at ~49.7 days of uptime. The
// unsigned subtraction, reinterpreted as signed, is safe against that
// wrap as long as the real interval is well under ~24.8 days (here the
// longest timer is ~12 s), without changing how the deadline is stored.
static inline uint32_t timeLeft(uint32_t deadline) {
  if (!deadline) return 0;
  int32_t left = (int32_t)(deadline - millis());
  return left > 0 ? (uint32_t)left : 0;
}

// end-of-cycle ceremonies
enum : uint8_t { CER_NONE = 0, CER_FAREWELL, CER_RUNAWAY, CER_RELEASE };

enum PetMood : uint8_t { MOOD_HAPPY, MOOD_SAD, MOOD_EATING, MOOD_SLEEPING };

// medals of the individual (bitmask)
enum : uint16_t {
  MED_LV10 = 1 << 0, MED_LV25 = 1 << 1, MED_LV50 = 1 << 2,
  MED_BERRY = 1 << 3, MED_STREAK7 = 1 << 4, MED_BOND = 1 << 5,
  MED_FINAL = 1 << 6, MED_FIT = 1 << 7,
};
#define MED_COUNT 8

class Pet {
public:
  // Stats 0..100
  uint8_t fullness = 80;  // food
  uint8_t joy = 80;       // happiness
  uint8_t energy = 80;    // energy
  uint8_t hygiene = 100;  // cleanliness
  uint8_t poops = 0;      // poops on screen (max 3)
  uint8_t weight = 0;     // 0-100: treats fatten, the minigame burns
  // genes (90-110%, rolled on hatching) and training (0-100)
  uint8_t geneAtk = 100, geneDef = 100, geneSpe = 100;
  uint8_t trAtk = 0, trDef = 0, trSpe = 0;
  bool berryKnown = false;  // has already discovered its favorite berry
  bool shiny = false;       // rare color variant (rolled in the egg)
  uint32_t ageMinutes = 0;
  int16_t speciesId = -1;      // Pokedex number (1-151), -1 = egg
  int16_t prevSpeciesId = -1;  // for the evolution animation
  uint8_t careMistakes = 0;   // care mistakes: each delays evolution by 1 level
  bool sleeping = false;
  uint32_t lastSeenEpoch = 0;   // last RTC time seen (for offline progression)
  uint8_t ceremony = CER_NONE;  // farewell/runaway/release in progress
  uint8_t lastEnd = CER_NONE;   // how the previous one ended (affects the egg)
  uint8_t dexReg[19] = { 0 };       // pokedex of raised ones (151-bit bitmap)
  uint8_t dexShinyReg[19] = { 0 };  // raised in shiny version
  // daily care streak (the player's: persists across rearings)
  uint16_t streak = 0, bestStreak = 0;
  uint32_t lastCareDay = 0;
  // bond (the pet's: rises slowly with care, resets when another is born)
  uint8_t bond = 0;
  char nick[12] = "";    // nickname (empty = species name)
  // walking (the player's: persists across rearings). Counted by the IMU, see imu.cpp
  uint32_t stepsToday = 0, stepsTotal = 0;
  uint32_t stepDay = 0;         // day (epoch / 86400) that stepsToday belongs to
  uint8_t stepRewardMask = 0;   // bit i: daily goal i already rewarded today
  // medals: the individual's + running count across all rearings
  uint16_t medals = 0, totalMedals = 0;
  uint16_t newMedal = 0;   // just earned, to celebrate
  uint16_t lastMilestone = 0;  // streak milestone already celebrated
  uint16_t gameHi = 0;     // minigame record (the player's)
  uint16_t strHi = 0;      // punching bag hit record

  void begin();                 // loads state from NVS (or creates the first egg)
  void update(uint32_t nowMs);  // call on every loop()

  // Actions (touch buttons)
  void feed();              // red berry (compatibility)
  void feedBerry(uint8_t color);  // 0 red, 1 blue, 2 green
  void feedCandy();
  bool lovesBerry(uint8_t color) const {
    return !isEgg() && (speciesId % 3) == color;  // hidden taste per species
  }
  void playResult(uint8_t score);  // minigame reward (trains SPD)
  uint8_t trainStrength(uint16_t hits);  // training punching bag (trains STR)

  // combat stats: real gen 1 base x genes + level + training
  uint16_t atkStat() const;
  uint16_t defStat() const;
  uint16_t speStat() const;
  void play();
  void toggleLight();  // sleep / wake
  void clean();
  void caress();  // touch the pet
  void eggTap();  // touch the egg: 3 touches and it hatches
  void newEgg();   // start over with a random starter
  void release();  // release (long press + confirm)
  void syncClock(uint32_t nowEpoch);  // applies the time elapsed while off
  void setClock(uint32_t nowEpoch);   // sets the time without applying progression
  void startFarewell();  // also usable from the serial console (BYE)
  void startRunaway();   // also usable from the serial console (RUN)

  bool isEgg() const { return speciesId < 0; }
  uint8_t eggCracks() const { return eggTaps; }
  bool eating() const { return timeLeft(eatUntil) > 0; }
  bool showHeart() const { return timeLeft(heartUntil) > 0; }
  bool evolving() const { return timeLeft(evolveUntil) > 0; }
  float evolveT() const {     // progress of the evolution animation 0..1
    return 1.0f - (float)timeLeft(evolveUntil) / (float)EVOLVE_ANIM_MS;
  }
  bool canEvolveNow() const;  // evolution conditions met (ready)
  void evolve();              // triggers the transformation (called by a user touch)
  bool canFarewellNow() const;  // final form + 7 days: ready to say farewell (button)
  bool canRunawayNow() const;   // total neglect 1h: ready to run away (sad button)
  // the user decides in a dialog; "keep/stay together" postpones and re-offers later
  bool wantEvolveButton() const { return canEvolveNow() && level() > evoDeclinedLv; }
  bool wantFarewellButton() const { return canFarewellNow() && ageMinutes >= farDeclinedAge; }
  void declineEvolve() { evoDeclinedLv = level(); }              // re-offers on level up
  void declineFarewell() { farDeclinedAge = ageMinutes + 1440; } // re-offers in 1 day
  // first game: the player picks a starter (Bulbasaur/Charmander/Squirtle)
  bool awaitingStarter() const { return starterPick; }
  void chooseStarter(int16_t dex) { eggTarget = dex; starterPick = false; save(); }
  void factoryReset() { prefs.clear(); }  // wipes NVS (test: serial command WIPE)
  void dbgRunawayReady() { fullness = joy = energy = hygiene = 0; neglectTicks = RUNAWAY_TICKS; }  // test
  // uint16_t, not uint8_t: with MINUTES_PER_LEVEL=60 the level is the hours, so
  // a uint8_t wraps to 0 at level 256, after ~10.6 days. And that is
  // reached in normal play, because "stay together" lets the farewell be
  // postponed indefinitely. It is capped at 999 instead of overflowing.
  uint16_t level() const {
    uint32_t lv = 1 + ageMinutes / MINUTES_PER_LEVEL;
    return lv > 999 ? 999 : (uint16_t)lv;
  }
  bool isRegistered(int16_t dex) const {
    return dex >= 1 && dex <= 151 && (dexReg[(dex - 1) >> 3] & (1 << ((dex - 1) & 7)));
  }
  bool isShinyRegistered(int16_t dex) const {
    return dex >= 1 && dex <= 151 && (dexShinyReg[(dex - 1) >> 3] & (1 << ((dex - 1) & 7)));
  }
  uint16_t registeredCount() const;
  bool lineHasUnregistered(int16_t base) const;
  uint8_t eggRarity() const;       // rarity of the current egg (without revealing the species)
  int16_t pickEggSpecies();        // public so rolls can be simulated (EGGS)
  uint8_t lowestStat() const { return min(min(fullness, joy), min(energy, hygiene)); }
  PetMood mood() const;
  // progress of the farewell/runaway ceremony, 0..1 (to animate it)
  float ceremonyT() const {
    if (ceremony == CER_NONE) return 0.0f;
    return 1.0f - (float)timeLeft(ceremonyUntil) / (float)CEREMONY_MS;
  }

  // streak / bond / medals / name
  void rename(const char *name);
  // walking: daily goals give a small reward, lifetime steps give a trail rank
  static constexpr uint8_t STEP_GOALS = 3, TRAIL_RANKS = 4;
  static uint32_t stepGoal(uint8_t i);    // steps per day for goal i
  static uint32_t trailGoal(uint8_t i);   // lifetime steps for rank i + 1
  uint8_t trailRank() const;              // 0 = none yet .. TRAIL_RANKS
  void applyWalk(uint16_t steps);
  // goal just reached (-1 = none), to announce it for a few seconds
  int8_t stepRewardShown() const { return timeLeft(stepRewardUntil) > 0 ? stepRewardIdx : -1; }
  bool hasMedal(uint16_t m) const { return medals & m; }
  bool showMedal() const { return timeLeft(medalUntil) > 0; }
  bool showMilestone() const { return timeLeft(milestoneUntil) > 0; }
  int careBonus() const;  // egg improvement from streak + bond

  // deferred periodic save: tick() marks it pending and the loop flushes it
  // when the screen is dimmed/off (the flash write freezes
  // both cores for ~1s: this way it is not visible and does not cut off touch)
  bool savePending() const { return pendingSave; }
  // last real time persisted; used to reseed an RTC that lost the time
  uint32_t savedEpoch() { return prefs.getUInt("seen", 0); }
  void flushSave();
  // Writes now, whatever pendingSave says: flushSave() is `if (pendingSave) save()`, so a serial
  // console command that changed a field the game had not marked dirty wrote nothing at all.
  void saveNow() { save(); }

  // Versioned snapshot of the game state for the browser installer (SAVEGET / SAVEPUT over
  // USB). Never includes SD data. Time away is not applied after a restore.
  static constexpr size_t BACKUP_MAX_BYTES = 128;
  size_t exportBackup(uint8_t *out, size_t capacity);
  bool importBackup(const uint8_t *data, size_t length);

private:
  template <class IO> void backupFields(IO &io);
  Preferences prefs;
  uint32_t lastTick = 0;
  uint32_t eatUntil = 0;
  uint32_t heartUntil = 0;
  uint32_t evolveUntil = 0;
  int16_t eggTarget = 1;       // hidden dex number that will come out of the egg
  bool eggShiny = false;       // surprise rolled when the egg is created
  uint8_t eggTaps = 0;
  uint8_t mistakeCooldown = 0;
  uint8_t ticksSinceSave = 0;
  bool pendingSave = false;     // periodic save waiting to be flushed
  uint16_t evoDeclinedLv = 0;   // "keep form": do not offer evolution until level up
  uint32_t farDeclinedAge = 0;  // "stay together": do not offer farewell until this age
  bool starterPick = false;     // first game: waiting for the player to pick a starter
  uint8_t neglectTicks = 0;
  uint16_t goodTicks = 0;  // well-cared streak: forges DEF
  uint32_t ceremonyUntil = 0;
  uint8_t bondToday = 0;       // daily cap on bond gain
  uint32_t medalUntil = 0;     // medal celebration on screen
  uint32_t milestoneUntil = 0; // streak milestone celebration
  uint32_t stepRewardUntil = 0;
  int8_t stepRewardIdx = -1;
  void ensureStepDay();

  uint32_t today() const { return lastSeenEpoch ? lastSeenEpoch / 86400 : 0; }
  void registerCare();   // first care of the day: streak + bond
  void addBond(uint8_t amt);
  void checkMedals();
  void tick();
  void hatch();
  void registerSpecies(int16_t dex);
  void save();
  void load();
  static uint8_t clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
};
