#include "audio.h"
#include "species_chirp.h"
#include "dex.h"
#include "pin_config.h"
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// TamaPoke audio: ES8311 codec (DAC -> PA amplifier -> speaker) over I2S.
// ES8311 init ported from Espressif's official driver (esp-bsp), fixed at
// MCLK=4.096MHz (256*fs), 16kHz, 16-bit, I2S slave. Effects are square-wave
// tones (Game Boy style) synthesized in a separate task so as not to
// block the game loop.
// ---------------------------------------------------------------------------

#define ES8311_ADDR 0x18
#define SAMPLE_RATE 16000

static I2SClass i2s;
static bool gReady = false;
static bool gOn = true;
static bool gSleeping = false;
static bool gIdle = false;          // no recent interaction (see power.h): the amplifier draws power at idle
static uint32_t gAmpHoldUntil = 0;  // an effect requested while idle keeps the amplifier on until here
static bool gAmpHold = false;       // (separate flag: so the comparison does not break when millis() wraps)
static uint32_t gAmpOnAt = 0;       // when the amplifier was turned on
static QueueHandle_t gQ = nullptr;  // uint16_t: an Sfx id, or CHIRP_BASE + dex number
#define CHIRP_BASE 0x100

// The NS4150B takes much longer than 8 ms to settle after each power-down, so
// turning it on right before each effect swallowed the short ones: the startup
// jingle (440 ms) was heard and the 35 ms beeps were not. It stays on while
// there is sound and the pet is awake, as in Waveshare's verified example,
// and is only turned off on sleep or mute.
static void updateAmplifierPower() {
  // gReady avoids turning it on on a board where the codec did not start: there the
  // amplifier would only add hiss, since nothing will play.
  if (gAmpHold && (int32_t)(gAmpHoldUntil - millis()) <= 0) gAmpHold = false;
  bool idleOff = gIdle && !gAmpHold;
  static int last = -1;  // the pin is only written when it changes
  int want = (gReady && gOn && !gSleeping && !idleOff) ? HIGH : LOW;
  if (want != last) {
    last = want;
    if (want == HIGH) gAmpOnAt = millis();
    digitalWrite(PA, want);
  }
}

// ---- codec I2C ----
static bool esW(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static uint8_t esR(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ES8311_ADDR, 1);
  return Wire.available() ? Wire.read() : 0;
}

// Init sequence VERIFIED ON THIS BOARD (PlaneRadar2.0 project, same
// Waveshare 1.75). Key: clock DERIVED FROM BCLK (reg01=0xBF, no external MCLK)
// and the internal reference that feeds the DAC (reg44=0x58); without those two the codec
// answered over I2C but no audio came out. 16kHz, 16-bit, I2S slave.
static bool es8311Init() {
  Wire.beginTransmission(ES8311_ADDR);
  if (Wire.endTransmission() != 0) return false;

  // open()
  esW(0x0D, 0xFA); esW(0x44, 0x08); esW(0x44, 0x08);  // power up + first-write quirk
  esW(0x01, 0x30); esW(0x02, 0x00); esW(0x03, 0x10); esW(0x16, 0x24);
  esW(0x04, 0x10); esW(0x05, 0x00); esW(0x0B, 0x00); esW(0x0C, 0x00);
  esW(0x10, 0x1F); esW(0x11, 0x7F);
  esW(0x00, 0x80); esW(0x00, 0x80);                   // reset clock, slave
  esW(0x01, 0xBF);                                    // clk src = BCLK (no external MCLK)
  { uint8_t r = esR(0x06); r &= ~0x20; esW(0x06, r); }  // SCLK not inverted
  esW(0x13, 0x10); esW(0x1B, 0x0A); esW(0x1C, 0x6A);
  esW(0x44, 0x58);                                    // internal reference -> feeds the DAC

  // config_sample(): BCLK*8 = DIG_MCLK
  esW(0x02, 0x18); esW(0x05, 0x00); esW(0x03, 0x10); esW(0x04, 0x20);
  { uint8_t r = esR(0x07); r &= 0xC0; esW(0x07, r); }
  esW(0x08, 0xFF);
  { uint8_t r = esR(0x06); r &= 0xE0; r |= 0x03; esW(0x06, r); }  // bclk_div=4

  // I2S 16-bit format
  esW(0x09, 0x0C); esW(0x0A, 0x0C);

  // start() DAC slave
  esW(0x00, 0x80); esW(0x01, 0xBF); esW(0x09, 0x0C); esW(0x0A, 0x0C);
  esW(0x17, 0xBF); esW(0x0E, 0x02); esW(0x12, 0x00); esW(0x14, 0x1A);
  esW(0x0D, 0x01); esW(0x15, 0x40); esW(0x37, 0x08); esW(0x45, 0x00);

  // volume + unmute
  esW(0x32, 0xBF);                                    // DAC volume ~0 dB
  { uint8_t r = esR(0x31); r &= 0x9F; esW(0x31, r); }  // unmute
  return true;
}

