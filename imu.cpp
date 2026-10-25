#include "imu.h"
#include "pin_config.h"
#include "stepdetect.h"
#include <Wire.h>
// the new driver: the legacy <SensorQMI8658.hpp> declares a different class with the same
// name, and mixing the two corrupts the vtable (boot loop in begin())
#include <sensor/imu/qmi8658/SensorQMI8658.hpp>
#include <sensor/AccelerometerUtils.hpp>

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
  qmi.configAccel(SensorQMI8658::AccelRange::FS_2G, SensorQMI8658::AccelODR::ODR_56Hz,
                  SensorQMI8658::LpfMode::MODE_0);
  qmi.enableAccel();
  detector.start(millis());
  Serial.printf("QMI8658 ok id=0x%X\n", qmi.getChipID());
  return true;
}

bool imuOk() { return ok; }

void imuPoll(uint32_t nowMs) {
  if (!ok || (lastPoll && nowMs - lastPoll < IMU_POLL_MS)) return;
  lastPoll = nowMs ? nowMs : 1;
  AccelerometerData a;
  if (!qmi.readAccel(a)) return;
  // the detector works in g
  float mag = AccelerometerUtils::mps2ToG(sqrtf(a.mps2.x * a.mps2.x + a.mps2.y * a.mps2.y + a.mps2.z * a.mps2.z));
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
