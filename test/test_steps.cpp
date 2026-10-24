// Step detector (stepdetect.h) on synthetic acceleration.
#include "framework.h"
#include "../stepdetect.h"

// walks `steps` steps, one impact every `periodMs`, sampled every 40 ms; returns confirmed steps
static int walk(StepDetector &d, uint32_t &t, int steps, uint32_t periodMs, float amp) {
  int got = 0;
  uint32_t end = t + (uint32_t)steps * periodMs;
  for (uint32_t t0 = t; t < end; t += 40) {
    uint32_t phase = (t - t0) % periodMs;
    float mag = 1.0f + (phase < 120 ? amp * sinf(3.14159f * phase / 120.0f) : 0.0f);
    got += d.feed(t, mag);
  }
  return got;
}

TEST(steps, caminar_cuenta_los_pasos) {
  StepDetector d;
  uint32_t t = 0;
  d.start(t);
  for (; t < 1000; t += 40) d.feed(t, 1.0f);   // at rest, settles the baseline
  int got = walk(d, t, 40, 550, 0.30f);
  CHECK(got >= 37 && got <= 40);               // the first peak only starts the walk
}

TEST(steps, un_golpe_suelto_no_cuenta) {
  StepDetector d;
  uint32_t t = 0;
  d.start(t);
  for (; t < 1000; t += 40) d.feed(t, 1.0f);
  CHECK_EQ(walk(d, t, 1, 550, 0.50f), 0);
  for (int i = 0; i < 100; i++, t += 40) CHECK_EQ((int)d.feed(t, 1.0f), 0);   // and nothing after
  CHECK_EQ(walk(d, t, 1, 550, 0.50f), 0);      // 4 s later: another single bump
}

TEST(steps, ignora_el_arranque_del_sensor) {
  StepDetector d;
  d.start(0);
  int got = 0;
  for (uint32_t t = 0; t < 400; t += 40) got += d.feed(t, t % 80 ? 1.6f : 1.0f);
  CHECK_EQ(got, 0);
}
