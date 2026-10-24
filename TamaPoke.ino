// TamaPoke - pixel art tamagotchi inspired by gen 1
// for the Waveshare ESP32-S3-Touch-AMOLED-1.75
//
// Libraries (Library Manager or the Waveshare repo):
//   - "GFX Library for Arduino" (moononournation), with CO5300 QSPI support
//   - "SensorLib" (Lewis He), CST9217 touch driver
//
// Board: ESP32S3 Dev Module | Flash 16MB | PSRAM: OPI PSRAM | USB CDC On Boot: Enabled
//
// The sprites and the species table are generated with tools/sprites.py (emit).

#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "pin_config.h"
#include "species.h"
#include "dex.h"
#include "pet.h"
#include "sdmon.h"
#include "rtcbat.h"
#include "imu.h"
#include "clockset.h"
#include "i18n.h"
#include "audio.h"
#include "power.h"
#include "nearby_radio.h"
#include "blit.h"
#include "layout.h"

// Firmware version. Bump this number on every release (and manifest.json for
// the web installer). Shown on the settings screen and over serial at boot.
#define FW_VERSION "1.18"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RESET, 0 /*rotation*/, LCD_WIDTH, LCD_HEIGHT, LCD_OFF_X, LCD_OFF_Y, 0, 0);
// Full framebuffer in PSRAM: we draw everything and then flush() (no flicker)
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);

TouchDrvCST92xx touch;
#define TOUCH_ADDR 0x5A  // CST9217
Pet pet;

// animated sprite from the SD for the current species (if the file exists)
SdMon mon;          // B/W sprite (fallback, and for the minigame when there is no PMD)
PmdMon pmd;         // multi-action PMD sprite (main screen)
// neighbors (nearby TamaPokes): one sprite per slot, loaded from the SD one at a time
struct NbSlot { uint32_t id; uint8_t species; bool shiny; };
PmdMon nbPmd[NB_MAX_SHOWN];
NbSlot nbSlot[NB_MAX_SHOWN];
PmdMon evoPmd;      // previous form, only during the evolution blink
int16_t monFor = -2;
bool monShinyFor = false;

// on-screen behavior of the pet
struct {
  uint8_t mode = 0;     // 0 idle, 1 walk, 2 one-shot gesture
  uint8_t act = PMD_IDLE;
  uint32_t t0 = 0;      // start of the current animation
  uint32_t until = 0;   // end of the current state
  float x = CX, targetX = CX;
} beh;
uint32_t walkLast = 0;  // time of the last walk step (0 = none yet)
#define PET_GROUND UY(304)  // pet ground line
PmdMon galleryPmd;  // big sprite of the gallery detail view (PMD/TPK2, legal)

// pokedex gallery
bool galleryOpen = false;
bool galleryDirty = false;
int galleryPage = 0;        // 10 pages of 16
int16_t galleryDetail = 0;  // dex in detail view, 0 = grid

bool screenOff = false;       // short press of the PWR button
bool cardOpen = false;        // pet card (vertical swipe)
bool kbOpen = false;          // keyboard to rename the pet
char nameBuf[12] = "";
uint8_t nameLen = 0;
#define CARD_PAGES 5
uint8_t cardPage = 0;         // 0 profile, 1 battle, 2 medals, 3 progress, 4 steps
bool clockOpen = false;       // clock-setting screen (swipe down)
int clockH = 12, clockM = 0;  // hour being edited

// bath scene: foam over the pet and cleanup when it pops
uint32_t bathUntil = 0;
bool bathPending = false;
struct { int16_t x, y; uint8_t r, ph; } bubbles[14];
uint32_t feedMenuUntil = 0;   // food picker open until this millis

// "touches" minigame: keep the pokeball in the air
bool gameOpen = false;
uint32_t gameOverUntil = 0;
float ballX, ballY, ballVX, ballVY, gamePetX;
uint32_t lastGameStep = 0;  // last stepGame() call: physics runs on real time, not per frame
uint16_t gameScore;  // not 8 bits: a long run passes 255
uint8_t gameMisses;
float hitX, hitY;             // last hit (impact ring)
uint32_t hitTime = 0;
bool gameNewHi = false;

// training sack (trains strength)
bool sackOpen = false;
uint32_t sackUntil = 0, sackOverUntil = 0;
uint16_t sackHits = 0;
float sackShake = 0;
uint8_t sackGain = 0;
bool sackNewHi = false;

// the 9 species with their own sprite in flash (fallback without SD): dex -> index
int flashIdxForDex(int16_t dex) {
  static const int8_t IDX[10] = { -1, 3, 4, 5, 0, 1, 2, 6, 7, 8 };
  return (dex >= 1 && dex <= 9) ? IDX[dex] : -1;
}

#define PET_CY UY(202)  // vertical center of the sprite

static const uint16_t INK_K = 0x18C4;  // spriteColor('k')

// icon buttons following the bottom arc of the round screen
// (the outer ones sit higher so they stay inside the circle)
struct Btn {
  int16_t cx, cy;
  const char *const *icon;
};
// four 80 px circles on a single arc, equally spaced: each one is a
// need (its level fills from bottom to top) and also its action
Btn buttons[4] = {
  { U(105), UY(348), SPR_ICON_FOOD },   // eat
  { U(186), UY(398), SPR_ICON_PLAY },   // play
  { U(280), UY(398), SPR_ICON_LIGHT },  // light
  { U(361), UY(348), SPR_ICON_CLEAN },  // bath
};
#define BTN_R U(40)     // circle radius (80 px diameter)
#define BTN_HIT U(44)   // touch radius (a bit more generous)

// egg cracks ('k' pixels over the sprite)
static const uint8_t CRACK1[][2] = { {15,8},{16,9},{15,10} };
static const uint8_t CRACK2[][2] = { {11,13},{12,14},{11,15},{20,12},{19,13},{20,14} };
// night mode stars
static const uint16_t STARS[][2] = { {120,140},{330,120},{370,210},{95,230},{280,90},{160,95} };

bool wasPressed = false;
// starter choice (first game): Bulbasaur / Charmander / Squirtle, 3 rows
static const int16_t STARTER_DEX[3] = { 1, 4, 7 };
#define STARTER_ROW_X U(70)
#define STARTER_ROW_W U(326)
#define STARTER_ROW_Y UY(110)
#define STARTER_ROW_H UY(70)
#define STARTER_ROW_GAP UY(8)
// evolution CTA button (centered, middle of the screen)
#define EVO_BTN_W U(176)
#define EVO_BTN_H UY(72)
#define EVO_BTN_X (CX - EVO_BTN_W / 2)
#define EVO_BTN_Y UY(136)
// farewell CTA button (wider: holds the name + phrase)
#define FAR_BTN_W U(408)
#define FAR_BTN_H UY(72)
#define FAR_BTN_X (CX - FAR_BTN_W / 2)
#define FAR_BTN_Y UY(150)
// Boxes of the game dialogs: drawing and touch use the same ones, so they
// cannot drift apart.
// decision dialog (evolve/keep, say goodbye/stay together)
#define CHOICE_X U(113)
#define CHOICE_W U(240)
#define CHOICE_BH UY(72)
#define CHOICE_B1_Y UY(232)
#define CHOICE_B2_Y UY(316)
// "release?" dialog: keep on top (accent), release below; YES_* is RELEASE
#define YES_X CHOICE_X
#define YES_Y CHOICE_B2_Y
#define YES_W CHOICE_W
#define YES_H CHOICE_BH
#define NO_X CHOICE_X
#define NO_Y CHOICE_B1_Y
// food picker: the 4 panel circles swap icons (FEED_ICON_* = center of icon i)
#define FEED_X (buttons[0].cx - BTN_R)
#define FEED_Y (buttons[0].cy - BTN_R)
#define FEED_W (buttons[3].cx + BTN_R - FEED_X)
#define FEED_H (buttons[1].cy + BTN_R - FEED_Y)
#define FEED_ICON_X(i) (buttons[i].cx - 8 * SC(3))
#define FEED_ICON_Y(i) (buttons[i].cy - 8 * SC(3))
// card medal pills: two columns
// card TRAIN STRENGTH button
#define TRAIN_X U(113)
#define TRAIN_Y UY(268)
#define TRAIN_W U(240)
#define TRAIN_H UY(68)
// clock-setting screen: +/- buttons, sound switch and OK
#define CLOCK_BTN_W U(72)
#define CLOCK_BTN_H UY(72)
#define CLOCK_BTN_Y UY(160)
// hour (- +) and minutes (- +) groups, 152 wide with a 24 gap, centered on the screen
#define CLK_X0 (CX - U(164))
#define CLK_X1 (CX - U(84))
#define CLK_X2 (CX + U(12))
#define CLK_X3 (CX + U(92))
#define SND_PILL_X (CX - SND_PILL_W / 2)
#define SND_PILL_W U(240)
#define CLOCK_OK_X (CX + U(4))
#define CLOCK_OK_Y UY(344)
#define CLOCK_OK_W U(140)
#define CLOCK_OK_H UY(68)
#define CLOCK_CANCEL_X (CX - U(4) - CLOCK_OK_W)
// top touch zones: the header exits/abandons; the card name opens the keyboard
#define HEADER_ZONE_H UY(72)
#define CARD_NAME_ZONE_H UY(84)
// the CST9217 signals touch data on the INT pin; we use it to avoid
// reading the I2C bus while the chip is asleep (that read hung for ~1s)
volatile bool gTouchIrq = false;
void IRAM_ATTR touchIsr() { gTouchIrq = true; }
uint32_t lastRender = 0;
uint32_t sdEpoch = 0;  // +1 each time the SD is reloaded (thumbnails change)
// AMOLED protection: dimmed on inactivity
uint32_t lastInteract = 0;
uint8_t dimStage = 0;        // 0 awake, 1 dimmed (90s), 2 almost off (5min)
bool swallowGesture = false; // the touch that wakes the screen triggers nothing
uint32_t holdStart = 0;     // long press on the pet
uint32_t confirmUntil = 0;  // "release?" dialog active until this millis
uint8_t choiceKind = 0;     // decision dialog: 0 none, 1 evolution, 2 farewell
uint32_t choiceUntil = 0;   // closes by itself at this millis
int16_t tX0, tY0, tXl, tYl; // gesture in progress (start and last position)
uint32_t tStart = 0;
bool holdFired = false;

void setup() {
  Serial.setRxBufferSize(8192);  // SD transfer arrives in 2 KB blocks
  Serial.begin(115200);
  // CRITICAL: without this, Serial.print BLOCKS the game when no
  // serial monitor is open on the host (the USB CDC TX buffer fills
  // and nobody drains it) -> with timeout 0 messages are dropped
  Serial.setTxTimeoutMs(0);
  Serial.printf("TamaPoke fw v%s\n", FW_VERSION);
  Wire.begin(IIC_SDA, IIC_SCL);
  // CST9217 (touch), AXP2101 (PMU) and PCF85063 (RTC) share this I2C bus.
  // Safety net for PMU/RTC (SensorLib does NOT honor this timeout on the
  // touch; the sleeping-touch hang is solved by gating on INT, see
  // handleTouch).
  Wire.setTimeOut(50);

  // CRITICAL: power up the panel supply (BLDO1=OLED VDD 3.3V) BEFORE
  // initializing the display. If the PMU was reset (fully drained), this rail
  // stays OFF and the screen looks black even though the rest of the board works.
#ifndef BOARD_AMOLED_216  // Waveshare 2.16 examples start the panel without touching the rails
  pmuEnablePanel();
#endif

  // QSPI at 80MHz (default 40): the framebuffer flush is the fps
  // bottleneck (~56ms at 40MHz). If the panel shows garbage, drop back to 40M.
  if (!gfx->begin(80000000)) Serial.println("gfx->begin() fallo");
#ifdef BOARD_AMOLED_216
  // Waveshare orientation for the 2.16 (MADCTL); paired with the touch
  // swap/mirror below: if one changes, change the other
  bus->writeC8D8(0x36, 0xA0);
#endif
  panel->setBrightness(180);

  touch.setPins(TP_RESET, TP_INT);
  bool touchOk = false;
  for (int i = 0; i < 3 && !touchOk; i++) {  // sometimes fails on the first try
    touchOk = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    if (!touchOk) delay(150);
  }
  if (!touchOk) Serial.println("tactil CST92xx no detectado");
  // begin() leaves the chip in command mode (reads the identity and does not exit);
  // a hardware reset is needed for it to report touches again
  touch.reset();
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
#ifdef BOARD_AMOLED_216
  touch.setSwapXY(true);  // combination from the official Waveshare 2.16 example
  touch.setMirrorXY(true, false);
#else
  touch.setMirrorXY(true, true);  // the panel is mounted rotated 180 degrees
#endif
  // INT is active-low: fires when data is ready. Gates the I2C reads (see loop)
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), touchIsr, FALLING);

  pet.begin();
  sdBegin();
  thumbs.load();

  // real clock: applies the time the device was off
  rtcBegin();
  batBegin();
  pwrSetup();
  imuBegin();  // step counter (optional: without the sensor there are no steps)
  uint32_t e = rtcEpoch();
  if (e == 0) {
    // Without a backup battery, the PCF85063 loses the time when power
    // is cut. Always seeding the fixed date would make game time GO BACK
    // relative to what is already saved, breaking the streak, the age and the
    // scene hour. It is seeded with the last seen time, if later, so
    // time never goes backwards. Minutes spent off are lost
    // anyway (without an RTC there is no way to know them), but nothing gets out of sync.
    uint32_t seed = 1767225600UL;             // truly blank RTC
    uint32_t seen = pet.savedEpoch();         // uses the NVS that pet already has open
    if (seen > seed) seed = seen;
    rtcSetEpoch(seed);
    e = rtcEpoch();
    Serial.printf("RTC sin hora: sembrado en %u%s\n", seed,
                  seen > 1767225600UL ? " (desde la ultima hora guardada)" : "");
  }
  pet.syncClock(e);

  nearbyBegin();  // id of this device; the radio only turns on in short windows (nearby.cpp)
  audioBegin();  // ES8311 + I2S + amplifier (plays a boot jingle)

  lastInteract = millis();
}

// load/unload the SD sprite when the species changes
void ensureMon() {
  if (pet.speciesId == monFor && monShinyFor == pet.shiny && !sdDirty) return;
  // freshly received files may include a new thumbs.bin, which used to be
  // loaded only in setup(): without this it did not show in the gallery until reboot
  if (sdDirty) { thumbs.unload(); thumbs.load(); sdEpoch++; }
  sdDirty = false;
  monFor = pet.speciesId;
  monShinyFor = pet.shiny;
  mon.unload();
  pmd.unload();
  beh.x = beh.targetX = CX;
  beh.mode = 0;
  beh.until = 0;
  if (pet.speciesId >= 1 && pet.speciesId <= DEX_COUNT) {
    pmd.load(pet.speciesId, pet.shiny);          // main: PMD
    if (!pmd.loaded) mon.load(pet.speciesId, pet.shiny);  // fallback: B/W
  }
}