// ---- square-wave tone synthesizer ----
struct Note { uint16_t f, ms; };

static const Note N_TAP[]    = {{880, 35}};
static const Note N_EAT[]    = {{660, 45}, {0, 12}, {660, 45}};
static const Note N_PLAY[]   = {{784, 45}, {988, 60}};
static const Note N_HEART[]  = {{1047, 55}, {1319, 90}};
static const Note N_HATCH[]  = {{523, 80}, {659, 80}, {784, 110}, {1047, 170}};
static const Note N_EVOLVE[] = {{523, 80}, {659, 80}, {784, 80}, {1047, 90}, {1319, 230}};
static const Note N_MEDAL[]  = {{784, 70}, {0, 25}, {784, 70}, {0, 25}, {1047, 200}};
static const Note N_DENY[]   = {{300, 110}, {200, 170}};
static const Note N_BYE[]    = {{784, 150}, {659, 150}, {523, 280}};
static const Note N_LEVEL[]  = {{784, 70}, {1047, 130}};

struct SfxDef { const Note *n; uint8_t len; };
static const SfxDef SFX[SFX_COUNT] = {
  {N_TAP, 1}, {N_EAT, 3}, {N_PLAY, 2}, {N_HEART, 2}, {N_HATCH, 4},
  {N_EVOLVE, 5}, {N_MEDAL, 5}, {N_DENY, 2}, {N_BYE, 3}, {N_LEVEL, 2},
};

static int16_t buf[256 * 2];  // interleaved stereo (L=R)

// plays a tone (or silence if f==0) with an anti-click attack/decay ramp
static void playTone(uint16_t f, uint16_t ms) {
  int total = SAMPLE_RATE * ms / 1000;
  int half = f ? (SAMPLE_RATE / (2 * f)) : 0;  // half period in samples
  const int16_t amp = 5000;
  int phase = 0, done = 0;
  bool high = true;
  while (done < total) {
    int n = total - done; if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
      int16_t s = 0;
      if (f) {
        s = high ? amp : -amp;
        int idx = done + i;
        if (idx < 64) s = (int16_t)(s * idx / 64);                 // attack
        else if (idx > total - 96) s = (int16_t)(s * (total - idx) / 96);  // decay
        if (++phase >= half) { phase = 0; high = !high; }
      }
      buf[i * 2] = s; buf[i * 2 + 1] = s;
    }
    i2s.write((uint8_t *)buf, n * 4);
    done += n;
  }
}

