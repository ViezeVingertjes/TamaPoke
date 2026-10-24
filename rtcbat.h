#pragma once
#include <Arduino.h>

// PCF85063 RTC: persistent time as long as the board has power
bool rtcBegin();
uint32_t rtcEpoch();             // unix seconds; 0 if the RTC is not valid
void rtcSetEpoch(uint32_t e);

// AXP2101 PMU: battery state
bool batBegin();
void pmuEnablePanel();           // turns on BLDO1 (OLED VDD 3.3V); call before gfx->begin()
void batSetPollMs(uint32_t ms);   // how often the PMU is read over I2C (lengthened while the screen is off)
int batPercent();                // 0-100, -1 if no battery is connected
bool batCharging();
int batMillivolts();               // battery voltage in mV, 0 if there is no reading
bool batShouldShutdown();          // discharged to the safe limit (batcurve.h): save and power off
void batPowerOff();                // physical PMU power-off
bool usbPresent();
bool usbPresentNow();             // uncached (one I2C read): to decide whether it is safe to sleep

// AXP2101 PWR button: long press 4s = physical power-off (RTC stays alive);
// the firmware captures the short press (screen on/off)
void pwrSetup();
bool pwrShortPressed();  // poll in the loop
