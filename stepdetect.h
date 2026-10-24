#pragma once
#include <stdint.h>
#include <math.h>

// Step detector on the acceleration magnitude (in g), sampled every ~40 ms. Pure code (no
// hardware) so it is tested on the PC with synthetic walking (test/test_steps.cpp).
//
// The baseline follows the sensor quickly at rest and slowly while moving, so a step's
// impulse is not filtered out. A step is the positive impact peak; the swing back is part of
// the same step and must die down before the next one counts. Walking is only confirmed after
// two peaks 280..1400 ms apart (then both are released together), so a single bump counts 0.
// Thresholds come from ShadowEnemyx's fork, tuned on the same QMI8658.
struct StepDetector {
  float gravity = 0;
  bool peak = false;
  uint32_t lastPeak = 0;
  uint32_t ignoreUntil = 0;
  uint8_t cadence = 0;

  void start(uint32_t nowMs) { *this = StepDetector(); ignoreUntil = nowMs + 400; }  // sensor settling

  // returns the steps confirmed by this sample
  uint8_t feed(uint32_t nowMs, float mag) {
    if (gravity <= 0.01f) { gravity = mag; return 0; }
    float signal = mag - gravity;
    float alpha = fabsf(signal) < 0.08f ? 0.12f : 0.025f;
    gravity += (mag - gravity) * alpha;
    if ((int32_t)(ignoreUntil - nowMs) > 0) return 0;
    if (peak) {
      if (signal <= 0.025f) peak = false;
      return 0;
    }
    if (signal < 0.085f) return 0;
    peak = true;

    uint32_t gap = lastPeak ? nowMs - lastPeak : 0;
    if (lastPeak && gap < 280UL) return 0;
    if (!lastPeak || gap > 1400UL) {  // first peak of a walk
      lastPeak = nowMs;
      cadence = 1;
      return 0;
    }
    lastPeak = nowMs;
    if (cadence == 1) { cadence = 2; return 2; }
    if (cadence < 255) cadence++;
    return 1;
  }
};