// a chirp note: the pitch glides by `slide` Hz over its length; phase accumulator in 16.16
static void playChirpNote(const ChirpNote &n) {
  int total = SAMPLE_RATE * n.ms / 1000;
  const int amp = n.wave == CHIRP_TRI ? 6000 : 4000;  // a triangle is quieter at the same height
  uint32_t phase = 0;
  int done = 0;
  while (done < total) {
    int len = total - done; if (len > 256) len = 256;
    for (int i = 0; i < len; i++) {
      int idx = done + i;
      int hz = n.freq + n.slide * idx / total;
      phase += (uint32_t)(hz < 100 ? 100 : hz) * 65536 / SAMPLE_RATE;
      int p = phase & 0xFFFF;
      int s = n.wave == CHIRP_TRI ? (p < 32768 ? p * 2 - 32768 : 98303 - p * 2) * amp / 32768
                                  : (p < 32768 ? amp : -amp);
      if (idx < 64) s = s * idx / 64;                          // attack
      else if (idx > total - 96) s = s * (total - idx) / 96;   // decay
      buf[i * 2] = buf[i * 2 + 1] = (int16_t)s;
    }
    i2s.write((uint8_t *)buf, len * 4);
    done += len;
  }
}

static void audioTask(void *) {
  uint16_t id;
  for (;;) {
    if (xQueueReceive(gQ, &id, portMAX_DELAY) && gOn && !gSleeping && gReady) {
      bool isChirp = id >= CHIRP_BASE;
      SpeciesChirp chirp;
      if (isChirp ? !speciesChirp(id - CHIRP_BASE, &chirp) : id >= SFX_COUNT) continue;
      // the NS4150B takes over 8 ms to settle: if it was just turned on, the effect waits
      int32_t since = (int32_t)(millis() - gAmpOnAt);
      if (since >= 0 && since < 50) vTaskDelay(pdMS_TO_TICKS(50 - since));
      if (isChirp) {
        for (const ChirpNote &n : chirp.notes) playChirpNote(n);
      } else {
        const SfxDef &d = SFX[id];
        for (uint8_t i = 0; i < d.len; i++) playTone(d.n[i].f, d.n[i].ms);
      }
    }
  }
}

void audioBegin() {
  // I2S first: it starts the MCLK the codec needs to lock on
  pinMode(PA, OUTPUT);
  digitalWrite(PA, LOW);   // until we know whether sound is enabled

  i2s.setPins(I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO, I2S_DI_IO, I2S_MCK_IO);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("I2S begin fallo");
    return;
  }
  if (!es8311Init()) { Serial.println("ES8311 no responde (audio off)"); return; }

  Preferences p;
  p.begin("tamapoke", true);
  gOn = p.getBool("snd", true);
  p.end();

  gReady = true;
  updateAmplifierPower();
  gQ = xQueueCreate(8, sizeof(uint16_t));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 1, nullptr, 0);
  sfxPlay(SFX_HATCH);  // startup jingle (confirms that it plays)
}

bool audioBusy() { return gAmpHold && (int32_t)(gAmpHoldUntil - millis()) > 0; }

static bool enqueue(uint16_t id) {
  if (gReady && gOn && gQ) {
    if (gIdle) {  // an alert while idle (e.g. "ready to evolve") has to be heard
      gAmpHoldUntil = millis() + 4000;
      gAmpHold = true;
      updateAmplifierPower();
    }
    return xQueueSend(gQ, &id, 0) == pdTRUE;  // dropped if the queue is full
  }
  return false;
}

void sfxPlay(uint8_t id) { enqueue(id); }

void chirpPlay(int16_t dex) {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (dex < 1 || dex > DEX_COUNT || (last && now - last < 1500)) return;  // tapping must not babble
  if (enqueue(CHIRP_BASE + dex)) last = now;  // only a call that was queued uses up the pause
}

void audioSetEnabled(bool on) {
  gOn = on;
  updateAmplifierPower();
  Preferences p;
  p.begin("tamapoke", false);
  p.putBool("snd", on);
  p.end();
}
bool audioEnabled() { return gOn; }

void audioSetSleeping(bool sleeping) {
  if (gSleeping == sleeping) return;
  gSleeping = sleeping;
  updateAmplifierPower();  // no effects while asleep: amp off, no power draw
}

void audioSetIdle(bool idle) {
  gIdle = idle;
  updateAmplifierPower();  // also when an alert's hold expires
}