void loop() {
  uint32_t now = millis();
  pet.update(now);

  // the amplifier follows the sleep state (the call returns early if unchanged).
  // Here it covers every path: the light button, night sleep and the one
  // arriving applied from offline progression.
  audioSetSleeping(pet.sleeping);

  // play a sound when the pet becomes ready to evolve
  // (includes the case of qualifying on wake-up). canEvolveNow is false while sleeping.
  static bool wasEvoReady = false;
  bool evoReady = pet.wantEvolveButton();
  if (evoReady && !wasEvoReady) sfxPlay(SFX_MEDAL);
  wasEvoReady = evoReady;
  // gloomy warning when the pet is about to run away from neglect
  static bool wasRunReady = false;
  bool runReady = pet.canRunawayNow();
  if (runReady && !wasRunReady) sfxPlay(SFX_DENY);
  wasRunReady = runReady;

  handleTouch();
  imuPoll(now);
  pet.applyWalk(imuTakeSteps());
  handleSerial();
  ensureMon();

  // short PWR press: screen on/off
  static uint32_t lastPwr = 0;
  if (now - lastPwr > 250) {
    lastPwr = now;
    if (pwrShortPressed()) {
      screenOff = !screenOff;
      if (screenOff) wasPressed = false;  // the next sample of a finger already down starts the wake hold
      else wakeUI();
    }
  }

  updateBrightness(now);

  // battery at the safe limit (no USB): save the game and power off the PMU before the
  // cell drops further; the RTC keeps the time
  if (batShouldShutdown() && !usbPresentNow()) {
    pet.flushSave();
    panel->setBrightness(0);
    batPowerOff();
  }

  // flush the periodic autosave ONLY with the screen dimmed/off or
  // sleeping: the NVS write freezes both cores for ~1s (flash cache off),
  // and here there is no animation to cut off and no finger waiting for a response. After 90s
  // of inactivity the screen already dims, so it flushes right away; active
  // use persists anyway through the saves of each action (eat/play/...).
  if (pet.savePending() && (screenOff || dimStage >= 1 || pet.sleeping)) {
    pet.flushSave();
  }

  // record the real time every 30 s (persisted on every game save)
  static uint32_t lastClock = 0;
  if (now - lastClock > 30000) {
    lastClock = now;
    uint32_t e = rtcEpoch();
    if (e) pet.lastSeenEpoch = e;
  }

  // health heartbeat every 5 min (for the soak test; dropped if no monitor)
  static uint32_t lastHealth = 0;
  if (now - lastHealth > 300000) {
    lastHealth = now;
    Serial.printf("HEALTH up=%lus heap=%u min=%u bat=%d%% mv=%d chg=%d usb=%d dim=%u off=%d\n",
                  (unsigned long)(now / 1000), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  batPercent(), batMillivolts(), batCharging() ? 1 : 0,
                  usbPresent() ? 1 : 0, dimStage, screenOff ? 1 : 0);
  }

  // Draw pacing (power.h): 10 fps while the pet is in use, less the longer without touch,
  // 85 ms in game/sack (safe margin so the redraw does not overrun the DMA send of the previous
  // frame: at 40-65 ms it overlapped and caused black flashes).
  // With the screen off nothing is drawn: at brightness 0 nothing is visible, but the redraw and the
  // canvas framebuffer DMA flush kept running at 10 fps, and measured that is 70 ms of
  // work per 100 (46 of them just the flush). Each render repaints the whole scene,
  // so nothing is left half-done; and since lastRender stays frozen, the first
  // frame after waking comes out immediately.
  PowerCtx pc = powerCtx(millis());
  NbSelf nbSelf = { !pet.isEgg() && pet.speciesId >= 1, (uint8_t)pet.speciesId, pet.shiny, pet.sleeping, pet.level() };
  nearbyPoll(now, pc, nbSelf);
  if (!screenOff) ensureNeighbors();
  audioSetIdle(!amplifierWanted(pc));  // the amplifier draws power at idle
  batSetPollMs(powerCacheMs(pc));
  uint32_t interval = frameIntervalMs(pc);
  bool drewFrame = false;
  if (interval && now - lastRender >= interval) {
    lastRender = now;
    drewFrame = render();
  }
  // yield the CPU on passes that do not draw: without this the loop spins at full speed
  // between frames; 1 ms is not noticeable on touch (read every 20 ms) and the frame is not delayed
  if (!drewFrame) idleWait(pc);
}

// current power state (see power.h)
PowerCtx powerCtx(uint32_t now) {
  PowerCtx c;
  c.idleMs = idleSince(now, lastInteract);
  c.screenOff = screenOff;
  c.sleeping = pet.sleeping;
  c.gameOpen = gameOpen || sackOpen;
  c.usb = usbPresent();
  c.charging = batCharging();
  c.batPct = batPercent();
  return c;
}

// the user interacted: normal draw pacing returns (without waiting for the next
// frame of a slow pace) and the amplifier turns on before anything sounds
void wakeUI() {
  uint32_t now = millis();
  uint32_t iv = frameIntervalMs(powerCtx(now));
  bool slow = !iv || iv > 100;  // screen off or slow pace: draw now, without waiting for the frame
  lastInteract = now;
  // the forced frame respects the 85 ms between redraws (otherwise it overlaps the previous DMA)
  if (slow) lastRender = (now - lastRender >= 85) ? 0 : lastRender - 15;
  audioSetIdle(false);
}

// brightness by sleep + inactivity (AMOLED protection) and battery
void updateBrightness(uint32_t now) {
  // visible events wake the screen by themselves
  if (!screenOff && (pet.evolving() || pet.ceremony || pet.eating() || pet.showHeart())) {
    if (dimStageFor(idleSince(now, lastInteract)) != DIM_AWAKE) wakeUI();
    else lastInteract = now;
  }
  PowerCtx c = powerCtx(now);
  dimStage = dimStageFor(c.idleMs);
  uint8_t target = brightnessFor(c);
  static uint8_t current = 255;
  if (target != current) {
    current = target;
    panel->setBrightness(target);
  }
}

// Pause for a loop pass that did not draw. With the screen off, or on but almost
// off (5 min untouched), and without USB the CPU sleeps (light sleep) until the next frame or
// 200 ms: it wakes on the timer or on a touch (the touch INT goes low). Not with USB, so as
// not to lose the SD card serial port: checked uncached, in case it was just plugged in.
void idleWait(PowerCtx c) {
  // do not sleep right after drawing (the frame's DMA flush is still running) nor with a warning sounding
  if (lightSleepAllowed(c) && millis() - lastRender >= 100 && !audioBusy() && !nearbyRadioOn()) {
    c.usb = usbPresentNow();
    if (!lightSleepAllowed(c)) { delay(idleDelayMs(c)); return; }
    if (digitalRead(TP_INT) == LOW) { gTouchIrq = true; delay(1); return; }  // touch pending: do not sleep
    uint32_t ms = POWER_SLEEP_MS;
    // while something moves, wake often enough to see steps; at rest the normal long sleep
    // (the first step or two of a walk are lost while it ramps up)
    if (imuMotionRecent(millis()) && ms > IMU_POLL_MS) ms = IMU_POLL_MS;
    uint32_t iv = frameIntervalMs(c);
    if (iv) {  // screen on: until the next frame
      uint32_t due = lastRender + iv - millis();
      if ((int32_t)due < (int32_t)ms) ms = (int32_t)due > 1 ? due : 1;
    }
    uint32_t nb = nearbyNextEventMs(millis());  // sleep does not go past the next radio window
    if (nb && nb < ms) ms = nb;
    esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000);
    gpio_wakeup_enable((gpio_num_t)TP_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
    // the touch interrupt goes back to edge-triggered (as attachInterrupt left it); a touch
    // that already pulled the line low is registered by hand
    gpio_wakeup_disable((gpio_num_t)TP_INT);
    gpio_set_intr_type((gpio_num_t)TP_INT, GPIO_INTR_NEGEDGE);
    if (digitalRead(TP_INT) == LOW) gTouchIrq = true;
    return;
  }
  delay(idleDelayMs(c));
}

// ---------- serial console (SD provisioning + debugging) ----------

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  if (sdSerialCommand(line)) return;

  // Browser save backup: SAVEGET -> "SAVE <n>", "SAVEHEX <hex>", DONE.
  // SAVEPUT <n> -> OK, then n raw bytes -> DONE and a reboot. The checksum in the
  // snapshot rejects altered or cut-off data before the save changes.
  if (line == "SAVEGET") {
    uint8_t backup[Pet::BACKUP_MAX_BYTES];
    size_t size = pet.exportBackup(backup, sizeof(backup));
    if (!size) { Serial.println("ERR"); return; }
    // setup() sets a zero TX timeout so logs never block without a terminal open; the
    // browser is reading now, so wait for the whole answer instead of dropping it
    Serial.setTxTimeoutMs(1500);
    char hex[2 * Pet::BACKUP_MAX_BYTES + 1];
    for (size_t i = 0; i < size; i++) snprintf(hex + 2 * i, 3, "%02X", backup[i]);
    Serial.printf("SAVE %u\nSAVEHEX %s\nDONE\n", (unsigned)size, hex);
    Serial.flush();
    Serial.setTxTimeoutMs(0);
    return;
  }
  if (line.startsWith("SAVEPUT ")) {
    uint32_t size = (uint32_t)line.substring(8).toInt();
    if (size == 0 || size > Pet::BACKUP_MAX_BYTES) { Serial.println("ERR"); return; }
    uint8_t backup[Pet::BACKUP_MAX_BYTES];
    Serial.println("OK");
    uint32_t oldTimeout = Serial.getTimeout();
    Serial.setTimeout(6000);
    size_t got = Serial.readBytes(backup, size);
    Serial.setTimeout(oldTimeout);
    if (got != size || !pet.importBackup(backup, size)) {
      while (Serial.available()) Serial.read();  // leftovers of a bad file must not run as commands
      Serial.println("ERR");
      return;
    }
    Serial.println("DONE");
    delay(150);
    ESP.restart();
  }

  if (line == "HATCH") {
    pet.eggTap(); pet.eggTap(); pet.eggTap();
    Serial.println("DONE");
  } else if (line.startsWith("SPEC ")) {
    int n = line.substring(5).toInt();
    if (n >= 1 && n <= DEX_COUNT) {
      pet.prevSpeciesId = pet.speciesId;
      pet.speciesId = n;
      pet.saveNow();
      Serial.printf("especie #%d %s\n", n, DEX_TBL[n].name);
    }
    Serial.println("DONE");
  } else if (line.startsWith("LVL ")) {
    String arg = line.substring(4);
    arg.trim();
    long lv = arg.toInt();
    if (arg.length() && isdigit((unsigned char)arg[0]) && lv <= 998) {  // written at once now: a typo must not reach the flash
      pet.ageMinutes = (uint32_t)lv * MINUTES_PER_LEVEL;
      pet.saveNow();
    } else {
      Serial.println("LVL 0..998");
    }
    Serial.println("DONE");
  } else if (line.startsWith("TIME ")) {
    uint32_t e = (uint32_t)line.substring(5).toInt();
    rtcSetEpoch(e);
    pet.setClock(e);
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line.startsWith("RTCSET ")) {  // RTC only (simulate power-offs in tests)
    rtcSetEpoch((uint32_t)line.substring(7).toInt());
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "TIME") {
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "GAL") {
    galleryOpen = !galleryOpen;
    galleryDetail = 0;
    galleryDirty = true;
    if (!galleryOpen) galleryPmd.unload();
    Serial.println("DONE");
  } else if (line == "EGGS") {
    // simulates 20 egg rolls (does not change the game state)
    for (int i = 0; i < 20; i++) {
      int16_t d = pet.pickEggSpecies();
      Serial.printf("%d:%s(r%u) ", d, DEX_TBL[d].name, DEX_TBL[d].rarity);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line == "SHINY") {  // toggles shiny on the current one (tests)
    pet.shiny = !pet.shiny;
    pet.saveNow();
    Serial.printf("shiny=%d\n", pet.shiny);
    Serial.println("DONE");
  } else if (line.startsWith("NICK ")) {
    pet.rename(line.substring(5).c_str());
    Serial.printf("nick=%s\n", pet.nick);
    Serial.println("DONE");
  } else if (line == "CAREDAY") {  // simulates a new cared-for day (tests)
    pet.setClock(pet.lastSeenEpoch + 86400);
    pet.caress();
    pet.saveNow();
    Serial.printf("streak=%u bond=%u medals=0x%X\n", pet.streak, pet.bond, pet.medals);
    Serial.println("DONE");
  } else if (line == "BYE") {
    pet.startFarewell();
    Serial.println("DONE");
  } else if (line == "RUN") {
    pet.startRunaway();
    Serial.println("DONE");
  } else if (line == "BEEP") {
    sfxPlay(SFX_HATCH);  // audio test
    Serial.println("DONE");
  } else if (line == "ABANDON") {
    pet.dbgRunawayReady();  // forces the "ready to run away" state (button test)
    Serial.println("DONE");
  } else if (line == "WIPE") {
    pet.factoryReset();     // wipes NVS and restarts -> new game (starter choice)
    Serial.println("DONE");
    delay(100);
    ESP.restart();
  } else if (line == "REG") {
    Serial.printf("pokedex %u/151:", pet.registeredCount());
    for (int i = 1; i <= 151; i++)
      if (pet.isRegistered(i)) Serial.printf(" %d", i);
    Serial.println();
    Serial.println("DONE");
  } else if (line == "HEALTH") {
    Serial.printf("up=%lus heap=%u min=%u sd=%d mon=%d\n",
                  (unsigned long)(millis() / 1000), ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), sdReady, pmd.loaded || mon.loaded);
    Serial.println("DONE");
  } else if (line == "STATS") {
    Serial.printf("spec=%d nv=%u com=%u fel=%u ene=%u lim=%u desc=%u sd=%d mon=%d bat=%d usb=%d rtc=%u\n",
                  pet.speciesId, pet.level(), pet.fullness, pet.joy, pet.energy,
                  pet.hygiene, pet.careMistakes, sdReady, mon.loaded,
                  batPercent(), usbPresent(), rtcEpoch());
    Serial.printf("peso=%u fue=%u def=%u vel=%u genes=%u/%u/%u tr=%u/%u/%u baya=%d\n",
                  pet.weight, pet.atkStat(), pet.defStat(), pet.speStat(),
                  pet.geneAtk, pet.geneDef, pet.geneSpe,
                  pet.trAtk, pet.trDef, pet.trSpe, pet.berryKnown);
    Serial.printf("shiny=%d streak=%u/%u bond=%u medals=0x%X(%u) nick=%s\n",
                  pet.shiny, pet.streak, pet.bestStreak, pet.bond, pet.medals,
                  pet.totalMedals, pet.nick);
    Serial.println("DONE");
  }
}

// ---------- entrada tactil ----------

// A dialog whose time ran out is no longer on screen, even if render() has not yet
// erased it: it must not receive touches or block gestures.
void expireDialogs() {
  if (choiceKind && !timeLeft(choiceUntil)) choiceKind = 0;
  if (confirmUntil && !timeLeft(confirmUntil)) confirmUntil = 0;
  if (feedMenuUntil && !timeLeft(feedMenuUntil)) feedMenuUntil = 0;
}

bool inPetZone(int16_t x, int16_t y) {
  return x > U(110) && x < U(356) && y > UY(95) && y < UY(310);
}

// the touch is resolved on finger LIFT to tell a tap from a swipe
void handleTouch() {
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 20) return;  // 50 Hz is plenty for a finger
  lastPoll = millis();
  // we only touch the bus if the chip signaled via INT or if the finger is still down (we
  // must detect the lift). Reading the sleeping CST9217 hung for ~1s and
  // froze the whole loop; SensorLib does not honor the Wire timeout.
  if (!gTouchIrq && !wasPressed) return;
  gTouchIrq = false;
  // An address-only cycle before reading. Without it, getPoint() hung for
  // exactly 1000 ms (the default timeout of the I2C driver, which SensorLib does not
  // bound) and froze the whole loop: this is what was perceived as "the minigame
  // freezes for 2-3 seconds when touching the ball" (issue #16), because there the touches
  // are fast and back to back.
  //
  // Measured on the board, playing the same thing (~470 reads, score ~50):
  //   without this line: 5 stalls of 1000 ms in 60 s
  //   with it:          0
  // The reject counter came out 0 in both cases, so it does NOT work by
  // skipping reads when the chip does not answer: what it does is wake it up,
  // so the next read does not find the CST9217 asleep.
  Wire.beginTransmission(TOUCH_ADDR);
  if (Wire.endTransmission() != 0) return;  // no response: retry in 20 ms
  int16_t x, y;
  bool pressed = touch.getPoint(&x, &y, 1) > 0;
  expireDialogs();  // long press and gestures see the same state as the drawing

  // screen off with PWR: a touch only wakes it if it is held in place (see touchWakes)
  if (screenOff) {
    if (pressed && !wasPressed) {
      tX0 = tXl = x;
      tY0 = tYl = y;
      tStart = millis();
      holdFired = false;
    } else if (pressed) {
      tXl = x;
      tYl = y;
      if (!holdFired && touchWakes(millis() - tStart, tXl - tX0, tYl - tY0, U(30), UY(30))) {
        screenOff = false;
        holdFired = true;
        swallowGesture = true;  // the finger is still down: its release does nothing
        wakeUI();
        sfxPlay(SFX_TAP);
      }
    } else {
      holdFired = false;
    }
    wasPressed = pressed;
    return;
  }

  // training sack: every touch counts instantly (rapid mashing)
  if (sackOpen) {
    if (pressed && !wasPressed) {
      wakeUI();
      sackTouch(y);
      if (!sackOpen) swallowGesture = true;  // the touch that quit is not a gesture when it lifts
    }
    wasPressed = pressed;
    return;
  }

  if (pressed && !wasPressed) {  // gesture starts
    tX0 = tXl = x;
    tY0 = tYl = y;
    tStart = millis();
    holdFired = false;
    swallowGesture = dimStage > 0;  // if it was dark, it only wakes up (screenOff is handled above)
    wakeUI();
    // like the sack: a hit counts on touch, not when the finger lifts. The header (quit) still
    // waits for the lift so a swipe that starts there cannot end the game; no overlay is on top
    if (!swallowGesture && gameOpen && y >= HEADER_ZONE_H && !kbOpen && !clockOpen && !cardOpen && !galleryOpen) {
      gameTap(x, y);
      swallowGesture = true;            // no second tap on lift
    }
  } else if (pressed) {  // still pressed
    tXl = x;
    tYl = y;
    // long press without moving on the pet -> release dialog
    if (!holdFired && !swallowGesture && !galleryOpen && !cardOpen && !kbOpen && !clockOpen && millis() - tStart > 3000 &&
        abs(tXl - tX0) < U(30) && abs(tYl - tY0) < UY(30) && inPetZone(tX0, tY0) &&
        !pet.isEgg() && !confirmUntil && !choiceKind && !pet.ceremony) {
      confirmUntil = millis() + 10000;
      holdFired = true;
    }
  } else if (wasPressed) {  // finger lifts: resolve gesture
    wakeUI();
    int dx = tXl - tX0, dy = tYl - tY0;
    uint32_t dt = millis() - tStart;
    if (!holdFired && !swallowGesture) {
      if (abs(dx) > U(80) && abs(dy) < UY(70) && dt < 800) onSwipe(dx > 0 ? 1 : -1);
      else if (abs(dy) > UY(80) && abs(dx) < U(70) && dt < 800) onSwipeV(dy > 0 ? 1 : -1);
      else if (dt < 1500 && abs(dx) < U(40) && abs(dy) < UY(40)) onTap(tX0, tY0);
    }
  }
  wasPressed = pressed;
}

