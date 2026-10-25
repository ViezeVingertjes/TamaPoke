#include "imu.h"
#include "pin_config.h"
#include "stepdetect.h"
#include <Wire.h>
// not the legacy <SensorQMI8658.hpp>: its class has the same name as the compiled driver,
// and linking both makes begin() call through a bad vtable (boot loop)
#include <ImuDrv.hpp>

static SensorQMI8658 qmi;
static StepDetector detector;
static bool ok = false;
static uint32_t lastPoll = 0;
static uint16_t stepBank = 0;
static uint32_t motionUntil = 0;  // the sensor is only sampled fast while something moves

bool imuBegin() {
  // 0x6B on this board; 0x6A is the alternative address (SA0)
  ok = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL) ||
       qmi.begin(Wire, QMI8658_H_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (!ok) {
    Serial.println("QMI8658 not detected");
    return false;
  }
  // 56 Hz is register 0x07 (the old driver's "62.5 Hz"); ODR_56Hz maps to 56.05 and rounds up to 112
  ok = qmi.configAccel(SensorQMI8658::AccelRange::FS_2G, 56.0f, SensorQMI8658::LpfMode::MODE_0) &&
       qmi.enableAccel();
  if (!ok) {
    Serial.println("QMI8658 config failed");
    return false;
  }
  detector.start(millis());
  Serial.printf("QMI8658 ok id=0x%X\n", qmi.getChipID());
  return true;
}

bool imuOk() { return ok; }

void imuPoll(uint32_t nowMs) {
  if (!ok || (lastPoll && nowMs - lastPoll < IMU_POLL_MS)) return;
  lastPoll = nowMs ? nowMs : 1;
  int16_t x, y, z;  // raw, so readAccel() does not add a temperature read every poll
  if (!qmi.readAccelRaw(x, y, z)) return;
  float mag = sqrtf((float)x * x + (float)y * y + (float)z * z) / 16384.0f;  // ±2g: 16384 LSB per g
  if (fabsf(mag - detector.gravity) > 0.06f) motionUntil = nowMs + 3000;
  uint32_t next = stepBank + detector.feed(nowMs, mag);
  stepBank = next > 60000UL ? 60000 : (uint16_t)next;
}

bool imuMotionRecent(uint32_t nowMs) { return (int32_t)(motionUntil - nowMs) > 0; }

uint16_t imuTakeSteps() {
  uint16_t n = stepBank;
  stepBank = 0;
  return n;
}
