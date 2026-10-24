#include "imu.h"
#include "pin_config.h"
#include "stepdetect.h"
#include <Wire.h>
#include <SensorQMI8658.hpp>

static SensorQMI8658 qmi;
static StepDetector detector;
static bool ok = false;
static uint32_t lastPoll = 0;
static uint16_t stepBank = 0;

bool imuBegin() {
  // 0x6B on this board; 0x6A is the alternative address (SA0)
  ok = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL) ||
       qmi.begin(Wire, QMI8658_H_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (!ok) {
    Serial.println("QMI8658 not detected");
    return false;
  }
  qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_2G, SensorQMI8658::ACC_ODR_62_5Hz,
                          SensorQMI8658::LPF_MODE_0);
  qmi.enableAccelerometer();
  detector.start(millis());
  Serial.printf("QMI8658 ok id=0x%X\n", qmi.getChipID());
  return true;
}

bool imuOk() { return ok; }

void imuPoll(uint32_t nowMs) {
  if (!ok || (lastPoll && nowMs - lastPoll < IMU_POLL_MS)) return;
  lastPoll = nowMs ? nowMs : 1;
  float x, y, z;
  if (!qmi.getAccelerometer(x, y, z)) return;
  uint32_t next = stepBank + detector.feed(nowMs, sqrtf(x * x + y * y + z * z));
  stepBank = next > 60000UL ? 60000 : (uint16_t)next;
}

uint16_t imuTakeSteps() {
  uint16_t n = stepBank;
  stepBank = 0;
  return n;
}