// vertical swipe: opens/closes the pet card
void openClock();  // prototype

void onSwipeV(int dir) {
  expireDialogs();
  if (pet.awaitingStarter()) return;  // blocked during the starter choice
  if (gameOpen || galleryOpen || kbOpen || sackOpen || pet.ceremony) return;
  if (clockOpen) { clockOpen = false; return; }
  if (cardOpen) {
    if (dir < 0) cardOpen = false;  // up closes the card
    return;
  }
  if (choiceKind) return;           // a decision dialog is waiting: it is answered or it times out
  if (dir > 0) {                    // swipe down: set the time
    if (!confirmUntil && !feedMenuUntil) openClock();
  } else if (!pet.isEgg() && !confirmUntil && !feedMenuUntil) {
    cardOpen = true;                // swipe up: card
    cardPage = 0;
  }
}

// swipe: dir +1 = to the right
void onSwipe(int dir) {
  expireDialogs();
  if (pet.awaitingStarter()) return;  // blocked during the starter choice
  if (gameOpen || kbOpen || clockOpen) return;
  if (cardOpen) {  // inside the card: switch between the 4 pages
    int p = (int)cardPage + (dir > 0 ? -1 : 1);  // left advances
    cardPage = p < 0 ? 0 : (p > CARD_PAGES - 1 ? CARD_PAGES - 1 : p);
    return;
  }
  if (!galleryOpen) {
    if (!pet.ceremony && !confirmUntil && !choiceKind && !feedMenuUntil) {
      galleryOpen = true;
      galleryPage = 0;
      galleryDetail = 0;
      galleryDirty = true;
    }
    return;
  }
  if (galleryDetail) {  // in detail: back to the grid
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  int np = galleryPage - dir;  // swiping left advances a page
  if (np < 0) {                // going back from the first = exit
    galleryOpen = false;
    galleryPmd.unload();
    return;
  }
  if (np > 9) np = 9;
  if (np != galleryPage) {
    galleryPage = np;
    galleryDirty = true;
  }
}

void onTap(int16_t x, int16_t y) {
  // Serial.printf("TOUCH %d %d\n", x, y);  // diagnostic (silenced: floods the log)
  if (pet.awaitingStarter()) {  // first game: pick a starter
    for (int i = 0; i < 3; i++) {
      int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
      if (x >= STARTER_ROW_X && x <= STARTER_ROW_X + STARTER_ROW_W && y >= ry && y <= ry + STARTER_ROW_H) {
        pet.chooseStarter(STARTER_DEX[i]);
        sfxPlay(SFX_TAP);
        break;
      }
    }
    return;
  }
  if (galleryOpen) {
    galleryTap(x, y);
    return;
  }
  if (kbOpen) {
    keyboardTap(x, y);
    return;
  }
  if (clockOpen) {
    clockTap(x, y);
    return;
  }
  expireDialogs();
  if (pet.ceremony) return;  // no buttons during the farewell
  if (cardOpen) {
    if (cardPage == 0 && y < CARD_NAME_ZONE_H) openKeyboard();  // touching the name = rename
    else if (cardPage == 1 && y >= TRAIN_Y && y <= TRAIN_Y + TRAIN_H && x >= TRAIN_X && x <= TRAIN_X + TRAIN_W) {
      cardOpen = false;            // TRAIN STRENGTH button
      startSack();
    } else {
      cardOpen = false;
    }
    return;
  }
  if (gameOpen) {
    gameTap(x, y);
    return;
  }
  if (choiceKind) {          // decision dialog: action button (top) / keep (bottom)
    bool b1 = (x >= CHOICE_X && x <= CHOICE_X + CHOICE_W && y >= CHOICE_B1_Y && y <= CHOICE_B1_Y + CHOICE_BH);  // action
    bool b2 = (x >= CHOICE_X && x <= CHOICE_X + CHOICE_W && y >= CHOICE_B2_Y && y <= CHOICE_B2_Y + CHOICE_BH);  // keep / stay together
    if (choiceKind == 1) {                 // evolution
      if (b1 && pet.canEvolveNow()) { int16_t old = pet.speciesId; pet.evolve(); evoPmd.load(old, pet.shiny); }
      else if (b2) pet.declineEvolve();
    } else if (choiceKind == 2) {          // farewell
      if (b1 && pet.canFarewellNow()) pet.startFarewell();
      else if (b2) pet.declineFarewell();
    }
    choiceKind = 0;
    return;
  }
  if (confirmUntil) {        // "release?" dialog: YES / NO
    if (x >= YES_X && x <= YES_X + YES_W && y >= YES_Y && y <= YES_Y + YES_H) {
      pet.release();
    }
    confirmUntil = 0;
    return;
  }
  if (feedMenuUntil) {       // food picker
    for (int item = 0; item < 4; item++) {
      int dx = x - buttons[item].cx, dy = y - buttons[item].cy;
      if (dx * dx + dy * dy <= BTN_HIT * BTN_HIT) {
        if (item == 3) pet.feedCandy();
        else pet.feedBerry(item);
        sfxPlay(SFX_EAT);
        break;
      }
    }
    feedMenuUntil = 0;
    return;
  }
  if (pet.isEgg()) {
    pet.eggTap();
    sfxPlay(SFX_TAP);
    return;
  }
  // evolution button: opens the evolve/keep dialog
  if (pet.wantEvolveButton() && x >= EVO_BTN_X && x <= EVO_BTN_X + EVO_BTN_W &&
      y >= EVO_BTN_Y && y <= EVO_BTN_Y + EVO_BTN_H) {
    choiceKind = 1; choiceUntil = millis() + 12000;
    return;
  }
  // ending buttons (same box): direct run-away; farewell opens a dialog
  if (x >= FAR_BTN_X && x <= FAR_BTN_X + FAR_BTN_W &&
      y >= FAR_BTN_Y && y <= FAR_BTN_Y + FAR_BTN_H) {
    if (pet.canRunawayNow()) { pet.startRunaway(); return; }
    if (pet.wantFarewellButton()) { choiceKind = 2; choiceUntil = millis() + 12000; return; }
  }
  for (int i = 0; i < 4; i++) {
    int dx = x - buttons[i].cx, dy = y - buttons[i].cy;
    if (dx * dx + dy * dy <= BTN_HIT * BTN_HIT) {
      Serial.printf("BTN %d\n", i);
      sfxPlay(SFX_TAP);
      if (i == 0) {
        if (!pet.sleeping) feedMenuUntil = millis() + 6000;
      } else if (i == 1) {
        startGame();
      } else if (i == 2) {
        pet.toggleLight();
      } else {
        startBath();
      }
      return;
    }
  }
  // touching the pet = caress
  if (inPetZone(x, y)) {
    Serial.println("PET");
    pet.caress();
    if (!pet.sleeping) {
      sfxPlay(SFX_HEART);
      chirpPlay(pet.speciesId);  // nothing for an egg (dex -1)
    }
  }
}

// ---------- render ----------

bool gNight = false;  // real night (by hour) or sleeping: set by render()
uint16_t inkColor() { return gNight ? UI_INK_NIGHT : UI_INK; }

#define C565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

// UI palette and pieces (defined further down, next to drawHeader)
#define UI_ACC C565(0xFF, 0xC2, 0x1A)
static uint16_t uiBg();
static uint16_t uiSurf();
static uint16_t uiFill();
static uint16_t uiAlert();
static uint16_t uiDim();
static void uiTextIn(int x, int y, int w, int h, const char *t, int size, uint16_t col);
static void uiPill(int x, int y, int w, int h, const char *t, bool primary);
static void drawPanelBase();
static void drawRecordPill(int y);

// ---------- background scene: type biome + real RTC time ----------

#define HORIZON UY(232)  // line where the sky meets the ground

uint16_t lerp565(uint16_t a, uint16_t b, int i, int n) {
  if (n <= 0) return a;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar + (br - ar) * i / n) << 11)) |
                    (((ag + (bg - ag) * i / n) << 5)) | (ab + (bb - ab) * i / n));
}

// hour of day 0-23 (from the real time cached every 30s; 13 if there is no clock)
int sceneHour() {
  uint32_t e = pet.lastSeenEpoch;
  return e ? (int)((e / 3600) % 24) : 13;
}

// sky colors by hour (top and bottom of the gradient)
static void skyColors(bool night, uint16_t &top, uint16_t &bot) {
  int h = sceneHour();
  if (night)            { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (h < 8)       { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }  // dawn
  else if (h < 18)      { top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }  // day
  else                  { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }  // dusk
}

// sky in bands down to the horizon
static void drawSky(int horizon, uint16_t top, uint16_t bot) {
  for (int y = 0; y < horizon; y += UY(8))
    gfx->fillRect(0, y, UI_W, UY(8), lerp565(top, bot, y, horizon));
}

// the night ground blends toward night blue
static uint16_t nightSoil(uint16_t soil) { return lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16); }

// ground of each biome by day (at night it blends toward night blue)
static const uint16_t BIOME_SOIL[6] = {
  C565(0x7e, 0xc0, 0x7f),  // 0 meadow
  C565(0xdc, 0xca, 0x94),  // 1 beach (sand)
  C565(0x4f, 0x8a, 0x55),  // 2 forest
  C565(0x8a, 0x55, 0x44),  // 3 volcano
  C565(0xa8, 0x90, 0x6a),  // 4 mountain
  C565(0xe6, 0xee, 0xf5),  // 5 snow
};

void drawClouds(uint32_t now, uint16_t col) {
  for (int k = 0; k < 2; k++) {
    int cx = (int)(U(now / 50 + k * 250) % (UI_W + U(94))) - U(40);  // px/s proportional to the panel
    int cy = UY(70 + k * 34);
    gfx->fillCircle(cx, cy, U(16), col);
    gfx->fillCircle(cx + U(18), cy + UY(3), U(13), col);
    gfx->fillCircle(cx - U(15), cy + UY(4), U(12), col);
  }
}

void drawScene(uint8_t biome, uint32_t now, bool night) {
  int h = sceneHour();
  uint16_t top, bot;
  skyColors(night, top, bot);
  drawSky(HORIZON, top, bot);

  // sun or moon
  if (night) {
    gfx->fillCircle(U(360), UY(78), U(24), C565(0xe8, 0xee, 0xf5));
    gfx->fillCircle(U(370), UY(72), U(22), lerp565(top, bot, UY(78), HORIZON));  // crescent
    for (auto &st : STARS) gfx->fillRect(U(st[0]), UY(st[1]), SC(4), SC(4), UI_WHITE);
  } else if (h < 18) {
    gfx->fillCircle(U(360), UY(84), U(26), h < 8 ? C565(0xff, 0xd9, 0x8a) : C565(0xff, 0xe7, 0x9f));
    drawClouds(now, C565(0xff, 0xff, 0xff));
  } else {
    gfx->fillCircle(CX, HORIZON - UY(6), U(34), C565(0xff, 0xf1, 0xc8));  // setting sun
  }

  // beach sea: a strip of water over the sand
  uint16_t soil = BIOME_SOIL[biome < 6 ? biome : 0];
  if (night) soil = nightSoil(soil);
  if (biome == 1) {
    uint16_t sea = night ? C565(0x1c, 0x34, 0x52) : C565(0x4f, 0x96, 0xc4);
    gfx->fillRect(0, HORIZON - UY(26), UI_W, UY(26), sea);
    for (int i = 0; i < 3; i++) {
      int wy = HORIZON - UY(26) + UY(4 + i * 7);
      uint16_t fc = night ? C565(0x3a, 0x58, 0x78) : C565(0xbf, 0xe6, 0xf5);
      gfx->fillRect(U(60) + (U(now / 60 + i * 30) % U(60)), wy, U(26), SC(2), fc);
      gfx->fillRect(U(300) - (U(now / 60 + i * 20) % U(60)), wy, U(26), SC(2), fc);
    }
  }

  // ground
  gfx->fillRect(0, HORIZON, UI_W, UI_H - HORIZON, soil);
  uint16_t hill = lerp565(soil, night ? C565(0x0c, 0x12, 0x24) : C565(0xff, 0xff, 0xff), 3, 16);
  gfx->fillRoundRect(-U(60), HORIZON - UY(14), UI_W + U(120), UY(60), U(30), hill);

  // biome details
  uint16_t dk = lerp565(soil, C565(0x10, 0x18, 0x20), night ? 11 : 7, 16);
  if (biome == 2) {  // forest: silhouette conifers
    for (int tx : { U(60), U(150), U(360), U(416) }) {
      gfx->fillTriangle(tx, HORIZON - UY(46), tx - U(16), HORIZON, tx + U(16), HORIZON, dk);
      gfx->fillTriangle(tx, HORIZON - UY(60), tx - U(12), HORIZON - UY(28), tx + U(12), HORIZON - UY(28), dk);
    }
  } else if (biome == 3) {  // volcano: rocks and embers
    gfx->fillTriangle(U(70), HORIZON, U(40), HORIZON + UY(30), U(100), HORIZON + UY(30), dk);
    gfx->fillTriangle(U(400), HORIZON + UY(4), U(372), HORIZON + UY(30), U(430), HORIZON + UY(30), dk);
    if (!night)
      for (int e = 0; e < 4; e++)
        gfx->fillRect(U(120 + e * 70), HORIZON + UY(8 + (e % 2) * 6), SC(4), SC(4), C565(0xff, 0x9b, 0x3a));
  } else if (biome == 4) {  // mountain: peaks in the background
    gfx->fillTriangle(U(140), HORIZON - UY(50), U(60), HORIZON, U(220), HORIZON, dk);
    gfx->fillTriangle(U(330), HORIZON - UY(38), U(250), HORIZON, U(410), HORIZON, dk);
  } else if (biome == 5 && !night) {  // snow: falling flakes
    for (int f = 0; f < 10; f++) {
      int fx = U(f * 53 + now / 40) % UI_W;
      int fy = UY(f * 90 + now / 18) % HORIZON;
      gfx->fillRect(fx, fy, SC(3), SC(3), UI_WHITE);
    }
  } else if (biome == 0) {  // meadow: grass tufts
    for (int gx : { U(80), U(175), U(300), U(395) })
      for (int b = -1; b <= 1; b++)
        gfx->fillRect(gx + U(b * 5), HORIZON + UY(6), SC(2), UY(8 + (b == 0 ? 4 : 0)), dk);
  }
}

// menu background: a disc on black on the round screen, the whole canvas on the
// square one
void drawScreenBg() {
#if LCD_ROUND
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, UI_R, uiBg());
#else
  gfx->fillScreen(uiBg());
#endif
}

// first game: pick a starter among Bulbasaur / Charmander / Squirtle
void renderStarterSelect() {
  drawScreenBg();
  const char *t = T(S_CHOOSE_STARTER);
  gfx->setTextColor(inkColor());
  int ts = fitSize(t, TS(3), TS(2), UY(60));
  gfx->setCursor(centerX(t, ts), UY(64));
  gfx->print(t);
  for (int i = 0; i < 3; i++) {
    int16_t d = STARTER_DEX[i];
    int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
    gfx->fillRoundRect(STARTER_ROW_X, ry, STARTER_ROW_W, STARTER_ROW_H, STARTER_ROW_H / 2, uiSurf());
    const uint8_t *th = thumbs.get(d);     // starter thumbnail (if the SD is ready)
    if (th) drawThumb(th, U(76), ry - UY(5), SC(3), false);
    gfx->setTextColor(inkColor());
    setSize(TS(3));
    gfx->setCursor(U(178), ry + UY(24));
    gfx->print(dexName(d));
  }
  gfx->flush();
}

