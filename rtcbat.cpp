#include "batcurve.h"
#include "rtcbat.h"
#include "pin_config.h"  // define XPOWERS_CHIP_AXP2101
#include <Wire.h>
#include <time.h>
#include <SensorPCF85063.hpp>
#include <XPowersLib.h>

static SensorPCF85063 rtc;
static XPowersPMU pmu;
static bool rtcOk = false;
static bool pmuOk = false;

bool rtcBegin() {
  rtcOk = rtc.begin(Wire, IIC_SDA, IIC_SCL);
  if (!rtcOk) Serial.println("PCF85063 no detectado");
  return rtcOk;
}

uint32_t rtcEpoch() {
  if (!rtcOk) return 0;
  RTC_DateTime t = rtc.getDateTime();
  if (t.getYear() < 2025 || t.getYear() > 2120) return 0;  // no valid time
  struct tm tmv = {};
  tmv.tm_year = t.getYear() - 1900;
  tmv.tm_mon = t.getMonth() - 1;
  tmv.tm_mday = t.getDay();
  tmv.tm_hour = t.getHour();
  tmv.tm_min = t.getMinute();
  tmv.tm_sec = t.getSecond();
  time_t e = mktime(&tmv);  // default TZ = UTC, consistent with gmtime_r
  return e > 0 ? (uint32_t)e : 0;
}

void rtcSetEpoch(uint32_t e) {
  if (!rtcOk) return;
  time_t tt = e;
  struct tm tmv;
  gmtime_r(&tt, &tmv);
  rtc.setDateTime(RTC_DateTime(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                               tmv.tm_hour, tmv.tm_min, tmv.tm_sec));
}

// Explicit charger setup instead of the PMU's power-on defaults, which some boards
// charge badly with (found by ShadowEnemyx's fork). 500 mA is about 0.45C on the
// 1100 mAh LiPo; the 4.2 V target and 100 mA termination match a standard cell.
static void configureBatteryCharger() {
  pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_900MA);
  pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
  pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_100MA);
  pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_100MA);
  pmu.enableChargerTerminationLimit();
  pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
  pmu.enableCellbatteryCharge();
  Serial.printf("AXP2101 charger: vbusLim=%u chgCur=%u target=%u\n",
                pmu.getVbusCurrentLimit(), pmu.getChargerConstantCurr(),
                pmu.getChargeTargetVoltage());
}

bool batBegin() {
  pmuOk = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (!pmuOk) { Serial.println("AXP2101 no detectado"); return false; }
  configureBatteryCharger();
  pmu.enableBattVoltageMeasure();
  pmu.setSysPowerDownVoltage(BAT_HW_CUTOFF_MV);  // backup: the PMU cuts power by itself if the firmware does not make it in time
  return true;
}

// Turns on power to the AMOLED. On the Waveshare 1.75 the panel (OLED VDD)
// hangs off the AXP2101's 3.3V BLDO1 rail. The firmware assumed it was
// on; if the PMU resets (total drain), BLDO1 stays OFF and the screen
// is black even though everything else works. Must be called BEFORE gfx->begin().
void pmuEnablePanel() {
  if (!pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    Serial.println("AXP2101 no detectado (pmuEnablePanel)");
    return;
  }
  pmu.setBLDO1Voltage(3300);   // OLED VDD
  pmu.enableBLDO1();
}

// the power state (I2C) is cached for ~2 s: reading it every loop frame
// added useless I2C traffic and could oscillate (brightness flicker)
static uint32_t powerCacheT = 0;
static int cachedPct = -1;
static bool cachedCharging = false, cachedUsb = true;

static BatMonitor mon;
static bool monDischarging = false;
static uint32_t pollMs = 2000;
void batSetPollMs(uint32_t ms) { pollMs = ms; }

static void refreshPower() {
  uint32_t now = millis();
  if (powerCacheT && now - powerCacheT < pollMs) return;
  powerCacheT = now ? now : 1;
  if (!pmuOk) { cachedPct = -1; cachedCharging = false; cachedUsb = true; return; }
  bool conn = pmu.isBatteryConnect();
  cachedCharging = pmu.isCharging();
  cachedUsb = pmu.isVbusIn();
  // While discharging, the percentage comes from a LiPo cell curve (batcurve.h); on USB or charging
  // the voltage rises from the charge and is useless, so the PMU's value is used.
  bool discharging = conn && !cachedUsb && !cachedCharging;
  if (discharging != monDischarging) {
    mon.reset();
    mon.seed(cachedPct);  // on unplugging the percentage does not go up (the voltage is still inflated by the charge)
    monDischarging = discharging;
  }
  if (discharging) cachedPct = mon.sample((int)pmu.getBattVoltage(), now);
  else cachedPct = conn ? pmu.getBatteryPercent() : -1;
  if (cachedPct < 0 && conn) cachedPct = pmu.getBatteryPercent();  // no voltage reading
}

int batPercent() { refreshPower(); return cachedPct; }
bool batCharging() { refreshPower(); return cachedCharging; }
bool batShouldShutdown() { refreshPower(); return monDischarging && mon.shutdownNeeded(millis()); }
void batPowerOff() {
  if (pmuOk) pmu.shutdown();
  mon.reset();  // if still alive (e.g. USB was just plugged in) everything has to hold again for the whole time
}
int batMillivolts() { return pmuOk ? (int)pmu.getBattVoltage() : 0; }
bool usbPresent() { refreshPower(); return cachedUsb; }
bool usbPresentNow() { return pmuOk ? pmu.isVbusIn() : true; }

void pwrSetup() {
  if (!pmuOk) return;
  pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
  pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
  pmu.clearIrqStatus();
}

bool pwrShortPressed() {
  if (!pmuOk) return false;
  pmu.getIrqStatus();
  bool hit = pmu.isPekeyShortPressIrq();
  if (hit) pmu.clearIrqStatus();
  return hit;
}
