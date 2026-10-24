#pragma once
#include <stdint.h>

// Game sound effects (queued, non-blocking). The order matches the
// SFX table in audio.cpp.
enum Sfx : uint8_t {
  SFX_TAP = 0,  // tap / button
  SFX_EAT,      // eat
  SFX_PLAY,     // minigame point / hit
  SFX_HEART,    // likes it / petting
  SFX_HATCH,    // hatching
  SFX_EVOLVE,   // evolution
  SFX_MEDAL,    // medal / milestone
  SFX_DENY,     // action not allowed
  SFX_BYE,      // farewell
  SFX_LEVEL,    // level up
  SFX_COUNT
};

void audioBegin();          // init ES8311 + I2S + amplifier + audio task
void sfxPlay(uint8_t id);   // queues an effect (does not block the loop)
void audioSetEnabled(bool on);
bool audioEnabled();
void audioSetSleeping(bool sleeping);  // asleep: amplifier off
bool audioBusy();                      // an effect requested while idle may still be playing: do not sleep the CPU
void audioSetIdle(bool idle);          // no recent use: amplifier off (an effect turns it on for 4 s)