// ---------- text: size and measurement ----------
// setSize() stores the applied scale because the library has textsize_x protected and textH()
// needs to know it. The font is the classic Arduino_GFX 5x7 (CP437, 6 px advance per character).
uint8_t gTextSize = 1;

void setSize(int n) {
  gTextSize = n;
  gfx->setTextSize(n);
}

// width of a text at scale `size`
uint16_t textW(const char *s, int size) { return (uint16_t)strlen(s) * 6 * size; }

// height of a text line at the applied size
static int textH() { return 7 * gTextSize; }

// cursor x to leave the text centered on CX
int centerX(const char *s, int size) { return CX - textW(s, size) / 2; }

// visible screen width at height y (px): on the round one, the chord of the UI_R circle;
// on the square one, the full width
int chordW(int y) {
#if LCD_ROUND
  int dy = y - CY;
  int h2 = UI_R * UI_R - dy * dy;
  return h2 > 0 ? 2 * (int)sqrtf((float)h2) : 0;
#else
  return UI_W;
#endif
}

// available width for a text line with its top edge at y: the visible chord
// of the screen (on the round one, that of its narrowest point: above or below the line),
// with 4 px of design margin per side. Requires the size already applied (for textH()).
int chordAvail(int y) {
  int topW = chordW(y), botW = chordW(y + textH());
  return (topW < botW ? topW : botW) - U(8);
}

// does the text fit, at that size, in the chord at height y?
bool fitsChord(const char *s, int size, int y) {
  setSize(size);
  return textW(s, size) <= chordAvail(y);
}

// picks the big size if the text fits in its line's chord, and the small one if not
int fitSize(const char *s, int big, int small, int y) {
  if (fitsChord(s, big, y)) return big;
  setSize(small);
  return small;
}

// Splits s into up to maxLines lines of <= maxW px at the applied scale: cuts at the last
// space that fits (or at any character if there are no spaces). Fills
// start/len and returns the number of lines; if the text does not fit in maxLines, the last
// one is left truncated.
int wrapText(const char *s, int size, int maxW, int maxLines, int *start, int *len) {
  int n = 0, pos = 0, L = (int)strlen(s);
  while (pos < L && n < maxLines) {
    while (s[pos] == ' ') pos++;
    if (pos >= L) break;
    int i = pos, end = pos, lastSpace = -1;
    while (i < L) {
      // reaching i implies s[pos..i) fits: a space here is a valid cut even if
      // the space itself no longer fits (a word that ends exactly at maxW)
      if (s[i] == ' ') lastSpace = i;
      if ((i + 1 - pos) * 6 * size > maxW) break;
      end = ++i;
    }
    int cut;
    if (i >= L) cut = L;                           // the whole remainder fits
    else cut = lastSpace > pos ? lastSpace : (end > pos ? end : pos + 1);
    int e = cut;
    while (e > pos && s[e - 1] == ' ') e--;        // no trailing spaces on the line
    start[n] = pos;
    len[n] = e - pos;
    n++;
    pos = cut;
  }
  return n;
}

// Writes s at scale `size`, split into <= maxLines lines of <= maxW px, with the block
// vertically centered on yMid. leftAlign: x is the left edge; otherwise, the center.
void drawWrapped(const char *s, int size, int x, int yMid, int maxW, int maxLines, bool leftAlign) {
  setSize(size);
  int st[4], ln[4];
  int n = wrapText(s, size, maxW, maxLines > 4 ? 4 : maxLines, st, ln);
  int gap = U(4);
  int total = n * textH() + (n - 1) * gap;
  int y = yMid - total / 2;
  for (int i = 0; i < n; i++) {
    char line[128];  // game strings are < 64 bytes
    int l = ln[i] < (int)sizeof(line) - 1 ? ln[i] : (int)sizeof(line) - 1;
    memcpy(line, s + st[i], l);
    line[l] = 0;
    gfx->setCursor(leftAlign ? x : x - textW(line, size) / 2, y + i * (textH() + gap));
    gfx->print(line);
  }
}

// Screens that only change on a touch (or on data that changes very rarely):
// while their "key" does not change they are not repainted or flushed over QSPI.
static bool staticScreen() {
  if (pet.awaitingStarter()) return true;
  if (galleryOpen || gameOpen || sackOpen) return false;  // the gallery handles its own repainting
  if (kbOpen || clockOpen) return true;
  return cardOpen && cardPage != 0;  // page 0 has the animated portrait
}
static uint32_t mixKey(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t screenKey() {
  uint32_t h = 2166136261u;
  h = mixKey(h, pet.awaitingStarter() ? 1 : kbOpen ? 2 : clockOpen ? 3 : 4 + cardPage);
  h = mixKey(h, sdEpoch);
  if (kbOpen) {
    h = mixKey(h, nameLen);
    for (int i = 0; i < nameLen; i++) h = mixKey(h, (uint8_t)nameBuf[i]);
  } else if (clockOpen) {
    h = mixKey(h, clockH * 60 + clockM);
    h = mixKey(h, audioEnabled());
  } else if (cardOpen) {
    for (uint32_t v : { (uint32_t)pet.speciesId, (uint32_t)pet.shiny, (uint32_t)pet.ageMinutes, (uint32_t)pet.streak,
                        (uint32_t)pet.bestStreak, (uint32_t)pet.bond, (uint32_t)pet.medals, (uint32_t)pet.weight,
                        (uint32_t)pet.trAtk, (uint32_t)pet.trDef, (uint32_t)pet.trSpe, (uint32_t)pet.geneAtk,
                        (uint32_t)pet.geneDef, (uint32_t)pet.geneSpe, (uint32_t)pet.careMistakes,
                        (uint32_t)pet.fullness, (uint32_t)pet.joy, (uint32_t)pet.energy, (uint32_t)pet.hygiene,
                        (uint32_t)pet.berryKnown, (uint32_t)pet.strHi, pet.stepsToday, pet.stepsTotal,
                        (uint32_t)pet.stepRewardMask })
      h = mixKey(h, v);
    for (const char *c = pet.nick; *c; c++) h = mixKey(h, (uint8_t)*c);
  }
  return h;
}
bool screenKeyValid = false;  // (the host renderer invalidates it between scenes)
uint32_t lastScreenKey = 0;

// Draws a frame and returns true if it flushed it to the panel.
bool render() {
  int h = sceneHour();
  gNight = pet.sleeping || h < 6 || h >= 20;  // theme: dark at night or while sleeping, light by day
  if (staticScreen()) {
    uint32_t k = screenKey();
    if (screenKeyValid && k == lastScreenKey) return false;
    lastScreenKey = k;
    screenKeyValid = true;
  } else {
    screenKeyValid = false;
  }
  if (pet.awaitingStarter()) {  // first game: pick a starter (top priority)
    renderStarterSelect();
    return true;
  }
  if (galleryOpen) {
    renderGallery();
    return true;
  }
  if (gameOpen) {
    renderGame();
    return true;
  }
  if (sackOpen) {
    renderSack();
    return true;
  }
  if (kbOpen) {
    renderKeyboard();
    return true;
  }
  if (clockOpen) {
    renderClock();
    return true;
  }
  if (cardOpen) {
    renderCard();
    return true;
  }
  // drawScene covers the whole canvas: no fillScreen(BLACK) beforehand so
  // an overlapping DMA flush never captures a half-black frame (anti-flicker)
  if (choiceKind) {
    if (!timeLeft(choiceUntil)) choiceKind = 0;
    else { drawScreenBg(); drawChoiceDialog(); gfx->flush(); return true; }
  }
  if (confirmUntil) {
    if (!timeLeft(confirmUntil)) confirmUntil = 0;
    else { drawScreenBg(); drawReleaseDialog(); gfx->flush(); return true; }
  }
  drawScene(pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome, millis(), gNight);

  if (pet.ceremony) {
    const char *msg = (pet.ceremony == CER_FAREWELL) ? T(S_FAREWELL)
                      : (pet.ceremony == CER_RUNAWAY) ? T(S_RUNAWAY)
                                                      : T(S_GOODBYE);
    drawHeader(dexName(pet.speciesId), msg, false);
    drawCeremony();
    gfx->flush();
    return true;
  }

  if (pet.isEgg()) {
    drawHeader(T(S_EGG_HDR), eggMsg(), false);
    int s = SC(5), x = CX - 16 * s, y = PET_CY - 16 * s;
    drawMap(SPR_EGG, SPRITE_H, x, y, s, false);
    if (pet.eggCracks() >= 1)
      for (auto &c : CRACK1) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggCracks() >= 2)
      for (auto &c : CRACK2) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggRarity() >= R_RARO) {
      const char *rar = (pet.eggRarity() == R_LEGENDARIO) ? T(S_EGG_LEGEND) : T(S_EGG_RARE);
      gfx->setTextColor(pet.eggRarity() == R_LEGENDARIO ? UI_BAR_WARN : 0x4C98);
      setSize(TS(2));
      gfx->setCursor(centerX(rar, TS(2)), UY(316));
      gfx->print(rar);
    }
    char reg[24];
    snprintf(reg, sizeof(reg), T(S_POKEDEX_FMT), pet.registeredCount());
    drawPanelBase();
    gfx->setTextColor(inkColor());
    setSize(TS(2));
    gfx->setCursor(centerX(reg, TS(2)), UY(372));
    gfx->print(reg);
  } else {
    char name[28];
    char lvl[12];
    const char *base = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
    snprintf(name, sizeof(name), "%s%s", pet.shiny ? "*" : "", base);
    snprintf(lvl, sizeof(lvl), T(S_LVL_FMT), pet.level());
    // the second line only says something when something is happening; otherwise, the level
    const char *st = statusMsg();
    bool calm = (st == T(S_HAPPY));
    bool alert = !pet.sleeping && !pet.eating() && pet.lowestStat() < 25;
    drawHeader(name, calm ? lvl : st, alert);
    drawNeighbors();
    drawPet();
    drawBath();
    drawPoops();
    // bottom panel: clean base with the curved edge; the four circles are need + action
    drawPanelBase();
    drawDeck();
    drawCelebration();
    // no dialog open on top: the farewell button is wider than the dialog and peeked out at the sides
    if ((choiceKind && timeLeft(choiceUntil)) || timeLeft(confirmUntil) || timeLeft(feedMenuUntil)) {
      // a dialog covers the button
    } else if (pet.wantEvolveButton()) drawEvolveButton();        // red CTA: evolve
    else if (pet.canRunawayNow()) drawRunawayButton();     // gloomy CTA: run away (neglect)
    else if (pet.wantFarewellButton()) drawFarewellButton();  // gold CTA: farewell
  }

  if (pet.sleeping) {
    gfx->setTextColor(UI_INK_NIGHT);
    setSize(TS(3));
    gfx->setCursor(U(320), UY(130));
    gfx->print("Zz");
  }

  // food picker: the four circles become the foods
  if (feedMenuUntil) {
    if (!timeLeft(feedMenuUntil)) {
      feedMenuUntil = 0;
    } else {
      const char *const *const items[4] = { SPR_ICON_FOOD, SPR_ICON_BERRY_B, SPR_ICON_BERRY_G, SPR_ICON_CANDY };
      for (int i = 0; i < 4; i++) {
        gfx->fillCircle(buttons[i].cx, buttons[i].cy, BTN_R, uiSurf());
        drawMap(items[i], 16, FEED_ICON_X(i), FEED_ICON_Y(i), SC(3), false);
      }
    }
  }

  gfx->flush();
  return true;
}

// ---------- minigame: touches with the pokeball ----------

void startGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || pet.evolving()) return;
  gameOpen = true;
  gameOverUntil = 0;
  gameScore = 0;
  gameMisses = 0;
  gameNewHi = false;
  hitTime = 0;
  gamePetX = CX;
  lastGameStep = millis();
  respawnBall();
}

void respawnBall() {
  ballX = U(150) + random(U(166));
  ballY = UY(96);
  float sp = 1.6f + gameScore * 0.05f;  // livelier as you progress
  if (sp > 4.0f) sp = 4.0f;
  sp *= UI_F;
  ballVX = random(2) ? sp : -sp;
  ballVY = 0;
}

void gameTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  if (y < HEADER_ZONE_H) {  // touching the header = quit without reward
    gameOpen = false;
    return;
  }
  float dx = ballX - x, dy = ballY - y;
  if (dx * dx + dy * dy < (float)U(74) * U(74)) {  // ball touched!
    gameScore++;
    sfxPlay(SFX_PLAY);
    // softer hit: moderate impulse that grows slowly with the score
    float lift = (6.6f + (gameScore > 16 ? 3.5f : gameScore * 0.22f)) * UI_F;
    ballVY = -lift;
    ballVX += dx * 0.12f;
    const float vmax = 6.5f * UI_F;
    if (ballVX > vmax) ballVX = vmax;
    if (ballVX < -vmax) ballVX = -vmax;
    hitX = ballX;
    hitY = ballY;
    hitTime = millis();
  }
}

void stepGame() {
  uint32_t now = millis();
  // step relative to 85 ms, the frame period these constants were tuned
  // for (see the render scheduler in loop()): without this the physics
  // advanced "one step per frame" and the game ran faster or slower depending on
  // how long the previous frame took to draw, which depends on the size of the
  // loaded sprite. With a Charizard the ball fell more slowly than with a
  // Diglett, so the record was not comparable between species.
  float k = lastGameStep ? (now - lastGameStep) / 85.0f : 1.0f;
  if (k > 3.0f) k = 3.0f;  // abnormally late frame: do not take a huge jump
  lastGameStep = now;

  float grav = 0.40f + gameScore * 0.013f;  // falls a bit faster each time
  if (grav > 0.80f) grav = 0.80f;
  grav *= UI_F;
  ballVY += grav * k;
  ballX += ballVX * k;
  ballY += ballVY * k;
#if LCD_ROUND
  // bounce off the circular wall
  float dx = ballX - CX, dy = ballY - CY;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > BALL_R) {
    float nx = dx / d, ny = dy / d;
    float dot = ballVX * nx + ballVY * ny;
    if (dot > 0) {
      ballVX = (ballVX - 2 * dot * nx) * 0.85f;
      ballVY = (ballVY - 2 * dot * ny) * 0.85f;
    }
    ballX = CX + nx * BALL_R;
    ballY = CY + ny * BALL_R;
  }
#else
  // bounce off the square's walls and ceiling (same margin as the circular wall):
  // the position is always corrected and the velocity is only reversed if the ball is heading outward
  if (ballX < CX - BALL_R) { ballX = CX - BALL_R; if (ballVX < 0) ballVX *= -0.85f; }
  else if (ballX > CX + BALL_R) { ballX = CX + BALL_R; if (ballVX > 0) ballVX *= -0.85f; }
  if (ballY < CY - BALL_R) { ballY = CY - BALL_R; if (ballVY < 0) ballVY *= -0.85f; }
#endif
  if (ballY > UY(384)) {  // to the ground
    if (++gameMisses >= 3) {
      gameNewHi = (gameScore > pet.gameHi);
      pet.playResult(gameScore);  // updates the record and gives happiness
      sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
      gameOverUntil = millis() + 4000;
    } else {
      respawnBall();
    }
  }
  // the pet follows it from below
  float chase = (ballX - gamePetX) * 0.12f;
  const float cmax = 7 * UI_F;
  if (chase > cmax) chase = cmax;
  if (chase < -cmax) chase = -cmax;
  gamePetX += chase * k;
}

// ---------- training sack (trains strength) ----------

void startSack() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || pet.evolving()) return;
  sackOpen = true;
  sackUntil = millis() + 10000;
  sackOverUntil = 0;
  sackHits = 0;
  sackShake = 0;
  sackNewHi = false;
}

// touch with the sack open: the header quits, the rest hits
void sackTouch(int16_t y) {
  if (y < HEADER_ZONE_H) sackOpen = false;
  else sackTap();
}

void sackTap() {
  if (!timeLeft(sackUntil)) return;  // time is already up
  sackHits++;
  sackShake = 16 * UI_F;  // shakes the sack
}

void drawGameScene();  // prototype (defined further down)

