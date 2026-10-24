#pragma once
#include <stdint.h>

// QMI8658 6-axis IMU on the shared I2C bus (touch/RTC/PMU). It has no interrupt pin on
// this board, so it is polled. Only the accelerometer is used, for the step counter.
#define IMU_POLL_MS 40  // 25 Hz: enough to see a step's impact

bool imuBegin();
bool imuOk();
void imuPoll(uint32_t nowMs);  // reads the sensor if IMU_POLL_MS passed since the last read
uint16_t imuTakeSteps();       // steps confirmed since the last call