void renderSack() {
  uint32_t now = millis();
  drawGameScene();  // habitat background
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  // result screen
  if (sackOverUntil) {
    if (!timeLeft(sackOverUntil)) { sackOpen = false; return; }
    char b[20];
    snprintf(b, sizeof(b), T(S_HITS_FMT), sackHits);
    gfx->setTextColor(ink);
    setSize(TS(4));
    gfx->setCursor(centerX(b, TS(4)), UY(150));
    gfx->print(b);
    char g[18];
    snprintf(g, sizeof(g), T(S_STR_GAIN_FMT), sackGain);
    gfx->setTextColor(ink);
    setSize(TS(3));
    gfx->setCursor(centerX(g, TS(3)), UY(210));
    gfx->print(g);
    setSize(TS(2));
    if (sackNewHi && sackHits > 0) {
      drawRecordPill(UY(246));
    } else {
      char r[18];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.strHi);
      gfx->setTextColor(ink);
      gfx->setCursor(centerX(r, TS(2)), UY(256));
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // the 10 s are up: apply training
  if (!timeLeft(sackUntil)) {
    sackNewHi = (sackHits > pet.strHi);
    sackGain = pet.trainStrength(sackHits);
    sfxPlay(sackNewHi ? SFX_MEDAL : SFX_PLAY);
    sackOverUntil = now + 3500;
    gfx->flush();
    return;
  }

  // active mashing
  sackShake *= 0.84f;
  int off = (int)(sackShake * sinf(now * 0.05f));
  int sx = CX + off, top = UY(86);
  gfx->fillRect(CX - U(3), UY(56), U(6), top - UY(56), ink);          // hook/rope
  gfx->fillRect(sx - U(4), top - UY(30), U(8), UY(34), ink);          // chain
  gfx->fillRoundRect(sx - U(42), top, U(84), UY(150), U(26), C565(0xb5, 0x3a, 0x3a));  // sack
  gfx->fillRoundRect(sx - U(42), top, U(84), UY(22), U(18), C565(0x7e, 0x28, 0x28));   // lid
  gfx->drawRoundRect(sx - U(42), top, U(84), UY(150), U(26), ink);
  gfx->fillRect(sx - U(42), top + UY(70), U(84), UY(4), C565(0x7e, 0x28, 0x28));        // stitching

  // hit counter
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", sackHits);
  gfx->setTextColor(ink);
  setSize(TS(6));
  gfx->setCursor(centerX(buf, TS(6)), UY(268));
  gfx->print(buf);

  setSize(TS(2));
  gfx->setCursor(centerX(T(S_HIT_FAST), TS(2)), UY(322));
  gfx->print(T(S_HIT_FAST));

  // time bar
  uint32_t left = sackUntil - now;
  int bw = U(280), fw = (int)((uint32_t)bw * left / 10000);
  gfx->fillRoundRect(CX - bw / 2, UY(350), bw, UY(16), U(5), UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, UY(350), fw, UY(16), U(5), UI_BAR_OK);

  gfx->flush();
}

// minigame background: the pet's habitat (sky by hour + biome ground)
void drawGameScene() {
  int hh = sceneHour();
  bool night = hh < 6 || hh >= 20;
  uint16_t top, bot;
  skyColors(night, top, bot);
  int hor = UY(376);
  drawSky(hor, top, bot);
  if (night)
    for (auto &st : STARS) gfx->fillRect(U(st[0]), UY(st[1]), SC(4), SC(4), UI_WHITE);
  uint8_t bio = pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome;
  uint16_t soil = BIOME_SOIL[bio < 6 ? bio : 0];
  if (night) soil = nightSoil(soil);
  gfx->fillRect(0, hor, UI_W, UI_H - hor, soil);
}

void renderGame() {
  // no fillScreen(BLACK): drawScene covers the whole canvas. If the
  // previous flush's DMA is still reading the buffer, it will see valid content (not
  // half-painted black), which was the flicker at 25 fps.
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (gameOverUntil) {
    drawGameScene();
    if (!timeLeft(gameOverUntil)) {
      gameOpen = false;
      return;
    }
    char buf[22];
    snprintf(buf, sizeof(buf), T(S_SCORE_FMT), gameScore);
    gfx->setTextColor(ink);
    setSize(TS(4));
    gfx->setCursor(centerX(buf, TS(4)), UY(160));
    gfx->print(buf);
    setSize(TS(2));
    if (gameNewHi && gameScore > 0) {
      drawRecordPill(UY(204));
    } else {
      char rec[20];
      snprintf(rec, sizeof(rec), T(S_RECORD_FMT), pet.gameHi);
      gfx->setTextColor(ink);
      gfx->setCursor(centerX(rec, TS(2)), UY(214));
      gfx->print(rec);
    }
    const char *msg = gameScore >= 10 ? T(S_GREAT_JOY) : T(S_PLUS_JOY);
    gfx->setTextColor(ink);
    gfx->setCursor(centerX(msg, TS(2)), UY(250));
    gfx->print(msg);
    gfx->flush();
    return;
  }

  drawGameScene();
  stepGame();

  // score, record and lives
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", gameScore);
  gfx->setTextColor(ink);
  setSize(TS(4));
  gfx->setCursor(centerX(buf, TS(4)), UY(30));
  gfx->print(buf);
  // text buffer sizes are guarded by TestBuffersDeTexto in test/test_tools.py
  char rec[24];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.gameHi);
  setSize(TS(2));
  gfx->setCursor(centerX(rec, TS(2)), UY(76));
  gfx->print(rec);
  for (int i = 0; i < 3; i++) {
    // three dots spaced 28 design px apart, centered under the score
    if (i < 3 - gameMisses) gfx->fillCircle(CX + U((i - 1) * 28), UY(104), U(6), UI_BAR_BAD);
    else gfx->drawCircle(CX + U((i - 1) * 28), UY(104), U(6), UI_TRACK);
  }

  if (pmd.loaded) {
    uint8_t act = (ballX > gamePetX + U(4)) ? PMD_WALKR : (ballX < gamePetX - U(4)) ? PMD_WALKL : PMD_IDLE;
    if (!pmd.has(act)) act = PMD_IDLE;
    drawPmdAct(act, (int)gamePetX, UY(394), millis(), true, false, SC(3));
  } else if (mon.loaded) {
    int s = SC((mon.h * 2 > UY(130)) ? 1 : 2);
    int w = mon.w * s, h = mon.h * s;
    uint16_t fm = mon.frameMs ? mon.frameMs : 100;
    uint16_t fi = (millis() / fm) % mon.frames;
    const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
    int px = (int)gamePetX - w / 2, py = UY(394) - h;
    for (int r = 0; r < mon.h; r++) {
      const uint8_t *row = fr + r * mon.w;
      forRuns(mon.w, [&](int c) { return indexedKey(row[c], false); },
              [&](int c0, int len, int k) { gfx->fillRect(px + c0 * s, py + r * s, len * s, s, mon.pal[k]); });
    }
  }

  // impact ring that expands and fades (soft hit feedback)
  uint32_t ht = millis() - hitTime;
  if (hitTime && ht < 260) {
    int rad = U(22 + (int)(ht / 6));
    gfx->drawCircle((int)hitX, (int)hitY, rad, C565(0xff, 0xe7, 0x9f));
    gfx->drawCircle((int)hitX, (int)hitY, rad - U(2), C565(0xff, 0xd9, 0x8a));
  }

  // the pokeball
  int bs = SC(3);  // 16x16 pokeball, centered at (ballX, ballY)
  drawMap(SPR_ICON_PLAY, 16, (int)ballX - 8 * bs, (int)ballY - 8 * bs, bs, false);

  gfx->flush();
}

// ---------- pet card (vertical swipe) ----------

// x where the card bars start: after the longest label (4 letters at scale 2) and
// 12 design px of margin, with 150 as a minimum.
// thin flat-fill bar (no gradients): track in surface color, fill in ink
static void uiBar(int x, int y, int w, int h, int val, int maxv, uint16_t fill) {
  gfx->fillRoundRect(x, y, w, h, h / 2, uiSurf());
  int fw = maxv ? (int)((int32_t)val * w / maxv) : 0;
  if (fw > w) fw = w;
  if (fw > h) gfx->fillRoundRect(x, y, fw, h, h / 2, fill);
  else if (fw > 0) gfx->fillRoundRect(x, y, h, h, h / 2, fill);
}

// card row: label, bar and value on the right
void drawCardStat(int y, const char *label, uint16_t val, uint16_t maxBar) {
  gfx->setTextColor(inkColor());
  setSize(TS(2));
  gfx->setCursor(U(92), y);
  gfx->print(label);
  char num[8];
  snprintf(num, sizeof(num), "%u", val);
  gfx->setCursor(U(388) - textW(num, TS(2)), y);
  gfx->print(num);
  uiBar(U(168), y - UY(1), U(160), UY(16), val, maxBar, inkColor());
}

// ---------- on-screen clock setting (swipe down) ----------
// The user sets their LOCAL time by eye; the firmware uses it as is, so
// there is no timezone to manage. Preserves the day (does not break streak/age).

void openClock() {
  uint32_t e = clockBase(rtcEpoch(), pet.lastSeenEpoch);  // the RTC first: the cached time lags up to 30 s
  clockH = (e / 3600) % 24;
  clockM = (e / 60) % 60;
  clockOpen = true;
}

void applyClock() {
  uint32_t e = clockEpoch(clockBase(rtcEpoch(), pet.lastSeenEpoch), clockH, clockM);
  rtcSetEpoch(e);
  pet.setClock(e);
  clockOpen = false;
}

// round step button (- / +): a sign drawn with two rectangles
void drawClockBtn(int x, int y, const char *l) {
  int cx = x + CLOCK_BTN_W / 2, cy = y + CLOCK_BTN_H / 2, r = CLOCK_BTN_W / 2, a = U(14), t = U(3);
  gfx->fillCircle(cx, cy, r, uiSurf());
  gfx->fillRect(cx - a, cy - t / 2, 2 * a + 1, t, inkColor());
  if (l[0] == '+') gfx->fillRect(cx - t / 2, cy - a, t, 2 * a + 1, inkColor());
}

// sound switch: full width, in accent when active
#define SND_PILL_Y UY(256)
#define SND_PILL_H UY(68)

void renderClock() {
  drawScreenBg();

  gfx->setTextColor(inkColor());
  setSize(TS(3));
  gfx->setCursor(centerX(T(S_SET_TIME), TS(3)), UY(40));
  gfx->print(T(S_SET_TIME));

  // hour and minutes, each above its pair of buttons
  char hh[4], mm[4];
  snprintf(hh, sizeof(hh), "%02d", clockH);
  snprintf(mm, sizeof(mm), "%02d", clockM);
  setSize(TS(7));
  int ty = UY(86);
  gfx->setCursor(CLK_X0 + CLOCK_BTN_W + U(4) - textW(hh, TS(7)) / 2, ty);
  gfx->print(hh);
  gfx->setCursor(CLK_X2 + CLOCK_BTN_W + U(4) - textW(mm, TS(7)) / 2, ty);
  gfx->print(mm);
  gfx->setCursor(CX - textW(":", TS(7)) / 2, ty);
  gfx->print(":");

  drawClockBtn(CLK_X0, CLOCK_BTN_Y, "-");  // hour -
  drawClockBtn(CLK_X1, CLOCK_BTN_Y, "+");  // hour +
  drawClockBtn(CLK_X2, CLOCK_BTN_Y, "-");  // min -
  drawClockBtn(CLK_X3, CLOCK_BTN_Y, "+");  // min +

  bool snd = audioEnabled();
  gfx->fillRoundRect(SND_PILL_X, SND_PILL_Y, SND_PILL_W, SND_PILL_H, SND_PILL_H / 2, snd ? UI_ACC : uiSurf());
  uiTextIn(SND_PILL_X, SND_PILL_Y, SND_PILL_W, SND_PILL_H, snd ? T(S_SND_ON) : T(S_SND_OFF), TS(3), snd ? RGB565_BLACK : inkColor());

  uiPill(CLOCK_CANCEL_X, CLOCK_OK_Y, CLOCK_OK_W, CLOCK_OK_H, T(S_CLOCK_CANCEL), false);
  uiPill(CLOCK_OK_X, CLOCK_OK_Y, CLOCK_OK_W, CLOCK_OK_H, "Done", true);

  // firmware version (discreet, at the very bottom)
  char ver[20];
  snprintf(ver, sizeof(ver), "v%s", FW_VERSION);
  gfx->setTextColor(uiDim());
  setSize(TS(2));
  gfx->setCursor(centerX(ver, TS(2)), UY(430));
  gfx->print(ver);

  gfx->flush();
}

void clockTap(int16_t x, int16_t y) {
  if (y >= CLOCK_BTN_Y && y <= CLOCK_BTN_Y + CLOCK_BTN_H) {  // row of +/- buttons
    if (x >= CLK_X0 && x < CLK_X0 + CLOCK_BTN_W) clockH = (clockH + 23) % 24;
    else if (x >= CLK_X1 && x < CLK_X1 + CLOCK_BTN_W) clockH = (clockH + 1) % 24;
    else if (x >= CLK_X2 && x < CLK_X2 + CLOCK_BTN_W) clockM = (clockM + 59) % 60;
    else if (x >= CLK_X3 && x < CLK_X3 + CLOCK_BTN_W) clockM = (clockM + 1) % 60;
    return;
  }
  if (y >= SND_PILL_Y && y <= SND_PILL_Y + SND_PILL_H && x >= SND_PILL_X && x < SND_PILL_X + SND_PILL_W) {  // sound switch
    audioSetEnabled(!audioEnabled());
    if (audioEnabled()) sfxPlay(SFX_TAP);    // confirms when turning on
    return;
  }
  if (y >= CLOCK_OK_Y && y <= CLOCK_OK_Y + CLOCK_OK_H) {
    if (x >= CLOCK_OK_X && x <= CLOCK_OK_X + CLOCK_OK_W) applyClock();
    else if (x >= CLOCK_CANCEL_X && x <= CLOCK_CANCEL_X + CLOCK_OK_W) clockOpen = false;
  }
}

// flame + streak number at the top left
// temporary banner: new medal or streak milestone (yellow pill at the top, over the header)
void drawCelebration() {
  const char *l1 = nullptr, *l2 = nullptr;
  char buf[32];
  if (pet.showMedal()) {
    for (int i = 0; i < MED_COUNT; i++)
      if (pet.newMedal & (1 << i)) { l2 = medalName(i); break; }
    l1 = T(S_MEDAL_BANNER);
  } else if (pet.showMilestone()) {
    snprintf(buf, sizeof(buf), T(S_STREAK_DAYS_FMT), pet.streak);
    l1 = T(S_GREAT);
    l2 = buf;
  }
  if (!l1) return;
  int w = U(240), h = UY(88), x = CX - w / 2, y = UY(34);
  gfx->fillRoundRect(x, y, w, h, h / 3, UI_ACC);
  uiTextIn(x, y + UY(8), w, UY(24), l1, TS(2), RGB565_BLACK);
  int sz = (textW(l2, TS(3)) <= w - U(24)) ? TS(3) : TS(2);
  uiTextIn(x, y + UY(40), w, UY(36), l2, sz, RGB565_BLACK);
}

// ---------- pet card: 4 pages with page dot and Back button ----------
static void drawCardNav() {
  int ds = U(12), gap = U(14), x0 = CX - ((CARD_PAGES - 1) * (ds + gap)) / 2 - ds / 2;
  for (int i = 0; i < CARD_PAGES; i++)
    gfx->fillCircle(x0 + i * (ds + gap) + ds / 2, UY(348), ds / 2, i == cardPage ? inkColor() : uiSurf());
  uiPill(CX - U(75), UY(372), U(150), UY(68), T(S_BACK), false);
}

static void drawPencil(int x, int y, int s, uint16_t col) {  // 20x20 design pencil: body + tip
  gfx->fillTriangle(x + s * 14 / 20, y, x + s, y + s * 6 / 20, x + s * 6 / 20, y + s, col);
  gfx->fillTriangle(x + s * 14 / 20, y, x, y + s * 14 / 20, x + s * 6 / 20, y + s, col);
  gfx->fillTriangle(x, y + s * 14 / 20, x + s * 6 / 20, y + s, x, y + s, col);
}

// page 0: profile (editable name, portrait, bond, streak, berry and age)
void renderCardProfile() {
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  char head[40];
  snprintf(head, sizeof(head), "%s%s", pet.shiny ? "*" : "", nm);
  // the name is a button: pill with a pencil (touching it opens the keyboard)
  int pw = U(250), ph = UY(52), px = CX - pw / 2, py = UY(34);
  gfx->fillRoundRect(px, py, pw, ph, ph / 2, uiSurf());
  setSize(TS(3));
  int hs = (textW(head, TS(3)) <= pw - U(64)) ? TS(3) : TS(2);
  int pen = U(20);
  int tw = textW(head, hs), gx = (pw - tw - pen - U(10)) / 2;
  gfx->setTextColor(inkColor());
  gfx->setCursor(px + gx, py + (ph - 7 * hs) / 2);
  gfx->print(head);
  drawPencil(px + gx + tw + U(10), py + (ph - pen) / 2, pen, uiDim());
  if (pet.nick[0]) {  // real species under the nickname
    char par[32];
    snprintf(par, sizeof(par), "(%s)", dexName(pet.speciesId));
    gfx->setTextColor(uiDim());
    setSize(TS(2));
    gfx->setCursor(centerX(par, TS(2)), UY(94));
    gfx->print(par);
  }

  // animated portrait
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, UY(226), millis(), true, false, SC(4));

  // bond
  setSize(TS(2));
  gfx->setTextColor(inkColor());
  gfx->setCursor(U(92), UY(244));
  gfx->print(T(S_VIN));
  uiBar(U(168), UY(243), U(160), UY(16), pet.bond, 100, inkColor());
  char num[8];
  snprintf(num, sizeof(num), "%u", pet.bond);
  gfx->setCursor(U(388) - textW(num, TS(2)), UY(244));
  gfx->print(num);

  // streak, with its flame
  char rl[40];
  snprintf(rl, sizeof(rl), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  int fw = U(18), sx = CX - (fw + U(10) + textW(rl, TS(2))) / 2, sy = UY(272);
  gfx->fillTriangle(sx + U(9), sy, sx + U(1), sy + UY(20), sx + U(17), sy + UY(20), uiAlert());
  gfx->fillTriangle(sx + U(9), sy + UY(8), sx + U(5), sy + UY(20), sx + U(13), sy + UY(20), UI_ACC);
  gfx->setTextColor(inkColor());
  gfx->setCursor(sx + fw + U(10), sy + UY(3));
  gfx->print(rl);

  // favorite berry and age
  const char *berry = !pet.berryKnown ? T(S_BERRY_UNK)
                      : pet.lovesBerry(0) ? T(S_BERRY_RED)
                      : pet.lovesBerry(1) ? T(S_BERRY_BLUE)
                                          : T(S_BERRY_GREEN);
  char info[48];
  snprintf(info, sizeof(info), T(S_INFO_FMT), berry, (unsigned long)(pet.ageMinutes / 1440));
  gfx->setTextColor(uiDim());
  gfx->setCursor(centerX(info, TS(2)), UY(308));
  gfx->print(info);
}

// page 1: combat (4 bars + train button)
void renderCardStats() {
  gfx->setTextColor(inkColor());
  setSize(TS(3));
  gfx->setCursor(centerX(T(S_BATTLE), TS(3)), UY(44));
  gfx->print(T(S_BATTLE));

  drawCardStat(UY(104), T(S_STAT_ATK), pet.atkStat(), 260);
  drawCardStat(UY(148), T(S_STAT_DEF), pet.defStat(), 260);
  drawCardStat(UY(192), T(S_STAT_SPE), pet.speStat(), 260);
  drawCardStat(UY(236), T(S_STAT_WGT), pet.weight, 100);

  uiPill(TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H, T(S_TRAIN_STR), true);  // strength training sack
}

// medal: circle with its mark (level in digits, or a drawing) and its name below
static void drawMedalGlyph(int i, int cx, int cy, uint16_t col) {
  int u = U(14);
  switch (i) {
    case 0: case 1: case 2: {
      char n[4];
      snprintf(n, sizeof(n), "%d", i == 0 ? 10 : i == 1 ? 25 : 50);
      uiTextIn(cx - U(30), cy - UY(12), U(60), UY(24), n, TS(3), col);
      break;
    }
    case 3: { int is = SC(2); drawMap(SPR_ICON_FOOD, 16, cx - 8 * is, cy - 8 * is, is, false); break; }
    case 4:  // flame
      gfx->fillTriangle(cx, cy - u * 3 / 2, cx - u, cy + u, cx + u, cy + u, col);
      break;
    case 5:  // heart
      gfx->fillCircle(cx - u / 2, cy - u / 3, u * 2 / 3, col);
      gfx->fillCircle(cx + u / 2, cy - u / 3, u * 2 / 3, col);
      gfx->fillTriangle(cx - u * 9 / 8, cy, cx + u * 9 / 8, cy, cx, cy + u * 3 / 2, col);
      break;
    case 6:  // star
      gfx->fillTriangle(cx, cy - u * 3 / 2, cx - u * 4 / 3, cy + u * 3 / 4, cx + u * 4 / 3, cy + u * 3 / 4, col);
      gfx->fillTriangle(cx, cy + u * 3 / 2, cx - u * 4 / 3, cy - u * 3 / 4, cx + u * 4 / 3, cy - u * 3 / 4, col);
      break;
    default:  // bolt
      gfx->fillTriangle(cx + u / 2, cy - u * 3 / 2, cx - u, cy + u / 4, cx + u / 4, cy + u / 4, col);
      gfx->fillTriangle(cx - u / 2, cy + u * 3 / 2, cx + u, cy - u / 4, cx - u / 4, cy - u / 4, col);
      break;
  }
}

// page 2: medals, 4 x 2: earned = yellow, pending = outline only
void renderCardMedals() {
  static const char *const LBL[MED_COUNT] = { "Level", "Level", "Level", "Berry", "7 days", "Bond", "Final", "Fit" };
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[24];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(inkColor());
  setSize(TS(3));
  gfx->setCursor(centerX(head, TS(3)), UY(44));
  gfx->print(head);

  int r = U(38), stepX = U(92);
  for (int i = 0; i < MED_COUNT; i++) {
    int cx = CX + ((i % 4) - 2) * stepX + stepX / 2, cy = UY(150 + (i / 4) * 112);
    bool g = pet.hasMedal(1 << i);
    if (g) gfx->fillCircle(cx, cy, r, UI_ACC);
    else for (int k = 0; k < U(3); k++) gfx->drawCircle(cx, cy, r - k, uiSurf());
    drawMedalGlyph(i, cx, cy, g ? RGB565_BLACK : uiDim());
    setSize(TS(2));
    gfx->setTextColor(g ? inkColor() : uiDim());
    gfx->setCursor(cx - textW(LBL[i], TS(2)) / 2, cy + r + UY(8));
    gfx->print(LBL[i]);
  }
}

// page 3: progress (level, evolution, neglect)
void renderCardProgress() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  gfx->setTextColor(inkColor());
  setSize(TS(3));
  gfx->setCursor(centerX(T(S_PROGRESS), TS(3)), UY(44));
  gfx->print(T(S_PROGRESS));

  char lv[10];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), pet.level());
  setSize(TS(5));
  gfx->setCursor(centerX(lv, TS(5)), UY(84));
  gfx->print(lv);

  // bar to the next level (1 level = 60 min of play)
  uint8_t into = pet.ageMinutes % MINUTES_PER_LEVEL;
  uiBar(U(103), UY(158), U(260), UY(18), into, MINUTES_PER_LEVEL, inkColor());
  char nx[32];
  snprintf(nx, sizeof(nx), T(S_NEXT_LVL_FMT), MINUTES_PER_LEVEL - into, pet.level() + 1);
  gfx->setTextColor(uiDim());
  setSize(TS(2));
  gfx->setCursor(centerX(nx, TS(2)), UY(190));
  gfx->print(nx);

  // evolution status
  char evoBuf[32];
  const char *evo;
  uint16_t evoCol = inkColor();
  if (d.evolvesTo == 0) {
    evo = T(S_FINAL_FORM);
  } else {
    int needed = d.evolveLevel + pet.careMistakes;
    if (pet.level() >= needed) {
      if (pet.lowestStat() >= 40) evo = T(S_EVO_READY);
      else { evo = T(S_EVO_BLOCKED); evoCol = uiAlert(); }
    } else {
      snprintf(evoBuf, sizeof(evoBuf), T(S_EVO_IN_FMT), needed - pet.level());
      evo = evoBuf;
    }
  }
  gfx->setTextColor(evoCol);
  gfx->setCursor(centerX(evo, TS(2)), UY(240));
  gfx->print(evo);

  // neglect (delays evolution)
  char ms[24];
  snprintf(ms, sizeof(ms), T(S_MISTAKES_FMT), pet.careMistakes);
  gfx->setTextColor(pet.careMistakes > 0 ? uiAlert() : uiDim());
  gfx->setCursor(centerX(ms, TS(2)), UY(276));
  gfx->print(ms);
}

// steps page: today's count, progress to the next daily goal, the three goals and the trail rank
void renderCardSteps() {
  gfx->setTextColor(inkColor());
  setSize(TS(3));
  gfx->setCursor(centerX(T(S_STEPS), TS(3)), UY(44));
  gfx->print(T(S_STEPS));

  char n[12];
  snprintf(n, sizeof(n), "%lu", (unsigned long)pet.stepsToday);
  setSize(TS(5));
  gfx->setCursor(centerX(n, TS(5)), UY(84));
  gfx->print(n);

  // the bar fills toward the next goal; when all are done it stays full
  uint8_t next = 0;
  while (next < Pet::STEP_GOALS && pet.stepsToday >= Pet::stepGoal(next)) next++;
  bool allDone = next == Pet::STEP_GOALS;
  uint32_t goal = Pet::stepGoal(allDone ? Pet::STEP_GOALS - 1 : next);
  uiBar(U(103), UY(158), U(260), UY(18), allDone ? goal : pet.stepsToday, goal, inkColor());
  char nx[24];
  if (allDone) snprintf(nx, sizeof(nx), "%s", T(S_STEPS_ALL_DONE));
  else snprintf(nx, sizeof(nx), T(S_STEPS_NEXT_FMT), (unsigned long)goal);
  gfx->setTextColor(uiDim());
  setSize(TS(2));
  gfx->setCursor(centerX(nx, TS(2)), UY(190));
  gfx->print(nx);

  // one dot per daily goal: filled once reached
  for (uint8_t i = 0; i < Pet::STEP_GOALS; i++) {
    int cx = CX + (i - 1) * U(80);
    bool got = pet.stepsToday >= Pet::stepGoal(i);
    gfx->fillCircle(cx, UY(228), U(14), got ? inkColor() : uiSurf());
    snprintf(n, sizeof(n), "%lu", (unsigned long)Pet::stepGoal(i));
    gfx->setTextColor(got ? inkColor() : uiDim());
    gfx->setCursor(cx - textW(n, TS(2)) / 2, UY(252));
    gfx->print(n);
  }

  char tot[24];
  snprintf(tot, sizeof(tot), T(S_STEPS_TOTAL_FMT), (unsigned long)pet.stepsTotal);
  gfx->setTextColor(uiDim());
  gfx->setCursor(centerX(tot, TS(2)), UY(284));
  gfx->print(tot);
  const char *rank = trailName(pet.trailRank());
  gfx->setTextColor(inkColor());
  gfx->setCursor(centerX(rank, TS(2)), UY(308));
  gfx->print(rank);
}

void renderCard() {
  drawScreenBg();
  if (cardPage == 0) renderCardProfile();
  else if (cardPage == 1) renderCardStats();
  else if (cardPage == 2) renderCardMedals();
  else if (cardPage == 3) renderCardProgress();
  else renderCardSteps();
  drawCardNav();
  gfx->flush();
}

// ---------- keyboard to rename ----------

static const char KB_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-";  // 28 + DEL + OK = 30
#define KB_COLS 6
// 6 columns x 5 rows, centered; the bottom row fits inside the circle
#define KB_W U(58)
#define KB_H UY(52)
#define KB_X (CX - (KB_COLS * KB_W - U(6)) / 2)  // the last column has no 6 gap on the right
#define KB_Y UY(108)

void openKeyboard() {
  kbOpen = true;
  strncpy(nameBuf, pet.nick, sizeof(nameBuf) - 1);
  nameBuf[sizeof(nameBuf) - 1] = 0;
  nameLen = strlen(nameBuf);
}

void renderKeyboard() {
  drawScreenBg();
  // the name in a pill, with the cursor in accent
  int fw = U(240), fh = UY(52), fx = CX - fw / 2, fy = UY(38);
  gfx->fillRoundRect(fx, fy, fw, fh, fh / 2, uiSurf());
  const char *shown = nameLen ? nameBuf : "";
  setSize(TS(3));
  int tw = textW(shown, TS(3)), cw = U(3);
  int tx = CX - (tw + U(6) + cw) / 2;
  gfx->setTextColor(inkColor());
  gfx->setCursor(tx, fy + (fh - 7 * TS(3)) / 2);
  gfx->print(shown);
  gfx->fillRect(tx + tw + U(6), fy + U(12), cw, fh - U(24), UI_ACC);

  for (int i = 0; i < 30; i++) {
    int x = KB_X + (i % KB_COLS) * KB_W, y = KB_Y + (i / KB_COLS) * KB_H;
    int kw = KB_W - U(6), kh = KB_H - UY(6);  // key: the cell minus 6 design px of spacing
    bool ok = (i == 29);
    gfx->fillRoundRect(x, y, kw, kh, U(14), ok ? UI_ACC : uiSurf());
    gfx->setTextColor(ok ? RGB565_BLACK : inkColor());
    if (i < 28) {
      char c[2] = { KB_KEYS[i], 0 };
      uiTextIn(x, y, kw, kh, c, TS(3), inkColor());
    } else {
      uiTextIn(x, y, kw, kh, i == 28 ? "<-" : "OK", TS(2), ok ? RGB565_BLACK : inkColor());
    }
  }
  gfx->flush();
}

void keyboardTap(int16_t x, int16_t y) {
  // filter BEFORE dividing: with x/y below KB_X/KB_Y the subtraction is negative
  // and integer division truncates toward zero (not toward -inf), so col/row
  // would come out 0 instead of negative and the filter below would not catch it
  if (x < KB_X || y < KB_Y) return;
  int col = (x - KB_X) / KB_W, row = (y - KB_Y) / KB_H;
  if (col >= KB_COLS || row >= 5) return;
  int i = row * KB_COLS + col;
  if (i >= 30) return;
  if (i == 28) {  // delete
    if (nameLen) nameBuf[--nameLen] = 0;
  } else if (i == 29) {  // OK
    pet.rename(nameBuf);
    kbOpen = false;
  } else if (nameLen < sizeof(nameBuf) - 1) {
    nameBuf[nameLen++] = KB_KEYS[i];
    nameBuf[nameLen] = 0;
  }
}

// ---------- pokedex gallery ----------

#define GAL_X U(73)
#define GAL_Y UY(84)
#define GAL_CELL U(80)    // cell width
#define GAL_CELL_Y UY(80)  // cell height

// draws a thumbnail centered in its cell; sil=true paints it in ink
void drawThumb(const uint8_t *b, int x, int y, int s, bool sil) {
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *pal = b + 3;
  const uint8_t *d = pal + n * 2;
  int ox = x + (GAL_CELL - w * s) / 2;
  int oy = y + (GAL_CELL_Y - h * s) / 2;
  for (int r = 0; r < h; r++) {
    const uint8_t *row = d + r * w;
    forRuns(w, [&](int c) { return indexedKey(row[c], sil); },
            [&](int c0, int len, int k) {
              uint16_t col = sil ? INK_K : (uint16_t)(pal[k * 2] | (pal[k * 2 + 1] << 8));
              gfx->fillRect(ox + c0 * s, oy + r * s, len * s, s, col);
            });
  }
}

void renderGallery() {
  if (galleryDetail) {  // detail view: always redrawn (animated)
    drawScreenBg();
    bool reg = pet.isRegistered(galleryDetail);   // raised: animated
    bool known = pet.isKnown(galleryDetail);      // raised or met nearby: colour and name
    char head[32];
    snprintf(head, sizeof(head), "N.%03d %s%s", galleryDetail,
             pet.isShinyRegistered(galleryDetail) ? "*" : "", known ? dexName(galleryDetail) : "???");
    gfx->setTextColor(inkColor());
    // auto-shrinks long names (they do not fit at t3), measuring instead of counting
    // bytes; 13 characters at size 3 are the 234 px from before.
    setSize(TS(3));
    bool big = textW(head, TS(3)) <= U(234);
    int gts = big ? TS(3) : TS(2);
    setSize(gts);
    gfx->setCursor(centerX(head, gts), UY(big ? 56 : 60));
    gfx->print(head);
    if (galleryPmd.loaded) {
      // animated and in color if raised; a still picture if only met nearby; a silhouette if unknown
      drawPmdActM(galleryPmd, PMD_IDLE, CX, UY(300), reg ? millis() : 0, true, !known, SC(6));
    } else {
      const uint8_t *t = thumbs.get(galleryDetail);
      if (t) drawThumb(t, CX - GAL_CELL / 2, UY(135), SC(4), !known);  // drawThumb centers in its cell
    }
    uiPill(CX - U(75), UY(372), U(150), UY(68), T(S_DETAIL_BACK), false);
    gfx->flush();
    return;
  }

  if (!galleryDirty) return;  // the grid is static
  galleryDirty = false;

  drawScreenBg();
  char head[24];
  snprintf(head, sizeof(head), T(S_POKEDEX_FMT), pet.registeredCount());
  gfx->setTextColor(inkColor());
  int hs = fitSize(head, TS(3), TS(2), UY(48));  // at 36 the edge of "POKEDEX" went outside the circle
  gfx->setCursor(centerX(head, hs), UY(48));
  gfx->print(head);

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int16_t dex = galleryPage * 16 + r * 4 + c + 1;
      if (dex > 151) break;
      int x = GAL_X + c * GAL_CELL, y = GAL_Y + r * GAL_CELL_Y;
      const uint8_t *t = thumbs.get(dex);
      if (t) {
        drawThumb(t, x, y, SC(2), !pet.isKnown(dex));
        if (pet.isRegistered(dex)) gfx->fillCircle(x + U(8), y + UY(10), U(4), inkColor());  // raised, not just met
        if (pet.isShinyRegistered(dex)) {
          gfx->setTextColor(UI_ACC);
          setSize(TS(2));
          gfx->setCursor(x + U(62), y + UY(4));
          gfx->print("*");
        }
      } else {
        char num[6];
        snprintf(num, sizeof(num), "%d", dex);
        gfx->setTextColor(uiDim());
        setSize(TS(2));
        gfx->setCursor(x + U(24), y + UY(32));
        gfx->print(num);
      }
    }
  }
  // current page
  char pg[20];
  snprintf(pg, sizeof(pg), "%d / 10", galleryPage + 1);
  gfx->setTextColor(uiDim());
  setSize(TS(2));
  gfx->setCursor(centerX(pg, TS(2)), UY(408));
  gfx->print(pg);
  gfx->flush();
}

void galleryTap(int16_t x, int16_t y) {
  if (galleryDetail) {  // back to the grid
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  if (y < HEADER_ZONE_H) {  // touching the header = exit
    galleryOpen = false;
    galleryPmd.unload();
    return;
  }
  // same reason as in the keyboard: filter before dividing
  if (x < GAL_X || y < GAL_Y) return;
  int c = (x - GAL_X) / GAL_CELL, r = (y - GAL_Y) / GAL_CELL_Y;
  if (c > 3 || r > 3) return;
  int16_t dex = galleryPage * 16 + r * 4 + c + 1;
  if (dex > 151) return;
  galleryDetail = dex;
  galleryPmd.load(dex, pet.isShinyRegistered(dex));
  if (pet.isKnown(dex)) chirpPlay(dex);  // a known species calls out
}

void drawBattery() {
  int pc = batPercent();
  if (pc < 0) return;  // no battery connected
  int x = CX - U(14), y = UY(12), w = U(24), h = UY(11);
  bool charging = batCharging();
  uint16_t col = charging ? UI_BAR_OK
                 : (pc >= 40) ? inkColor()
                 : (pc >= 15) ? UI_BAR_WARN
                              : UI_BAR_BAD;
  gfx->drawRoundRect(x, y, w, h, U(2), col);
  gfx->fillRect(x + w, y + UY(3), U(3), UY(5), col);  // terminal
  if (charging) {
    // charging bolt (zigzag) instead of the level bar
    uint16_t bolt = C565(0xff, 0xd9, 0x4a);
    int bx = x + w / 2;
    gfx->fillTriangle(bx + U(3), y + UY(1), bx - U(4), y + UY(6), bx + U(1), y + UY(6), bolt);
    gfx->fillTriangle(bx - U(1), y + UY(5), bx + U(4), y + UY(5), bx - U(3), y + UY(10), bolt);
  } else {
    int fw = (w - U(4)) * pc / 100;
    if (fw > 0) gfx->fillRect(x + U(2), y + UY(2), fw, h - UY(4), col);
  }
}

// ---------- UI: palette and common pieces ----------
// Dark at night, light by day. A single accent (yellow) for the primary or chosen;
// red only warns of a low need.
static uint16_t uiBg()    { return gNight ? RGB565_BLACK : C565(0xF3, 0xF5, 0xF4); }
static uint16_t uiSurf()  { return gNight ? C565(0x3D, 0x3A, 0x34) : C565(0xDC, 0xE0, 0xDD); }
static uint16_t uiFill()  { return gNight ? C565(0xED, 0xE7, 0xD6) : C565(0x8E, 0x96, 0x8F); }
static uint16_t uiAlert() { return gNight ? C565(0xFF, 0x5A, 0x47) : C565(0xD9, 0x3A, 0x2B); }
static uint16_t uiDim()   { return gNight ? C565(0xB4, 0xAF, 0xA2) : C565(0x52, 0x57, 0x4F); }

// text centered in a box
static void uiTextIn(int x, int y, int w, int h, const char *t, int size, uint16_t col) {
  setSize(size);
  gfx->setTextColor(col);
  gfx->setCursor(x + (w - textW(t, size)) / 2, y + (h - 7 * size) / 2);
  gfx->print(t);
}
// "new record": yellow pill over the minigame background
static void drawRecordPill(int y) {
  int w = U(220), h = UY(40);
  gfx->fillRoundRect(CX - w / 2, y, w, h, h / 2, UI_ACC);
  uiTextIn(CX - w / 2, y, w, h, T(S_NEW_RECORD), TS(2), RGB565_BLACK);
}
// button pill: primary (accent) or secondary (surface)
static void uiPill(int x, int y, int w, int h, const char *t, bool primary) {
  gfx->fillRoundRect(x, y, w, h, h / 2, primary ? UI_ACC : uiSurf());
  setSize(TS(3));
  int sz = (textW(t, TS(3)) <= w - U(32)) ? TS(3) : TS(2);
  uiTextIn(x, y, w, h, t, sz, primary ? RGB565_BLACK : inkColor());
}

// base of the bottom panel: its top edge curves upward at the sides
// (like the arc of the buttons)
static void drawPanelBase() {
  uint16_t bg = uiBg();
  gfx->fillRect(0, PANEL_Y, UI_W, UI_H - PANEL_Y, bg);
  int sag = UY(24);
  for (int x = 0; x < UI_W; x += 2) {
    float t = (float)(x + 1 - CX) / CX;
    int d = (int)(sag * t * t);
    if (d > 0) gfx->fillRect(x, PANEL_Y - d, 2, d, bg);
  }
}

// a need: circle that fills from bottom to top; below 25% the ring and fill turn red
static void drawNeed(int i, int level) {
  int cx = buttons[i].cx, cy = buttons[i].cy, r = BTN_R;
  bool off = pet.sleeping && i != 2;  // while sleeping only LIGHT works
  bool low = level < 25 && !pet.sleeping;
  if (i == 2 && pet.sleeping) {
    gfx->fillCircle(cx, cy, r, UI_ACC);
  } else if (off) {
    gfx->fillCircle(cx, cy, r, lerp565(uiSurf(), uiBg(), 1, 2));
  } else {
    gfx->fillCircle(cx, cy, r, uiSurf());
    int hh = 2 * r * level / 100;
    uint16_t fc = low ? uiAlert() : uiFill();
    for (int y = cy + r - hh; y <= cy + r; y++) {
      int dy = y - cy, hw = (int)sqrtf((float)(r * r - dy * dy));
      gfx->drawFastHLine(cx - hw, y, 2 * hw + 1, fc);
    }
    if (low)
      for (int k = 0; k < U(4); k++) gfx->drawCircle(cx, cy, r - k, uiAlert());
  }
  if (!off) {
    int is = SC(3);  // 16x16 icon at an integer scale, centered
    drawMap(buttons[i].icon, 16, cx - 8 * is, cy - 8 * is, is, false);
  }
}
void drawDeck() {
  drawNeed(0, pet.fullness);
  drawNeed(1, pet.joy);
  drawNeed(2, pet.energy);
  drawNeed(3, pet.hygiene);
}

// header: name and, below it, the level or what is happening to the pet (in red if it is a need)
void drawHeader(const char *name, const char *msg, bool alert) {
  drawBattery();
  gfx->setTextColor(inkColor());
  int hs = fitSize(name, TS(3), TS(2), UY(52));  // long names do not fit at size 3 in the upper part of the circle
  gfx->setCursor(centerX(name, hs), UY(52) + (TS(3) * 7 - textH()) / 2);
  gfx->print(name);
  gfx->setTextColor(alert ? (gNight ? C565(0xFF, 0x6B, 0x58) : C565(0xB3, 0x26, 0x1E)) : uiDim());
  setSize(TS(2));
  gfx->setCursor(centerX(msg, TS(2)), UY(90));
  gfx->print(msg);
}

// ceremony animation (10s): farewell = bow with hearts and it
// walks away; run-away = gets scared and runs off. Replaces idle.
void drawCeremony() {
  if (!pmd.loaded) { drawPet(); return; }  // fallback if there is no PMD sprite
  uint32_t now = millis();
  float t = pet.ceremonyT();               // 0..1 over the 10s
  bool panic = (pet.ceremony == CER_RUNAWAY);
  int x = CX, y = PET_GROUND;
  uint8_t act = PMD_IDLE;

  if (panic) {
    // sad ending: bluish gloom + rain
    for (int i = 0; i < 46; i++) {
      int rx = U(i * 47 + now / 3) % UI_W;
      int ry = UY(i * 91 + now / 2) % (UI_H + UY(4));
      gfx->drawLine(rx, ry, rx - U(3), ry + UY(12), C565(0x6a, 0x84, 0xb0));
    }
    bool fade = false;
    if (t < 0.30f) {                       // head down, trembling
      act = pmd.has(PMD_HURT) ? PMD_HURT : PMD_IDLE;
      x = CX + (int)(U(4) * sinf(now * 0.04f));
    } else {                               // slowly walks away and fades
      act = pmd.has(PMD_WALKL) ? PMD_WALKL : PMD_IDLE;
      x = CX - (int)(((t - 0.30f) / 0.70f) * (CX + U(120)));
      fade = (t > 0.6f) && ((now / 160) % 2 == 0);  // blinks toward the silhouette
    }
    drawPmdAct(act, x, y, now, true, fade, SC(5));  // fade=silhouette: blurs out as it leaves
    // tear falling from the pet
    if (t < 0.55f) {
      int ty = y - UY(150) + UY((int)((now / 6) % 40));
      gfx->fillRect(x + U(6), ty, SC(3), UY(6), C565(0x9a, 0xc4, 0xe8));
    }
    return;
  }

  // epic farewell: pulsing golden halo + sparks and hearts rising
  int gcy = PET_GROUND - UY(96);
  for (int k = 0; k < 4; k++) {
    int r = U(60 + k * 34) + (int)(U(10) * sinf(now * 0.02f));
    gfx->drawCircle(CX, gcy, r, C565(0xff, 0xdf, 0x8a));
  }
  for (int i = 0; i < 16; i++) {
    int px = U(i * 71 + 28) % UI_W;
    int py = UY(410) - UY((int)((now / 8 + i * 70) % 360));   // rise and reappear at the bottom
    if (py < UY(30)) continue;
    if (i % 4 == 0) drawMap(SPR_HEART, 32, px - U(8), py - UY(8), SC(1), false);  // little heart
    else gfx->fillRect(px, py, SC(4), SC(4), (i % 2) ? C565(0xff, 0xe7, 0x9f) : C565(0xff, 0x9a, 0xc0));
  }

  if (t < 0.45f) {                         // bow / farewell pose
    act = pmd.has(PMD_POSE) ? PMD_POSE : (pmd.has(PMD_NOD) ? PMD_NOD : PMD_IDLE);
  } else {                                 // walks off to the right
    act = pmd.has(PMD_WALKR) ? PMD_WALKR : PMD_IDLE;
    x = CX + (int)(((t - 0.45f) / 0.55f) * (CX + U(140)));
  }
  drawPmdAct(act, x, y, now, true, false, SC(5));
  if (pet.showHeart())                     // big heart following the pet
    drawMap(SPR_HEART, 32, x + U(50), y - UY(190), SC(2), false);
}

// full-screen decision dialog (2 stacked pills): evolve/keep or
// say goodbye/stay together. What cannot be undone stays secondary; the option to carry on is in accent.
void drawChoiceDialog() {
  const char *q, *o1, *o2;
  bool evo = (choiceKind == 1);
  if (evo) { q = T(S_EVO_Q); o1 = T(S_EVO_TAP); o2 = T(S_EVO_KEEP); }
  else     { q = T(S_FAR_Q); o1 = T(S_FAR_GO);  o2 = T(S_FAR_STAY); }
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, UY(168), millis(), true, false, SC(3));
  gfx->setTextColor(inkColor());
  uiTextIn(0, UY(180), UI_W, UY(40), q, TS(3), inkColor());
  uiPill(CHOICE_X, CHOICE_B1_Y, CHOICE_W, CHOICE_BH, o1, evo);
  uiPill(CHOICE_X, CHOICE_B2_Y, CHOICE_W, CHOICE_BH, o2, !evo);
}

// full-screen "release?" dialog: keep (accent) on top, release (red outline) below
void drawReleaseDialog() {
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, UY(160), millis(), true, false, SC(3));
  char q[28];
  snprintf(q, sizeof(q), T(S_RELEASE_FMT), dexName(pet.speciesId));
  gfx->setTextColor(inkColor());
  drawWrapped(q, TS(3), CX, UY(204), U(380), 2, false);
  uiPill(NO_X, NO_Y, CHOICE_W, CHOICE_BH, T(S_NO), true);
  for (int k = 0; k < U(3); k++)
    gfx->drawRoundRect(YES_X + k, YES_Y + k, YES_W - 2 * k, YES_H - 2 * k, (YES_H - 2 * k) / 2, uiAlert());
  uiTextIn(YES_X, YES_Y, YES_W, YES_H, T(S_YES), TS(2), uiAlert());
}

// CTA buttons: yellow pill that pulses gently (evolve, farewell) or muted (neglect)
static void drawCta(int bx, int by, int bw, int bh, int amp, float rate, bool primary, const char *t, int size, bool wrapped) {
  int p = (int)(amp * sinf(millis() * rate));
  int x = bx - p, y = by - p, w = bw + 2 * p, h = bh + 2 * p;
  gfx->fillRoundRect(x, y, w, h, h / 2, primary ? UI_ACC : uiSurf());
  gfx->setTextColor(primary ? RGB565_BLACK : inkColor());
  if (wrapped) drawWrapped(t, size, CX, y + h / 2, w - U(48), 2, false);  // with long names it goes to two lines
  else uiTextIn(x, y, w, h, t, size, primary ? RGB565_BLACK : inkColor());
}
void drawEvolveButton() { drawCta(EVO_BTN_X, EVO_BTN_Y, EVO_BTN_W, EVO_BTN_H, U(3), 0.006f, true, T(S_EVO_TAP), TS(3), false); }
void drawFarewellButton() {
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  snprintf(buf, sizeof(buf), T(S_FAREWELL_BTN), nm);
  drawCta(FAR_BTN_X, FAR_BTN_Y, FAR_BTN_W, FAR_BTN_H, U(2), 0.005f, true, buf, TS(2), true);
}
// sad ending: muted surface and slow pulse
void drawRunawayButton() {
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  snprintf(buf, sizeof(buf), T(S_RUNAWAY_BTN), nm);
  drawCta(FAR_BTN_X, FAR_BTN_Y, FAR_BTN_W, FAR_BTN_H, U(2), 0.003f, false, buf, TS(2), true);
}

// epic evolution animation: radial halo + rotating rays + accelerating
// sprite blink + sparks shooting out + final flash
void drawEvolveFX(uint32_t now) {
  float t = pet.evolveT();          // 0..1
  int cx = CX, cy = PET_GROUND - UY(96);

  // radial halo that grows and pulses
  int halo = U(36) + (int)(t * U(150)) + (int)(U(8) * sinf(now * 0.02f));
  for (int k = 0; k < 4; k++) {
    int r = halo - U(k * 7);
    if (r > 0) gfx->drawCircle(cx, cy, r, UI_WHITE);
  }
  // rotating rays from the center of the pet
  float base = now * 0.004f;
  for (int i = 0; i < 12; i++) {
    float a = base + i * (float)(PI / 6);
    int len = U(90) + (int)(U(70) * (0.5f + 0.5f * sinf(now * 0.012f + i)));
    gfx->drawLine(cx, cy, cx + (int)(cosf(a) * len), cy + (int)(sinf(a) * len), UI_WHITE);
  }
  // blink between the PREVIOUS and the NEW form (silhouettes), accelerating; at the
  // end (t>0.9) it stays fixed on the new one for the reveal flash
  int period = 60 + (int)(220 * (1.0f - t));
  bool showOld = t < 0.9f && evoPmd.loaded && ((now / period) % 2) == 0;
  if (showOld) drawPmdActM(evoPmd, PMD_IDLE, cx, PET_GROUND, 0, true, true, SC(5));
  else drawPmdAct(PMD_IDLE, cx, PET_GROUND, 0, true, true, SC(5));
  // sparks shooting out
  for (int i = 0; i < 10; i++) {
    float a = i * (float)(PI / 5) + t * 4.0f;
    int d = (int)(U(now / 14 + i * 33) % U(200));
    int sx = cx + (int)(cosf(a) * d), sy = cy + (int)(sinf(a) * d);
    int q = SC(5);  // square spark centered at (sx, sy)
    gfx->fillRect(sx - q / 2, sy - q / 2, q, q, (i & 1) ? C565(0xff, 0xe0, 0x70) : UI_WHITE);
  }
  // final flash before revealing the new form
  if (t > 0.9f) gfx->fillCircle(cx, cy, (int)(U(300) * (t - 0.9f) / 0.1f), UI_WHITE);
}

// Loads one neighbor's sprite per pass (each SD read is slow); frees slots that
// were emptied or changed pet.
void ensureNeighbors() {
  const NbEntry *sh[NB_MAX_SHOWN];
  int n = nearbyShown(sh);
  for (int i = 0; i < n; i++) pet.markSeen(sh[i]->species);  // the species was validated when the packet was decoded
  for (int i = 0; i < NB_MAX_SHOWN; i++) {
    NbSlot want = i < n ? NbSlot{ sh[i]->id, sh[i]->species, sh[i]->shiny } : NbSlot{ 0, 0, false };
    NbSlot &have = nbSlot[i];
    if (want.id == have.id && want.species == have.species && want.shiny == have.shiny) continue;
    if (want.id) {  // at most one load per second: a stranger who makes up ids does not saturate the SD
      static uint32_t lastLoad = 0;
      uint32_t now = millis();
      if (!sdReady || (lastLoad && now - lastLoad < 1000)) continue;
      lastLoad = now;
    }
    nbPmd[i].unload();
    have = want;
    if (want.id) {
      nbPmd[i].load(want.species, want.shiny);
      return;  // one per pass
    }
  }
}

// Neighbors stay still behind and beside the pet, smaller and higher up, like
// a group: two in front (at the sides) and two at the back (between them, higher and smaller).
void drawNeighbors() {
  const NbEntry *sh[NB_MAX_SHOWN];
  int n = nearbyShown(sh);
  struct Pos { int16_t dx, up; uint8_t s; };
  const Pos pos[NB_MAX_SHOWN] = { { -135, 14, 3 }, { 135, 14, 3 }, { -70, 34, 2 }, { 70, 34, 2 } };
  const uint8_t order[NB_MAX_SHOWN] = { 2, 3, 0, 1 };  // back ones first
  uint32_t now = millis();
  for (uint8_t i : order) {
    if (i >= n) continue;
    PmdMon &m = nbPmd[i];
    if (!m.loaded || nbSlot[i].id != sh[i]->id) continue;
    uint8_t act = sh[i]->sleeping && m.has(PMD_SLEEP) ? PMD_SLEEP : PMD_IDLE;
    uint32_t t = now + (sh[i]->id % 997) * 13;  // each at its own moment of the animation
    drawPmdActM(m, act, CX + U(pos[i].dx), PET_GROUND - UY(pos[i].up), t, true, false, SC(pos[i].s));
  }
}

void drawPet() {
  if (pmd.loaded) {
    drawPetPMD();
    return;
  }
  if (mon.loaded) {
    drawPetSD();
    return;
  }
  int fi = flashIdxForDex(pet.speciesId);
  if (fi < 0) {
    // no SD and no flash sprite: clear notice that sprites are missing
    gfx->setTextColor(inkColor());
    setSize(TS(6));
    gfx->setCursor(CX - 3 * TS(6), PET_CY - UY(80));  // "?" is 6 px wide per scale: half is 3 * scale
    gfx->print("?");
    setSize(TS(2));
    const char *l1 = T(S_NO_SPRITES);
    gfx->setCursor(centerX(l1, TS(2)), PET_CY - UY(4));
    gfx->print(l1);
    const char *l2 = T(S_LOAD_SPRITES);
    gfx->setCursor(centerX(l2, TS(2)), PET_CY + UY(20));
    gfx->print(l2);
    return;
  }
  const Species &sp = SPECIES[fi];
  int s = SC(sp.scale);
  int x = CX - 16 * s;
  int y = PET_CY - 16 * s;

  // evolution animation: alternates the silhouette of the previous and the new form
  if (pet.evolving()) {
    bool flash = (millis() / 300) % 2;
    int16_t showDex = (flash && pet.prevSpeciesId >= 0) ? pet.prevSpeciesId : pet.speciesId;
    int sfi = flashIdxForDex(showDex);
    if (sfi >= 0) {
      const Species &show = SPECIES[sfi];
      int ss = SC(show.scale);
      drawMap(show.sprite, SPRITE_H, CX - 16 * ss, PET_CY - 16 * ss, ss, flash);
    }
    return;
  }

  PetMood m = pet.mood();
  if (m == MOOD_HAPPY && (millis() / 500) % 2) y -= UY(6);  // little hop

  drawMap(sp.sprite, SPRITE_H, x, y, s, false);

  // overlaid expressions using the species anchors
  bool blink = (millis() % 3500 < 300);
  if (m == MOOD_SLEEPING || blink) {
    overlayEye(sp, x, y, s, sp.eyeColL);
    overlayEye(sp, x, y, s, sp.eyeColR);
  }
  if (m == MOOD_EATING) overlayMouth(sp, x, y, s, true);
  else if (m == MOOD_SAD) overlayMouth(sp, x, y, s, false);

  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + 20 * s, y - 2 * s, SC(2), false);
}

// ---------- bath scene ----------

void startBath() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || bathUntil) return;
  bathUntil = millis() + 3000;
  bathPending = true;
  int cx = (int)beh.x;
  for (auto &b : bubbles) {
    b.x = cx - U(70) + random(U(140));
    b.y = PET_GROUND - random(UY(150));
    b.r = U(8) + random(U(16));
    b.ph = random(64);
  }
}

void drawBath() {
  uint32_t now = millis();
  if (!timeLeft(bathUntil)) {
    bathUntil = 0;
    if (bathPending) {
      bathPending = false;
      pet.clean();
      // joy pose on getting clean
      if (pmd.has(PMD_POSE)) {
        beh.mode = 2;
        beh.act = PMD_POSE;
        beh.t0 = now;
        beh.until = now + pmdActTotalMs(pmd.acts[PMD_POSE]) * 2;
      }
    }
    return;
  }
  uint32_t left = bathUntil - now;
  if (left > 800) {
    // foam: bubbles swaying and slowly rising
    float t = now / 220.0f;
    for (auto &b : bubbles) {
      int bx = b.x + (int)(sinf(t + b.ph) * U(6));
      int by = b.y - UY((int)((3000 - left) / 90));
      gfx->fillCircle(bx, by, b.r, UI_WHITE);
      gfx->drawCircle(bx, by, b.r, 0x7E3D);
      gfx->fillCircle(bx - b.r / 3, by - b.r / 3, b.r / 4, UI_BG_DAY);
    }
  } else {
    // the bubbles pop: sparkles
    for (int i = 0; i < 8; i++) {
      auto &b = bubbles[i];
      int sx = b.x + U((i % 3) * 6 - 6), sy = b.y - UY(18);
      uint16_t col = (i % 2) ? UI_BAR_WARN : UI_WHITE;
      int th = SC(3), len = U(13);  // cross centered at (sx, sy)
      gfx->fillRect(sx - len / 2, sy - th / 2, len, th, col);
      gfx->fillRect(sx - th / 2, sy - len / 2, th, len, col);
    }
  }
}

// ---------- PMD pet: behavior ----------

uint32_t pmdActTotalMs(const PmdAct &a) {
  uint32_t t = 0;
  for (uint8_t i = 0; i < a.frames; i++) t += a.ms[i];
  return t ? t : 100;
}

uint8_t pmdFrameAt(const PmdAct &a, uint32_t t, bool loop) {
  uint32_t total = pmdActTotalMs(a);
  if (!loop && t >= total) return a.frames - 1;
  t %= total;
  uint8_t i = 0;
  // guard: never more loops than frames, in case a corrupt .bin brings ms[]=0
  // despite the sanitizing on load (defense in depth)
  for (uint8_t guard = 0; guard < a.frames && t >= a.ms[i]; guard++) {
    t -= a.ms[i];
    i = (i + 1) % a.frames;
  }
  return i;
}

// draws an action anchored at its base (center-x, ground) and returns its scale
// draws an action of a specific PmdMon (m); drawPmdAct uses the global pmd
void drawPmdActM(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  const PmdAct &a = m.acts[actId];
  if (!a.frames) return;
  int sBase = m.acts[PMD_IDLE].h ? UY(170) / m.acts[PMD_IDLE].h : SC(5);
  if (sBase < SC(2)) sBase = SC(2);
  if (sBase > maxS) sBase = maxS;
  int s = sBase;
  while (s > SC(2) && a.h * s > UY(250)) s--;  // actions with a large frame (attack)
  uint8_t fi = pmdFrameAt(a, t, loop);
  const uint8_t *fr = a.data + (uint32_t)fi * a.w * a.h;
  // anchor by the feet (a.base), not by the canvas height: this way actions
  // with different padding (Hurt, Eat...) all end up at the same ground height
  int x0 = cx - a.w * s / 2, y0 = groundY - (a.base ? a.base : a.h) * s;
  for (int r = 0; r < a.h; r++) {
    const uint8_t *row = fr + r * a.w;
    forRuns(a.w, [&](int c) { return indexedKey(row[c], sil); },
            [&](int c0, int len, int k) { gfx->fillRect(x0 + c0 * s, y0 + r * s, len * s, s, sil ? INK_K : m.pal[k]); });
  }
}
void drawPmdAct(uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  drawPmdActM(pmd, actId, cx, groundY, t, loop, sil, maxS);
}

// picks the pet's next whim when it is content
void behNext() {
  uint32_t now = millis();
  beh.t0 = now;
  int r = random(100);
  if (r < 35 && (pmd.has(PMD_WALKL) || pmd.has(PMD_WALKR))) {
    beh.mode = 1;  // walk
    beh.targetX = U(150) + random(U(176));
    beh.until = now + 15000;
  } else if (r < 60) {
    // random gesture among the available ones
    // (Hop left out: jumps too high; Sit left out: looks backward)
    static const uint8_t flair[] = { PMD_POSE, PMD_NOD, PMD_BREATH };
    uint8_t pick[3], n = 0;
    for (uint8_t f : flair)
      if (pmd.has(f)) pick[n++] = f;
    if (n) {
      beh.mode = 2;
      beh.act = pick[random(n)];
      beh.until = now + pmdActTotalMs(pmd.acts[beh.act]);
      return;
    }
    beh.mode = 0;
    beh.until = now + 2000 + random(3000);
  } else {
    beh.mode = 0;  // look straight ahead
    beh.until = now + 2000 + random(3000);
  }
}

void drawPetPMD() {
  uint32_t now = millis();

  if (pet.evolving()) {
    drawEvolveFX(now);
    return;
  }
  if (evoPmd.loaded) evoPmd.unload();  // evolution finished: frees the previous form

  PetMood m = pet.mood();
  uint8_t act;
  bool loop = true;
  if (m == MOOD_SLEEPING && pmd.has(PMD_SLEEP)) {
    act = PMD_SLEEP;
    beh.mode = 0;
  } else if (m == MOOD_EATING && pmd.has(PMD_EAT)) {
    act = PMD_EAT;
    beh.t0 = 0;
  } else if (m == MOOD_SAD && pmd.has(PMD_HURT)) {
    act = PMD_HURT;
  } else {
    // content: the scheduler decides (idle / walk / gesture)
    if (now > beh.until) behNext();
    if (beh.mode != 1) walkLast = 0;  // when walking again, the time spent standing is not counted
    if (beh.mode == 1) {
      float d = beh.targetX - beh.x;
      if (fabsf(d) < 4.0f * UI_F) {
        behNext();
        act = PMD_IDLE;
      } else {
        // 30 design px/s (3 px per frame at 10 fps), measuring real time: with fewer frames
        // per second it walks just as fast
        uint32_t dt = walkLast ? now - walkLast : 100;
        uint32_t cap = frameIntervalMs(powerCtx(now)) + 100;  // a larger gap (another screen in between) is not a long step
        if (dt > cap) dt = cap;
        walkLast = now;
        float step = 30.0f * UI_F * dt / 1000.0f;
        if (step > fabsf(d)) step = fabsf(d);  // a slow frame does not overshoot the destination
        beh.x += d > 0 ? step : -step;
        act = (d > 0) ? PMD_WALKR : PMD_WALKL;
      }
    } else {
      act = (beh.mode == 2) ? beh.act : PMD_IDLE;
      loop = false;
    }
    if (!pmd.has(act)) act = PMD_IDLE;
  }

  drawPmdAct(act, (int)beh.x, PET_GROUND, now - beh.t0, loop || act == PMD_IDLE, false, SC(5));

  if (pet.showHeart()) drawMap(SPR_HEART, 32, (int)beh.x + U(50), PET_GROUND - UY(190), SC(2), false);
}

// animated sprite from the SD: integer zoom per pixel, frames at their own pace
void drawPetSD() {
  int s = UY(200) / mon.h;  // as on load (200 / height), but with the scene scaled
  if (s < SC(2)) s = SC(2);
  if (s > SC(5)) s = SC(5);
  int w = mon.w * s, h = mon.h * s;
  int x = CX - w / 2;
  int y = PET_CY - h / 2;

  bool sil = false;
  if (pet.evolving()) {
    sil = (millis() / 300) % 2;
  } else if (pet.mood() == MOOD_HAPPY && (millis() / 500) % 2) {
    y -= UY(6);  // little hop
  }

  uint16_t fm = mon.frameMs ? mon.frameMs : 100;
  uint16_t fi = pet.sleeping ? 0 : (millis() / fm) % mon.frames;
  const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
  for (int r = 0; r < mon.h; r++) {
    const uint8_t *row = fr + r * mon.w;
    forRuns(mon.w, [&](int c) { return indexedKey(row[c], sil); },
            [&](int c0, int len, int k) { gfx->fillRect(x + c0 * s, y + r * s, len * s, s, sil ? INK_K : mon.pal[k]); });
  }

  // emotes instead of expressions (imported sprites have no anchors)
  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + w - U(30), y - UY(50), SC(2), false);
}

// closed eye: erases the 3x4 eye and draws the eyelid
void overlayEye(const Species &sp, int x, int y, int s, int col) {
  gfx->fillRect(x + col * s, y + sp.eyeRow * s, 3 * s, 4 * s, sp.bodyColor);
  gfx->fillRect(x + col * s, y + (sp.eyeRow + 2) * s, 3 * s, s, INK_K);
}

// erases the base smile and paints an open mouth (eating) or a frown (sad)
void overlayMouth(const Species &sp, int x, int y, int s, bool open) {
  int mc = sp.mouthCol, mr = sp.mouthRow;
  gfx->fillRect(x + (mc - 3) * s, y + mr * s, 7 * s, 2 * s, sp.bodyColor);
  if (open) {
    gfx->fillRect(x + (mc - 2) * s, y + mr * s, 5 * s, 2 * s, INK_K);
  } else {
    gfx->fillRect(x + (mc - 2) * s, y + mr * s, 5 * s, s, INK_K);
    gfx->fillRect(x + (mc - 3) * s, y + (mr + 1) * s, s, s, INK_K);
    gfx->fillRect(x + (mc + 3) * s, y + (mr + 1) * s, s, s, INK_K);
  }
}

void drawPoops() {
  for (int i = 0; i < pet.poops; i++) {
    drawMap(SPR_POOP, 32, U(36 + i * 46), UY(244), SC(2), false);
  }
}

const char *eggMsg() {
  switch (pet.eggCracks()) {
    case 0: return T(S_EGG_TOUCH);
    case 1: return T(S_EGG_MOVES);
    default: return T(S_EGG_ALMOST);
  }
}

const char *statusMsg() {
  if (pet.evolving()) return T(S_EVOLVING);
  if (pet.stepRewardShown() >= 0) return T((StrId)(S_STEP_REWARD_1 + pet.stepRewardShown()));
  if (bathUntil) return "Splish splash!";  // universal onomatopoeia
  if (pet.sleeping) return "Zzz...";
  if (pet.eating()) return T(S_EATING);
  if (pet.showHeart()) return T(S_LIKES);
  if (pet.fullness < 25) return T(S_HUNGRY);
  if (pet.hygiene < 25) return T(S_NEEDS_BATH);
  if (pet.energy < 25) return T(S_EXHAUSTED);
  if (pet.joy < 25) return T(S_SAD);
  if (pet.weight > 60) return T(S_CHUBBY);
  if (pet.shiny && pet.ageMinutes < 15) return T(S_IS_SHINY);
  return T(S_HAPPY);
}

// draws an n x n pixel map, scaled; silhouette=true paints it in ink
void drawMap(const char *const *map, int n, int x, int y, int s, bool silhouette) {
  for (int r = 0; r < n; r++) {
    const char *row = map[r];
    forRuns(n, [&](int c) { return charKey(row[c], silhouette); },
            [&](int c0, int len, int k) { gfx->fillRect(x + c0 * s, y + r * s, len * s, s, silhouette ? INK_K : spriteColor((char)k)); });
  }
}
