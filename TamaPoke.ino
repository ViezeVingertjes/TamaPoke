// TamaPoke - tamagotchi pixel art inspirado en la gen 1
// para Waveshare ESP32-S3-Touch-AMOLED-1.75
//
// Librerias (Library Manager o repo de Waveshare):
//   - "GFX Library for Arduino" (moononournation), con soporte CO5300 QSPI
//   - "SensorLib" (Lewis He), driver tactil CST9217
//
// Placa: ESP32S3 Dev Module | Flash 16MB | PSRAM: OPI PSRAM | USB CDC On Boot: Enabled
//
// Los sprites y la tabla de especies se generan con tools/sprites.py (emit).

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
#include "i18n.h"
#include "audio.h"
#include "power.h"
#include "nearby_radio.h"
#include "blit.h"
#include "layout.h"

// Version del firmware. Subir este numero en cada release (y manifest.json para
// el instalador web). Se muestra en la pantalla de ajustes y por serie al arrancar.
#define FW_VERSION "1.17"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RESET, 0 /*rotation*/, LCD_WIDTH, LCD_HEIGHT, LCD_OFF_X, LCD_OFF_Y, 0, 0);
// Framebuffer completo en PSRAM: dibujamos todo y hacemos flush() (sin parpadeo)
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);

TouchDrvCST92xx touch;
#define TOUCH_ADDR 0x5A  // CST9217
Pet pet;

// sprite animado de la SD para la especie actual (si existe el archivo)
SdMon mon;          // sprite B/N (respaldo y minijuego si no hay PMD)
PmdMon pmd;         // sprite PMD multi-accion (pantalla principal)
// vecinos (TamaPokes cercanos): un sprite por hueco, cargado de la SD de uno en uno
struct NbSlot { uint32_t id; uint8_t species; bool shiny; };
PmdMon nbPmd[NB_MAX_SHOWN];
NbSlot nbSlot[NB_MAX_SHOWN];
PmdMon evoPmd;      // forma anterior, solo durante el parpadeo de evolucion
int16_t monFor = -2;
bool monShinyFor = false;

// comportamiento del bicho en pantalla
struct {
  uint8_t mode = 0;     // 0 idle, 1 paseo, 2 gesto one-shot
  uint8_t act = PMD_IDLE;
  uint32_t t0 = 0;      // inicio de la animacion en curso
  uint32_t until = 0;   // fin del estado actual
  float x = CX, targetX = CX;
} beh;
uint32_t walkLast = 0;  // instante del ultimo paso del paseo (0 = ninguno aun)
#define PET_GROUND UY(304)  // linea de suelo de la mascota
PmdMon galleryPmd;  // sprite grande de la vista detalle de la galeria (PMD/TPK2, legal)

// galeria pokedex
bool galleryOpen = false;
bool galleryDirty = false;
int galleryPage = 0;        // 10 paginas de 16
int16_t galleryDetail = 0;  // dex en vista detalle, 0 = rejilla

bool screenOff = false;       // pulsacion corta del boton PWR
bool cardOpen = false;        // ficha del bicho (deslizar vertical)
bool kbOpen = false;          // teclado para renombrar al bicho
char nameBuf[12] = "";
uint8_t nameLen = 0;
uint8_t cardPage = 0;         // 0 perfil, 1 stats+medallas
bool clockOpen = false;       // pantalla de ajuste de hora (deslizar abajo)
int clockH = 12, clockM = 0;  // hora en edicion

// escena de bano: espuma sobre el bicho y limpieza al reventar
uint32_t bathUntil = 0;
bool bathPending = false;
struct { int16_t x, y; uint8_t r, ph; } bubbles[14];
uint32_t feedMenuUntil = 0;   // selector de comida abierto hasta este millis

// minijuego "toques": mantener la pokeball en el aire
bool gameOpen = false;
uint32_t gameOverUntil = 0;
float ballX, ballY, ballVX, ballVY, gamePetX;
uint32_t lastGameStep = 0;  // ultima llamada a stepGame(): fisica por tiempo real, no por frame
uint8_t gameScore, gameMisses;
float hitX, hitY;             // ultimo golpe (anillo de impacto)
uint32_t hitTime = 0;
bool gameNewHi = false;

// saco de entrenamiento (entrena la fuerza)
bool sackOpen = false;
uint32_t sackUntil = 0, sackOverUntil = 0;
uint16_t sackHits = 0;
float sackShake = 0;
uint8_t sackGain = 0;
bool sackNewHi = false;

// las 9 especies con sprite propio en flash (respaldo sin SD): dex -> indice
int flashIdxForDex(int16_t dex) {
  static const int8_t IDX[10] = { -1, 3, 4, 5, 0, 1, 2, 6, 7, 8 };
  return (dex >= 1 && dex <= 9) ? IDX[dex] : -1;
}

#define PET_CY UY(202)  // centro vertical del sprite

static const uint16_t INK_K = 0x18C4;  // spriteColor('k')

// botones de icono siguiendo el arco inferior de la pantalla redonda
// (los exteriores van mas altos para no salirse del circulo)
struct Btn {
  int16_t cx, cy;
  const char *const *icon;
};
Btn buttons[4] = {
  { U(140), UY(390), SPR_ICON_FOOD },   // comer
  { U(202), UY(404), SPR_ICON_PLAY },   // jugar
  { U(264), UY(404), SPR_ICON_LIGHT },  // luz
  { U(326), UY(390), SPR_ICON_CLEAN },  // bano
};
#define BTN_HALF U(26)  // boton de 52x52
#define BTN_HIT U(36)   // radio tactil (un poco mas generoso)

// grietas del huevo (pixeles 'k' sobre el sprite)
static const uint8_t CRACK1[][2] = { {15,8},{16,9},{15,10} };
static const uint8_t CRACK2[][2] = { {11,13},{12,14},{11,15},{20,12},{19,13},{20,14} };
// estrellas del modo noche
static const uint16_t STARS[][2] = { {120,140},{330,120},{370,210},{95,230},{280,90},{160,95} };

bool wasPressed = false;
// eleccion de inicial (primera partida): Bulbasaur / Charmander / Squirtle, 3 filas
static const int16_t STARTER_DEX[3] = { 1, 4, 7 };
#define STARTER_ROW_X U(70)
#define STARTER_ROW_W U(326)
#define STARTER_ROW_Y UY(110)
#define STARTER_ROW_H UY(70)
#define STARTER_ROW_GAP UY(8)
// boton-CTA de evolucion (centrado, mitad de pantalla)
#define EVO_BTN_W U(256)
#define EVO_BTN_H UY(64)
#define EVO_BTN_X (CX - EVO_BTN_W / 2)
#define EVO_BTN_Y UY(172)
// boton-CTA de despedida (mas ancho: lleva el nombre + frase)
#define FAR_BTN_W U(408)
#define FAR_BTN_H UY(58)
#define FAR_BTN_X (CX - FAR_BTN_W / 2)
#define FAR_BTN_Y UY(176)
// Cajas de los dialogos del juego: el dibujo y el toque usan las mismas, asi no
// pueden desajustarse.
// dialogo de decision (evolucionar/mantener, despedirse/quedaros)
#define CHOICE_X U(93)
#define CHOICE_W U(280)
#define CHOICE_BH UY(52)
#define CHOICE_B1_Y UY(206)
#define CHOICE_B2_Y UY(268)
// dialogo "soltar?": boton SI (NO va 130 px de diseno a su derecha)
#define YES_X U(118)
#define YES_Y UY(252)
#define YES_W U(100)
#define YES_H UY(52)
#define NO_X (YES_X + U(130))
// selector de comida: 4 casillas separadas por FEED_STRIDE
#define FEED_X U(101)
#define FEED_Y UY(288)
#define FEED_STRIDE U(66)
#define FEED_W (4 * FEED_STRIDE)
#define FEED_H UY(64)
// icono i (16x16 a escala SC(3)) dentro de su casilla
#define FEED_ICON_X(i) (FEED_X + U(9) + (i) * FEED_STRIDE)
#define FEED_ICON_Y (FEED_Y + UY(8))
// pastillas de medallas de la ficha: dos columnas
#define MED_W U(192)
#define MED_GAP U(6)
// boton ENTRENAR FUERZA de la ficha
#define TRAIN_X U(96)
#define TRAIN_Y UY(300)
#define TRAIN_W U(274)
#define TRAIN_H UY(40)
// pantalla de ajuste de hora: botones +/-, interruptor de sonido y OK
#define CLOCK_BTN_W U(58)
#define CLOCK_BTN_H UY(58)
#define CLOCK_BTN_Y UY(190)
// grupos hora (- +) y minutos (- +) de 124 de ancho con 24 de hueco, centrados en la pantalla
#define CLK_X0 (CX - U(136))
#define CLK_X1 (CX - U(70))
#define CLK_X2 (CX + U(12))
#define CLK_X3 (CX + U(78))
#define SND_PILL_X (CX - SND_PILL_W / 2)
#define SND_PILL_W U(96)
#define CLOCK_OK_X U(133)
#define CLOCK_OK_Y UY(340)
#define CLOCK_OK_W U(200)
#define CLOCK_OK_H UY(48)
// zonas de toque de arriba: la cabecera sale/abandona; el nombre de la ficha abre el teclado
#define HEADER_ZONE_H UY(72)
#define CARD_NAME_ZONE_H UY(84)
// el CST9217 avisa por el pin INT cuando hay datos tactiles; lo usamos para no
// leer el bus I2C mientras el chip esta dormido (esa lectura se colgaba ~1s)
volatile bool gTouchIrq = false;
void IRAM_ATTR touchIsr() { gTouchIrq = true; }
uint32_t lastRender = 0;
uint32_t sdEpoch = 0;  // +1 cada vez que se recarga la SD (las miniaturas cambian)
// proteccion del AMOLED: atenuado por inactividad
uint32_t lastInteract = 0;
uint8_t dimStage = 0;        // 0 despierto, 1 atenuado (90s), 2 casi apagado (5min)
bool swallowGesture = false; // el toque que despierta no acciona nada
uint32_t holdStart = 0;     // pulsacion larga sobre el bicho
uint32_t confirmUntil = 0;  // dialogo "soltar?" activo hasta este millis
uint8_t choiceKind = 0;     // dialogo de decision: 0 ninguno, 1 evolucion, 2 despedida
uint32_t choiceUntil = 0;   // se cierra solo a este millis
int16_t tX0, tY0, tXl, tYl; // gesto en curso (inicio y ultima posicion)
uint32_t tStart = 0;
bool holdFired = false;

void setup() {
  Serial.setRxBufferSize(8192);  // la transferencia a SD llega en bloques de 2 KB
  Serial.begin(115200);
  // CRITICO: sin esto, Serial.print BLOQUEA el juego cuando no hay un
  // monitor serie abierto en el host (el bufer TX del USB CDC se llena
  // y nadie lo vacia) -> con timeout 0 los mensajes se descartan
  Serial.setTxTimeoutMs(0);
  Serial.printf("TamaPoke fw v%s\n", FW_VERSION);
  Wire.begin(IIC_SDA, IIC_SCL);
  // CST9217 (tactil), AXP2101 (PMU) y PCF85063 (RTC) comparten este bus I2C.
  // Red de seguridad para PMU/RTC (SensorLib NO respeta este timeout en el
  // tactil; el cuelgue del tactil dormido se resuelve gateando por INT, ver
  // handleTouch).
  Wire.setTimeOut(50);

  // CRITICO: encender la alimentacion del panel (BLDO1=OLED VDD 3.3V) ANTES de
  // inicializar el display. Si el PMU se reseteo (drenaje total), este rail
  // queda OFF y la pantalla se ve negra aunque el resto de la placa funcione.
#ifndef BOARD_AMOLED_216  // los ejemplos de Waveshare 2.16 arrancan el panel sin tocar los rails
  pmuEnablePanel();
#endif

  // QSPI a 80MHz (por defecto 40): el flush del framebuffer es el cuello de
  // botella del fps (~56ms a 40MHz). Si el panel mostrara basura, bajar a 40M.
  if (!gfx->begin(80000000)) Serial.println("gfx->begin() fallo");
#ifdef BOARD_AMOLED_216
  // orientacion de Waveshare para la 2.16 (MADCTL); va emparejada con el
  // swap/mirror del tactil de mas abajo: si cambia una, cambia la otra
  bus->writeC8D8(0x36, 0xA0);
#endif
  panel->setBrightness(180);

  touch.setPins(TP_RESET, TP_INT);
  bool touchOk = false;
  for (int i = 0; i < 3 && !touchOk; i++) {  // a veces falla al primer intento
    touchOk = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    if (!touchOk) delay(150);
  }
  if (!touchOk) Serial.println("tactil CST92xx no detectado");
  // begin() deja el chip en modo comando (lee la identidad y no sale);
  // hace falta un reset por hardware para que vuelva a reportar toques
  touch.reset();
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
#ifdef BOARD_AMOLED_216
  touch.setSwapXY(true);  // combinacion del ejemplo oficial de Waveshare 2.16
  touch.setMirrorXY(true, false);
#else
  touch.setMirrorXY(true, true);  // el panel esta montado girado 180 grados
#endif
  // INT activo-bajo: salta cuando hay datos. Gatea las lecturas I2C (ver loop)
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), touchIsr, FALLING);

  pet.begin();
  sdBegin();
  thumbs.load();

  // reloj real: aplica el tiempo que estuvo apagado
  rtcBegin();
  batBegin();
  pwrSetup();
  uint32_t e = rtcEpoch();
  if (e == 0) {
    // Sin pila de respaldo, el PCF85063 pierde la hora al cortar la
    // alimentacion. Sembrar siempre la fecha fija haria RETROCEDER el tiempo de
    // juego respecto a lo ya guardado, y eso rompe la racha, la edad y la hora
    // de la escena. Se siembra con la ultima hora vista, si es posterior, para
    // que el tiempo no vaya nunca hacia atras. Los minutos apagado se pierden
    // igual (sin RTC no hay forma de saberlos), pero nada se descuadra.
    uint32_t seed = 1767225600UL;             // RTC virgen de verdad
    uint32_t seen = pet.savedEpoch();         // usa la NVS que pet ya tiene abierta
    if (seen > seed) seed = seen;
    rtcSetEpoch(seed);
    e = rtcEpoch();
    Serial.printf("RTC sin hora: sembrado en %u%s\n", seed,
                  seen > 1767225600UL ? " (desde la ultima hora guardada)" : "");
  }
  pet.syncClock(e);

  nearbyBegin();  // id de este aparato; la radio solo se enciende en ventanas cortas (nearby.cpp)
  audioBegin();  // ES8311 + I2S + amplificador (suena un jingle de arranque)

  lastInteract = millis();
}

// carga/descarga el sprite de SD cuando cambia la especie
void ensureMon() {
  if (pet.speciesId == monFor && monShinyFor == pet.shiny && !sdDirty) return;
  // los ficheros recien recibidos pueden incluir un thumbs.bin nuevo, que se
  // cargaba solo en setup(): sin esto no se veia en la galeria hasta reiniciar
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
    pmd.load(pet.speciesId, pet.shiny);          // principal: PMD
    if (!pmd.loaded) mon.load(pet.speciesId, pet.shiny);  // respaldo: B/N
  }
}

void loop() {
  uint32_t now = millis();
  pet.update(now);

  // el amplificador sigue al estado de sueno (la llamada sale sola si no cambia).
  // Aqui cubre todas las vias: el boton de luz, el sueno nocturno y el que llega
  // aplicado desde la progresion offline.
  audioSetSleeping(pet.sleeping);

  // avisa con un sonido cuando el bicho pasa a estar listo para evolucionar
  // (incluye el caso de cumplir al despertar). canEvolveNow es false durmiendo.
  static bool wasEvoReady = false;
  bool evoReady = pet.wantEvolveButton();
  if (evoReady && !wasEvoReady) sfxPlay(SFX_MEDAL);
  wasEvoReady = evoReady;
  // aviso sombrio cuando el bicho esta a punto de escaparse por abandono
  static bool wasRunReady = false;
  bool runReady = pet.canRunawayNow();
  if (runReady && !wasRunReady) sfxPlay(SFX_DENY);
  wasRunReady = runReady;

  handleTouch();
  handleSerial();
  ensureMon();

  // pulsacion corta del PWR: pantalla on/off
  static uint32_t lastPwr = 0;
  if (now - lastPwr > 250) {
    lastPwr = now;
    if (pwrShortPressed()) {
      screenOff = !screenOff;
      if (!screenOff) wakeUI();
    }
  }

  updateBrightness(now);

  // bateria al limite seguro (sin USB): se guarda la partida y se apaga el PMU antes de que la
  // celda baje mas; el RTC conserva la hora
  if (batShouldShutdown() && !usbPresentNow()) {
    pet.flushSave();
    panel->setBrightness(0);
    batPowerOff();
  }

  // vuelca el autoguardado periodico SOLO con la pantalla atenuada/apagada o
  // durmiendo: la escritura a NVS congela ~1s ambos cores (caché de flash off),
  // y aqui no hay animacion que se corte ni dedo esperando respuesta. Con 90s
  // de inactividad la pantalla ya atenua, asi que se vuelca enseguida; el uso
  // activo persiste igual por los guardados de cada accion (comer/jugar/...).
  if (pet.savePending() && (screenOff || dimStage >= 1 || pet.sleeping)) {
    pet.flushSave();
  }

  // anota la hora real cada 30 s (se persiste en cada save del juego)
  static uint32_t lastClock = 0;
  if (now - lastClock > 30000) {
    lastClock = now;
    uint32_t e = rtcEpoch();
    if (e) pet.lastSeenEpoch = e;
  }

  // latido de salud cada 5 min (para el soak test; se descarta si no hay monitor)
  static uint32_t lastHealth = 0;
  if (now - lastHealth > 300000) {
    lastHealth = now;
    Serial.printf("HEALTH up=%lus heap=%u min=%u bat=%d%% mv=%d chg=%d usb=%d dim=%u off=%d\n",
                  (unsigned long)(now / 1000), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  batPercent(), batMillivolts(), batCharging() ? 1 : 0,
                  usbPresent() ? 1 : 0, dimStage, screenOff ? 1 : 0);
  }

  // Ritmo de dibujo (power.h): 10 fps con el bicho en uso, menos cuanto mas rato sin tocar,
  // 85 ms en juego/saco (margen seguro para que el redibujado no pise el envio DMA del frame
  // anterior: a 40-65 ms solapaba y causaba flashes negros).
  // Con la pantalla apagada no se dibuja: a brillo 0 no se ve nada, pero el redibujado y el
  // flush DMA del framebuffer del lienzo seguian corriendo a 10 fps, y medido son 70 ms de
  // trabajo por cada 100 (46 de ellos solo el volcado). Cada render repinta la escena
  // entera, asi que no queda nada a medias; y al quedarse lastRender congelado, el primer
  // frame tras despertar sale en el acto.
  PowerCtx pc = powerCtx(millis());
  NbSelf nbSelf = { !pet.isEgg() && pet.speciesId >= 1, (uint8_t)pet.speciesId, pet.shiny, pet.sleeping, pet.level() };
  nearbyPoll(now, pc, nbSelf);
  if (!screenOff) ensureNeighbors();
  audioSetIdle(!amplifierWanted(pc));  // el amplificador consume en reposo
  batSetPollMs(powerCacheMs(pc));
  uint32_t interval = frameIntervalMs(pc);
  bool drewFrame = false;
  if (interval && now - lastRender >= interval) {
    lastRender = now;
    drewFrame = render();
  }
  // cede la CPU en los pases que no dibujan: sin esto el loop gira a pleno rendimiento
  // entre frames; 1 ms no se nota en el tactil (se lee cada 20 ms) y el frame no se retrasa
  if (!drewFrame) idleWait(pc);
}

// estado de consumo actual (ver power.h)
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

// el usuario ha interactuado: vuelve el ritmo normal de dibujo (sin esperar al proximo
// frame de un ritmo lento) y el amplificador se enciende antes de que suene nada
void wakeUI() {
  uint32_t now = millis();
  uint32_t iv = frameIntervalMs(powerCtx(now));
  bool slow = !iv || iv > 100;  // pantalla apagada o ritmo lento: se dibuja ya, sin esperar al frame
  lastInteract = now;
  // el frame forzado respeta los 85 ms entre redibujados (si no, solapa el DMA del anterior)
  if (slow) lastRender = (now - lastRender >= 85) ? 0 : lastRender - 15;
  audioSetIdle(false);
}

// brillo segun sueno + inactividad (proteccion del AMOLED) y bateria
void updateBrightness(uint32_t now) {
  // los eventos visibles despiertan la pantalla solos
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

// Pausa de un pase del bucle que no ha dibujado. Con la pantalla apagada, o encendida pero casi
// apagada (5 min sin tocar), y sin USB se duerme la CPU (light sleep) hasta el proximo frame o
// 200 ms: despierta por el temporizador o por un toque (el INT del tactil baja). Con USB no, para
// no perder el puerto serie de la tarjeta SD: se mira sin cache, por si se acaba de enchufar.
void idleWait(PowerCtx c) {
  // no se duerme justo tras dibujar (el volcado DMA del frame sigue en marcha) ni con un aviso sonando
  if (lightSleepAllowed(c) && millis() - lastRender >= 100 && !audioBusy() && !nearbyRadioOn()) {
    c.usb = usbPresentNow();
    if (!lightSleepAllowed(c)) { delay(idleDelayMs(c)); return; }
    if (digitalRead(TP_INT) == LOW) { gTouchIrq = true; delay(1); return; }  // toque pendiente: no se duerme
    uint32_t ms = POWER_SLEEP_MS;
    uint32_t iv = frameIntervalMs(c);
    if (iv) {  // pantalla encendida: hasta el proximo frame
      uint32_t due = lastRender + iv - millis();
      if ((int32_t)due < (int32_t)ms) ms = (int32_t)due > 1 ? due : 1;
    }
    uint32_t nb = nearbyNextEventMs(millis());  // el sueno no pasa de la proxima ventana de radio
    if (nb && nb < ms) ms = nb;
    esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000);
    gpio_wakeup_enable((gpio_num_t)TP_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
    // la interrupcion del tactil vuelve a ser por flanco (como la dejo attachInterrupt); un toque
    // que ya haya bajado la linea se registra a mano
    gpio_wakeup_disable((gpio_num_t)TP_INT);
    gpio_set_intr_type((gpio_num_t)TP_INT, GPIO_INTR_NEGEDGE);
    if (digitalRead(TP_INT) == LOW) gTouchIrq = true;
    return;
  }
  delay(idleDelayMs(c));
}

// ---------- consola serie (provision de SD + depuracion) ----------

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  if (sdSerialCommand(line)) return;

  if (line == "HATCH") {
    pet.eggTap(); pet.eggTap(); pet.eggTap();
    Serial.println("DONE");
  } else if (line.startsWith("SPEC ")) {
    int n = line.substring(5).toInt();
    if (n >= 1 && n <= DEX_COUNT) {
      pet.prevSpeciesId = pet.speciesId;
      pet.speciesId = n;
      Serial.printf("especie #%d %s\n", n, DEX_TBL[n].name);
    }
    Serial.println("DONE");
  } else if (line.startsWith("LVL ")) {
    pet.ageMinutes = (uint32_t)line.substring(4).toInt() * MINUTES_PER_LEVEL;
    Serial.println("DONE");
  } else if (line.startsWith("TIME ")) {
    uint32_t e = (uint32_t)line.substring(5).toInt();
    rtcSetEpoch(e);
    pet.setClock(e);
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line.startsWith("RTCSET ")) {  // solo RTC (simular apagados en pruebas)
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
    // simula 20 tiradas de huevo (no cambia el estado del juego)
    for (int i = 0; i < 20; i++) {
      int16_t d = pet.pickEggSpecies();
      Serial.printf("%d:%s(r%u) ", d, DEX_TBL[d].name, DEX_TBL[d].rarity);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line == "SHINY") {  // alterna shiny del actual (pruebas)
    pet.shiny = !pet.shiny;
    Serial.printf("shiny=%d\n", pet.shiny);
    Serial.println("DONE");
  } else if (line.startsWith("NICK ")) {
    pet.rename(line.substring(5).c_str());
    Serial.printf("nick=%s\n", pet.nick);
    Serial.println("DONE");
  } else if (line == "CAREDAY") {  // simula un dia nuevo cuidado (pruebas)
    pet.setClock(pet.lastSeenEpoch + 86400);
    pet.caress();
    Serial.printf("streak=%u bond=%u medals=0x%X\n", pet.streak, pet.bond, pet.medals);
    Serial.println("DONE");
  } else if (line == "BYE") {
    pet.startFarewell();
    Serial.println("DONE");
  } else if (line == "RUN") {
    pet.startRunaway();
    Serial.println("DONE");
  } else if (line == "BEEP") {
    sfxPlay(SFX_HATCH);  // prueba de audio
    Serial.println("DONE");
  } else if (line == "ABANDON") {
    pet.dbgRunawayReady();  // fuerza el estado "lista para escaparse" (test del boton)
    Serial.println("DONE");
  } else if (line == "WIPE") {
    pet.factoryReset();     // borra NVS y reinicia -> partida nueva (eleccion de inicial)
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

// Un dialogo cuyo tiempo se agoto ya no esta en pantalla, aunque render() aun no lo haya
// borrado: no debe recibir toques ni impedir gestos.
void expireDialogs() {
  if (choiceKind && !timeLeft(choiceUntil)) choiceKind = 0;
  if (confirmUntil && !timeLeft(confirmUntil)) confirmUntil = 0;
  if (feedMenuUntil && !timeLeft(feedMenuUntil)) feedMenuUntil = 0;
}

bool inPetZone(int16_t x, int16_t y) {
  return x > U(110) && x < U(356) && y > UY(95) && y < UY(310);
}

// el toque se resuelve al LEVANTAR el dedo para distinguir tap de deslizar
void handleTouch() {
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 20) return;  // 50 Hz le sobra a un dedo
  lastPoll = millis();
  // solo tocamos el bus si el chip aviso por INT o si el dedo sigue abajo (hay
  // que detectar el levantamiento). Leer el CST9217 dormido se colgaba ~1s y
  // congelaba el loop entero; SensorLib no respeta el timeout de Wire.
  if (!gTouchIrq && !wasPressed) return;
  gTouchIrq = false;
  // Un ciclo de solo direccion antes de leer. Sin esto, getPoint() se colgaba
  // exactamente 1000 ms (el timeout por defecto del driver I2C, que SensorLib no
  // acota) y congelaba el loop entero: es lo que se percibia como "el minijuego
  // se congela 2-3 segundos al tocar la bola" (issue #16), porque ahi los toques
  // son rapidos y seguidos.
  //
  // Medido en placa, jugando lo mismo (~470 lecturas, marcador ~50):
  //   sin esta linea: 5 parones de 1000 ms en 60 s
  //   con ella:       0
  // El contador de rechazos salio 0 en ambos casos, asi que NO funciona
  // saltandose lecturas cuando el chip no contesta: lo que hace es despertarlo,
  // para que la lectura siguiente no se encuentre el CST9217 dormido.
  Wire.beginTransmission(TOUCH_ADDR);
  if (Wire.endTransmission() != 0) return;  // no responde: se reintenta en 20 ms
  int16_t x, y;
  bool pressed = touch.getPoint(&x, &y, 1) > 0;
  expireDialogs();  // la pulsacion larga y los gestos ven el mismo estado que el dibujo

  // saco de entrenamiento: cada toque cuenta al instante (aporrear rapido)
  if (sackOpen) {
    if (pressed && !wasPressed) {
      wakeUI();
      sackTouch(y);
    }
    wasPressed = pressed;
    return;
  }

  if (pressed && !wasPressed) {  // empieza el gesto
    tX0 = tXl = x;
    tY0 = tYl = y;
    tStart = millis();
    holdFired = false;
    swallowGesture = (dimStage > 0) || screenOff;  // si estaba a oscuras, solo despierta
    screenOff = false;
    wakeUI();
  } else if (pressed) {  // sigue apoyado
    tXl = x;
    tYl = y;
    // pulsacion larga sin moverse sobre el bicho -> dialogo de soltar
    if (!holdFired && !swallowGesture && !galleryOpen && !cardOpen && !kbOpen && !clockOpen && millis() - tStart > 3000 &&
        abs(tXl - tX0) < U(30) && abs(tYl - tY0) < UY(30) && inPetZone(tX0, tY0) &&
        !pet.isEgg() && !confirmUntil && !pet.ceremony) {
      confirmUntil = millis() + 10000;
      holdFired = true;
    }
  } else if (wasPressed) {  // levanta el dedo: resolver gesto
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

// deslizar vertical: abre/cierra la ficha del bicho
void openClock();  // prototipo

void onSwipeV(int dir) {
  expireDialogs();
  if (pet.awaitingStarter()) return;  // bloqueado durante la eleccion de inicial
  if (gameOpen || galleryOpen || kbOpen || sackOpen || pet.ceremony) return;
  if (clockOpen) { clockOpen = false; return; }
  if (cardOpen) {
    if (dir < 0) cardOpen = false;  // arriba cierra la ficha
    return;
  }
  if (dir > 0) {                    // deslizar abajo: ajustar hora
    if (!confirmUntil && !feedMenuUntil) openClock();
  } else if (!pet.isEgg() && !confirmUntil && !feedMenuUntil) {
    cardOpen = true;                // deslizar arriba: ficha
    cardPage = 0;
  }
}

// deslizar: dir +1 = hacia la derecha
void onSwipe(int dir) {
  expireDialogs();
  if (pet.awaitingStarter()) return;  // bloqueado durante la eleccion de inicial
  if (gameOpen || kbOpen || clockOpen) return;
  if (cardOpen) {  // dentro de la ficha: cambiar entre las 4 paginas
    int p = (int)cardPage + (dir > 0 ? -1 : 1);  // izquierda avanza
    cardPage = p < 0 ? 0 : (p > 3 ? 3 : p);
    return;
  }
  if (!galleryOpen) {
    if (!pet.ceremony && !confirmUntil) {
      galleryOpen = true;
      galleryPage = 0;
      galleryDetail = 0;
      galleryDirty = true;
    }
    return;
  }
  if (galleryDetail) {  // en detalle: volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  int np = galleryPage - dir;  // deslizar a la izquierda avanza pagina
  if (np < 0) {                // retroceder desde la primera = salir
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
  // Serial.printf("TOUCH %d %d\n", x, y);  // diagnostico (silenciado: satura el log)
  if (pet.awaitingStarter()) {  // primera partida: elegir inicial
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
  if (pet.ceremony) return;  // durante la despedida no hay botones
  if (cardOpen) {
    if (cardPage == 0 && y < CARD_NAME_ZONE_H) openKeyboard();  // tocar el nombre = renombrar
    else if (cardPage == 1 && y >= TRAIN_Y && y <= TRAIN_Y + TRAIN_H && x >= TRAIN_X && x <= TRAIN_X + TRAIN_W) {
      cardOpen = false;            // boton ENTRENAR FUERZA
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
  if (choiceKind) {          // dialogo de decision: boton accion (arriba) / mantener (abajo)
    bool b1 = (x >= CHOICE_X && x <= CHOICE_X + CHOICE_W && y >= CHOICE_B1_Y && y <= CHOICE_B1_Y + CHOICE_BH);  // accion
    bool b2 = (x >= CHOICE_X && x <= CHOICE_X + CHOICE_W && y >= CHOICE_B2_Y && y <= CHOICE_B2_Y + CHOICE_BH);  // mantener / quedaros
    if (choiceKind == 1) {                 // evolucion
      if (b1) { int16_t old = pet.speciesId; pet.evolve(); evoPmd.load(old, pet.shiny); }
      else if (b2) pet.declineEvolve();
    } else if (choiceKind == 2) {          // despedida
      if (b1) pet.startFarewell();
      else if (b2) pet.declineFarewell();
    }
    choiceKind = 0;
    return;
  }
  if (confirmUntil) {        // dialogo "soltar?": SI / NO
    if (x >= YES_X && x <= YES_X + YES_W && y >= YES_Y && y <= YES_Y + YES_H) {
      pet.release();
    }
    confirmUntil = 0;
    return;
  }
  if (feedMenuUntil) {       // selector de comida
    if (y >= FEED_Y && y <= FEED_Y + FEED_H && x >= FEED_X && x <= FEED_X + FEED_W) {
      int item = (x - FEED_X) / FEED_STRIDE;
      if (item > 3) item = 3;  // el borde derecho del recuadro cuenta como la ultima casilla
      if (item == 3) pet.feedCandy();
      else pet.feedBerry(item);
      sfxPlay(SFX_EAT);
    }
    feedMenuUntil = 0;
    return;
  }
  if (pet.isEgg()) {
    pet.eggTap();
    sfxPlay(SFX_TAP);
    return;
  }
  // boton de evolucion: abre el dialogo evolucionar/mantener
  if (pet.wantEvolveButton() && x >= EVO_BTN_X && x <= EVO_BTN_X + EVO_BTN_W &&
      y >= EVO_BTN_Y && y <= EVO_BTN_Y + EVO_BTN_H) {
    choiceKind = 1; choiceUntil = millis() + 12000;
    return;
  }
  // botones de final (mismo recuadro): escapada directa; despedida abre dialogo
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
  // tocar al bicho = caricia
  if (inPetZone(x, y)) {
    Serial.println("PET");
    pet.caress();
    if (!pet.sleeping) sfxPlay(SFX_HEART);
  }
}

// ---------- render ----------

bool gNight = false;  // noche real (por hora) o durmiendo: lo fija render()
uint16_t inkColor() { return gNight ? UI_INK_NIGHT : UI_INK; }

// ---------- escena de fondo: bioma del tipo + hora real del RTC ----------

#define C565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define HORIZON UY(232)  // linea donde el cielo se encuentra con el suelo

uint16_t lerp565(uint16_t a, uint16_t b, int i, int n) {
  if (n <= 0) return a;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar + (br - ar) * i / n) << 11)) |
                    (((ag + (bg - ag) * i / n) << 5)) | (ab + (bb - ab) * i / n));
}

// hora del dia 0-23 (de la hora real cacheada cada 30s; 13 si no hay reloj)
int sceneHour() {
  uint32_t e = pet.lastSeenEpoch;
  return e ? (int)((e / 3600) % 24) : 13;
}

// suelo de cada bioma de dia (de noche se mezcla hacia el azul nocturno)
static const uint16_t BIOME_SOIL[6] = {
  C565(0x7e, 0xc0, 0x7f),  // 0 pradera
  C565(0xdc, 0xca, 0x94),  // 1 playa (arena)
  C565(0x4f, 0x8a, 0x55),  // 2 bosque
  C565(0x8a, 0x55, 0x44),  // 3 volcan
  C565(0xa8, 0x90, 0x6a),  // 4 montana
  C565(0xe6, 0xee, 0xf5),  // 5 nieve
};

void drawClouds(uint32_t now, uint16_t col) {
  for (int k = 0; k < 2; k++) {
    int cx = (int)(U(now / 50 + k * 250) % (UI_W + U(94))) - U(40);  // px/s proporcionales al panel
    int cy = UY(70 + k * 34);
    gfx->fillCircle(cx, cy, U(16), col);
    gfx->fillCircle(cx + U(18), cy + UY(3), U(13), col);
    gfx->fillCircle(cx - U(15), cy + UY(4), U(12), col);
  }
}

void drawScene(uint8_t biome, uint32_t now, bool night) {
  int h = sceneHour();
  uint16_t top, bot;
  if (night)            { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (h < 8)       { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }  // amanecer
  else if (h < 18)      { top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }  // dia
  else                  { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }  // atardecer

  // cielo en bandas
  for (int y = 0; y < HORIZON; y += UY(8))
    gfx->fillRect(0, y, UI_W, UY(8), lerp565(top, bot, y, HORIZON));

  // sol o luna
  if (night) {
    gfx->fillCircle(U(360), UY(78), U(24), C565(0xe8, 0xee, 0xf5));
    gfx->fillCircle(U(370), UY(72), U(22), lerp565(top, bot, UY(78), HORIZON));  // creciente
    for (auto &st : STARS) gfx->fillRect(U(st[0]), UY(st[1]), SC(4), SC(4), UI_WHITE);
  } else if (h < 18) {
    gfx->fillCircle(U(360), UY(84), U(26), h < 8 ? C565(0xff, 0xd9, 0x8a) : C565(0xff, 0xe7, 0x9f));
    drawClouds(now, C565(0xff, 0xff, 0xff));
  } else {
    gfx->fillCircle(CX, HORIZON - UY(6), U(34), C565(0xff, 0xf1, 0xc8));  // sol poniente
  }

  // mar de la playa: una franja de agua sobre la arena
  uint16_t soil = BIOME_SOIL[biome < 6 ? biome : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
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

  // suelo
  gfx->fillRect(0, HORIZON, UI_W, UI_H - HORIZON, soil);
  uint16_t hill = lerp565(soil, night ? C565(0x0c, 0x12, 0x24) : C565(0xff, 0xff, 0xff), 3, 16);
  gfx->fillRoundRect(-U(60), HORIZON - UY(14), UI_W + U(120), UY(60), U(30), hill);

  // detalles del bioma
  uint16_t dk = lerp565(soil, C565(0x10, 0x18, 0x20), night ? 11 : 7, 16);
  if (biome == 2) {  // bosque: coniferas en silueta
    for (int tx : { U(60), U(150), U(360), U(416) }) {
      gfx->fillTriangle(tx, HORIZON - UY(46), tx - U(16), HORIZON, tx + U(16), HORIZON, dk);
      gfx->fillTriangle(tx, HORIZON - UY(60), tx - U(12), HORIZON - UY(28), tx + U(12), HORIZON - UY(28), dk);
    }
  } else if (biome == 3) {  // volcan: rocas y brasas
    gfx->fillTriangle(U(70), HORIZON, U(40), HORIZON + UY(30), U(100), HORIZON + UY(30), dk);
    gfx->fillTriangle(U(400), HORIZON + UY(4), U(372), HORIZON + UY(30), U(430), HORIZON + UY(30), dk);
    if (!night)
      for (int e = 0; e < 4; e++)
        gfx->fillRect(U(120 + e * 70), HORIZON + UY(8 + (e % 2) * 6), SC(4), SC(4), C565(0xff, 0x9b, 0x3a));
  } else if (biome == 4) {  // montana: cumbres al fondo
    gfx->fillTriangle(U(140), HORIZON - UY(50), U(60), HORIZON, U(220), HORIZON, dk);
    gfx->fillTriangle(U(330), HORIZON - UY(38), U(250), HORIZON, U(410), HORIZON, dk);
  } else if (biome == 5 && !night) {  // nieve: copos cayendo
    for (int f = 0; f < 10; f++) {
      int fx = U(f * 53 + now / 40) % UI_W;
      int fy = UY(f * 90 + now / 18) % HORIZON;
      gfx->fillRect(fx, fy, SC(3), SC(3), UI_WHITE);
    }
  } else if (biome == 0) {  // pradera: matas de hierba
    for (int gx : { U(80), U(175), U(300), U(395) })
      for (int b = -1; b <= 1; b++)
        gfx->fillRect(gx + U(b * 5), HORIZON + UY(6), SC(2), UY(8 + (b == 0 ? 4 : 0)), dk);
  }
}

// fondo de los menus: un disco sobre negro en la pantalla redonda, todo el lienzo en la
// cuadrada
void drawScreenBg() {
#if LCD_ROUND
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, UI_R, UI_BG_DAY);
#else
  gfx->fillScreen(UI_BG_DAY);
#endif
}

// primera partida: elige inicial entre Bulbasaur / Charmander / Squirtle
void renderStarterSelect() {
  drawScreenBg();
  const char *t = T(S_CHOOSE_STARTER);
  gfx->setTextColor(UI_INK);
  setSize(TS(2));
  gfx->setCursor(centerX(t, TS(2)), UY(68));
  gfx->print(t);
  for (int i = 0; i < 3; i++) {
    int16_t d = STARTER_DEX[i];
    const DexEntry &de = DEX_TBL[d];
    int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
    gfx->fillRoundRect(STARTER_ROW_X, ry, STARTER_ROW_W, STARTER_ROW_H, U(14), lerp565(de.accent, UI_WHITE, 6, 8));
    gfx->drawRoundRect(STARTER_ROW_X, ry, STARTER_ROW_W, STARTER_ROW_H, U(14), de.accent);
    const uint8_t *th = thumbs.get(d);     // miniatura del inicial (si la SD esta lista)
    if (th) drawThumb(th, U(76), ry - UY(5), SC(3), false);
    gfx->setTextColor(UI_INK);
    setSize(TS(3));
    gfx->setCursor(U(178), ry + UY(24));
    gfx->print(dexName(d));
  }
  gfx->flush();
}

// ---------- texto: tamano y medida ----------
// setSize() guarda la escala aplicada porque la libreria tiene textsize_x protegido y textH()
// necesita saberla. La fuente es la 5x7 clasica de Arduino_GFX (CP437, 6 px de avance por caracter).
uint8_t gTextSize = 1;

void setSize(int n) {
  gTextSize = n;
  gfx->setTextSize(n);
}

// ancho de un texto a la escala `size`
uint16_t textW(const char *s, int size) { return (uint16_t)strlen(s) * 6 * size; }

// alto de una linea de texto con el tamano aplicado
static int textH() { return 7 * gTextSize; }

// x del cursor para dejar el texto centrado en CX
int centerX(const char *s, int size) { return CX - textW(s, size) / 2; }

// ancho visible de la pantalla a la altura y (px): en la redonda, la cuerda del circulo UI_R;
// en la cuadrada, todo el ancho
int chordW(int y) {
#if LCD_ROUND
  int dy = y - CY;
  int h2 = UI_R * UI_R - dy * dy;
  return h2 > 0 ? 2 * (int)sqrtf((float)h2) : 0;
#else
  return UI_W;
#endif
}

// ancho disponible para una linea de texto con su borde superior en y: la cuerda visible
// de la pantalla (en la redonda, la de su punto mas estrecho: arriba o abajo de la linea),
// con 4 px de diseno de aire por lado. Requiere el tamano ya aplicado (para textH()).
int chordAvail(int y) {
  int topW = chordW(y), botW = chordW(y + textH());
  return (topW < botW ? topW : botW) - U(8);
}

// cabe el texto, a ese tamano, en la cuerda a la altura y?
bool fitsChord(const char *s, int size, int y) {
  setSize(size);
  return textW(s, size) <= chordAvail(y);
}

// elige el tamano grande si el texto cabe en la cuerda de su linea, y el pequeno si no
int fitSize(const char *s, int big, int small, int y) {
  if (fitsChord(s, big, y)) return big;
  setSize(small);
  return small;
}

// Parte s en hasta maxLines lineas de <= maxW px a la escala aplicada: corta en el ultimo
// espacio que cabe (o en cualquier caracter si no hay espacios). Rellena
// start/len y devuelve el numero de lineas; si el texto no cabe en maxLines, la ultima
// se queda cortada.
int wrapText(const char *s, int size, int maxW, int maxLines, int *start, int *len) {
  int n = 0, pos = 0, L = (int)strlen(s);
  while (pos < L && n < maxLines) {
    while (s[pos] == ' ') pos++;
    if (pos >= L) break;
    int i = pos, end = pos, lastSpace = -1;
    while (i < L) {
      // llegar a i implica que s[pos..i) cabe: un espacio aqui es un corte valido aunque
      // el propio espacio ya no quepa (una palabra que acaba justo en maxW)
      if (s[i] == ' ') lastSpace = i;
      if ((i + 1 - pos) * 6 * size > maxW) break;
      end = ++i;
    }
    int cut;
    if (i >= L) cut = L;                           // todo el resto cabe
    else cut = lastSpace > pos ? lastSpace : (end > pos ? end : pos + 1);
    int e = cut;
    while (e > pos && s[e - 1] == ' ') e--;        // sin espacios al final de la linea
    start[n] = pos;
    len[n] = e - pos;
    n++;
    pos = cut;
  }
  return n;
}

// Escribe s a la escala `size`, partido en <= maxLines lineas de <= maxW px, con el bloque
// centrado verticalmente en yMid. leftAlign: x es el borde izquierdo; si no, el centro.
void drawWrapped(const char *s, int size, int x, int yMid, int maxW, int maxLines, bool leftAlign) {
  setSize(size);
  int st[4], ln[4];
  int n = wrapText(s, size, maxW, maxLines > 4 ? 4 : maxLines, st, ln);
  int gap = U(4);
  int total = n * textH() + (n - 1) * gap;
  int y = yMid - total / 2;
  for (int i = 0; i < n; i++) {
    char line[128];  // las cadenas del juego miden < 64 bytes
    int l = ln[i] < (int)sizeof(line) - 1 ? ln[i] : (int)sizeof(line) - 1;
    memcpy(line, s + st[i], l);
    line[l] = 0;
    gfx->setCursor(leftAlign ? x : x - textW(line, size) / 2, y + i * (textH() + gap));
    gfx->print(line);
  }
}

// Pantallas que solo cambian con un toque (o con un dato que cambia muy de vez en cuando):
// mientras su "clave" no cambie no se repintan ni se vuelcan por QSPI.
static bool staticScreen() {
  if (pet.awaitingStarter()) return true;
  if (galleryOpen || gameOpen || sackOpen) return false;  // la galeria gestiona su propio repintado
  if (kbOpen || clockOpen) return true;
  return cardOpen && cardPage != 0;  // la pagina 0 tiene el retrato animado
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
                        (uint32_t)pet.berryKnown, (uint32_t)pet.strHi })
      h = mixKey(h, v);
    for (const char *c = pet.nick; *c; c++) h = mixKey(h, (uint8_t)*c);
  }
  return h;
}
bool screenKeyValid = false;  // (el renderer del host lo invalida entre escenas)
uint32_t lastScreenKey = 0;

// Dibuja un frame y devuelve true si lo ha volcado al panel.
bool render() {
  if (staticScreen()) {
    uint32_t k = screenKey();
    if (screenKeyValid && k == lastScreenKey) return false;
    lastScreenKey = k;
    screenKeyValid = true;
  } else {
    screenKeyValid = false;
  }
  if (pet.awaitingStarter()) {  // primera partida: elegir inicial (prioridad total)
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
  int h = sceneHour();
  gNight = pet.sleeping || h < 6 || h >= 20;
  // drawScene cubre el lienzo completo: sin fillScreen(NEGRO) previo para
  // que un flush DMA solapado nunca capture negro a medias (anti-parpadeo)
  drawScene(pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome, millis(), gNight);

  if (pet.ceremony) {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    const char *msg = (pet.ceremony == CER_FAREWELL) ? T(S_FAREWELL)
                      : (pet.ceremony == CER_RUNAWAY) ? T(S_RUNAWAY)
                                                      : T(S_GOODBYE);
    drawHeader(dexName(pet.speciesId), d.accent, msg);
    drawCeremony();
    gfx->flush();
    return true;
  }

  if (pet.isEgg()) {
    drawHeader(T(S_EGG_HDR), inkColor(), eggMsg());
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
    gfx->fillRect(0, PANEL_Y, UI_W, UI_H - PANEL_Y, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    gfx->setTextColor(inkColor());
    setSize(TS(2));
    gfx->setCursor(centerX(reg, TS(2)), UY(348));
    gfx->print(reg);
  } else {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    char name[28];
    const char *base = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
    snprintf(name, sizeof(name), T(S_NAME_FMT), pet.shiny ? "*" : "", base, pet.level());
    drawHeader(name, gNight ? UI_INK_NIGHT : d.accent, statusMsg());
    drawStreakBadge();
    drawNeighbors();
    drawPet();
    drawBath();
    drawPoops();
    // panel inferior: base limpia para barras y botones sobre el paisaje
    gfx->fillRect(0, PANEL_Y, UI_W, UI_H - PANEL_Y, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    drawBars();
    drawButtons();
    drawCelebration();
    // sin dialogo abierto encima: el boton de despedida es mas ancho que el dialogo y asomaba por los lados
    if ((choiceKind && timeLeft(choiceUntil)) || timeLeft(confirmUntil) || timeLeft(feedMenuUntil)) {
      // un dialogo tapa el boton
    } else if (pet.wantEvolveButton()) drawEvolveButton();        // CTA rojo: evolucionar
    else if (pet.canRunawayNow()) drawRunawayButton();     // CTA sombrio: escapada (abandono)
    else if (pet.wantFarewellButton()) drawFarewellButton();  // CTA dorado: despedida
  }

  if (pet.sleeping) {
    gfx->setTextColor(UI_INK_NIGHT);
    setSize(TS(3));
    gfx->setCursor(U(320), UY(130));
    gfx->print("Zz");
  }

  // selector de comida
  if (feedMenuUntil) {
    if (!timeLeft(feedMenuUntil)) {
      feedMenuUntil = 0;
    } else {
      gfx->fillRoundRect(FEED_X, FEED_Y, FEED_W, FEED_H, U(14), UI_WHITE);
      gfx->drawRoundRect(FEED_X, FEED_Y, FEED_W, FEED_H, U(14), inkColor());
      const char *const *const items[4] = { SPR_ICON_FOOD, SPR_ICON_BERRY_B, SPR_ICON_BERRY_G, SPR_ICON_CANDY };
      for (int i = 0; i < 4; i++)
        drawMap(items[i], 16, FEED_ICON_X(i), FEED_ICON_Y, SC(3), false);
    }
  }

  // dialogo "soltar?" (pulsacion larga sobre el bicho)
  if (confirmUntil) {
    if (!timeLeft(confirmUntil)) {
      confirmUntil = 0;
    } else {
      gfx->fillRoundRect(U(94), UY(168), U(278), UY(152), U(16), UI_WHITE);
      gfx->drawRoundRect(U(94), UY(168), U(278), UY(152), U(16), UI_INK);
      char q[28];
      snprintf(q, sizeof(q), T(S_RELEASE_FMT), dexName(pet.speciesId));
      gfx->setTextColor(UI_INK);
      drawWrapped(q, TS(2), CX, UY(203), U(278) - U(16), 2, false);
      setSize(TS(2));
      gfx->fillRoundRect(YES_X, YES_Y, YES_W, YES_H, U(12), UI_BAR_OK);
      gfx->setTextColor(UI_WHITE);
      gfx->setCursor(YES_X + (YES_W - textW(T(S_YES), TS(2))) / 2, UY(270));
      gfx->print(T(S_YES));
      gfx->fillRoundRect(NO_X, YES_Y, YES_W, YES_H, U(12), UI_BAR_BAD);
      gfx->setCursor(NO_X + (YES_W - textW(T(S_NO), TS(2))) / 2, UY(270));
      gfx->print(T(S_NO));
    }
  }

  // dialogo de decision (evolucionar/mantener, despedirse/quedaros)
  if (choiceKind) {
    if (!timeLeft(choiceUntil)) choiceKind = 0;
    else drawChoiceDialog();
  }

  gfx->flush();
  return true;
}

// ---------- minijuego: toques con la pokeball ----------

void startGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
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
  float sp = 1.6f + gameScore * 0.05f;  // mas viva segun avanzas
  if (sp > 4.0f) sp = 4.0f;
  sp *= UI_F;
  ballVX = random(2) ? sp : -sp;
  ballVY = 0;
}

void gameTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  if (y < HEADER_ZONE_H) {  // tocar la cabecera = abandonar sin premio
    gameOpen = false;
    return;
  }
  float dx = ballX - x, dy = ballY - y;
  if (dx * dx + dy * dy < (float)U(74) * U(74)) {  // toque a la bola!
    gameScore++;
    sfxPlay(SFX_PLAY);
    // golpe mas suave: impulso moderado que crece poco a poco con la puntuacion
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
  // paso relativo a 85 ms, el periodo de frame con el que se afinaron estas
  // constantes (ver el planificador de render en loop()): sin esto la fisica
  // avanzaba "un paso por frame" y el juego iba mas rapido o mas lento segun
  // lo que tardara en dibujarse el frame anterior, que depende del tamano del
  // sprite cargado. Con un Charizard la bola caia mas despacio que con un
  // Diglett, asi que el record no era comparable entre especies.
  float k = lastGameStep ? (now - lastGameStep) / 85.0f : 1.0f;
  if (k > 3.0f) k = 3.0f;  // frame anormalmente tardio: no dar un salto enorme
  lastGameStep = now;

  float grav = 0.40f + gameScore * 0.013f;  // cae un poco mas rapido cada vez
  if (grav > 0.80f) grav = 0.80f;
  grav *= UI_F;
  ballVY += grav * k;
  ballX += ballVX * k;
  ballY += ballVY * k;
#if LCD_ROUND
  // rebote en la pared circular
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
  // rebote en las paredes y el techo del cuadrado (el mismo margen que la pared circular):
  // la posicion se corrige siempre y la velocidad solo se invierte si la bola va hacia fuera
  if (ballX < CX - BALL_R) { ballX = CX - BALL_R; if (ballVX < 0) ballVX *= -0.85f; }
  else if (ballX > CX + BALL_R) { ballX = CX + BALL_R; if (ballVX > 0) ballVX *= -0.85f; }
  if (ballY < CY - BALL_R) { ballY = CY - BALL_R; if (ballVY < 0) ballVY *= -0.85f; }
#endif
  if (ballY > UY(384)) {  // al suelo
    if (++gameMisses >= 3) {
      gameNewHi = (gameScore > pet.gameHi);
      pet.playResult(gameScore);  // actualiza el record y da felicidad
      sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
      gameOverUntil = millis() + 4000;
    } else {
      respawnBall();
    }
  }
  // el bicho la sigue por abajo
  float chase = (ballX - gamePetX) * 0.12f;
  const float cmax = 7 * UI_F;
  if (chase > cmax) chase = cmax;
  if (chase < -cmax) chase = -cmax;
  gamePetX += chase * k;
}

// ---------- saco de entrenamiento (entrena la fuerza) ----------

void startSack() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  sackOpen = true;
  sackUntil = millis() + 10000;
  sackOverUntil = 0;
  sackHits = 0;
  sackShake = 0;
  sackNewHi = false;
}

// toque con el saco abierto: la cabecera abandona, el resto golpea
void sackTouch(int16_t y) {
  if (y < HEADER_ZONE_H) sackOpen = false;
  else sackTap();
}

void sackTap() {
  if (!timeLeft(sackUntil)) return;  // ya termino el tiempo
  sackHits++;
  sackShake = 16 * UI_F;  // sacude el saco
}

void drawGameScene();  // prototipo (definida mas abajo)

void renderSack() {
  uint32_t now = millis();
  drawGameScene();  // fondo del habitat
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  // pantalla de resultado
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
    gfx->setTextColor(UI_BAR_BAD);
    setSize(TS(3));
    gfx->setCursor(centerX(g, TS(3)), UY(210));
    gfx->print(g);
    setSize(TS(2));
    if (sackNewHi && sackHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(centerX(T(S_NEW_RECORD), TS(2)), UY(256));
      gfx->print(T(S_NEW_RECORD));
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

  // se acabaron los 10 s: aplicar entrenamiento
  if (!timeLeft(sackUntil)) {
    sackNewHi = (sackHits > pet.strHi);
    sackGain = pet.trainStrength(sackHits);
    sfxPlay(sackNewHi ? SFX_MEDAL : SFX_PLAY);
    sackOverUntil = now + 3500;
    gfx->flush();
    return;
  }

  // aporreo activo
  sackShake *= 0.84f;
  int off = (int)(sackShake * sinf(now * 0.05f));
  int sx = CX + off, top = UY(86);
  gfx->fillRect(CX - U(3), UY(56), U(6), top - UY(56), ink);          // gancho/cuerda
  gfx->fillRect(sx - U(4), top - UY(30), U(8), UY(34), ink);          // cadena
  gfx->fillRoundRect(sx - U(42), top, U(84), UY(150), U(26), C565(0xb5, 0x3a, 0x3a));  // saco
  gfx->fillRoundRect(sx - U(42), top, U(84), UY(22), U(18), C565(0x7e, 0x28, 0x28));   // tapa
  gfx->drawRoundRect(sx - U(42), top, U(84), UY(150), U(26), ink);
  gfx->fillRect(sx - U(42), top + UY(70), U(84), UY(4), C565(0x7e, 0x28, 0x28));        // costura

  // contador de golpes
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", sackHits);
  gfx->setTextColor(ink);
  setSize(TS(6));
  gfx->setCursor(centerX(buf, TS(6)), UY(268));
  gfx->print(buf);

  setSize(TS(2));
  gfx->setCursor(centerX(T(S_HIT_FAST), TS(2)), UY(322));
  gfx->print(T(S_HIT_FAST));

  // barra de tiempo
  uint32_t left = sackUntil - now;
  int bw = U(280), fw = (int)((uint32_t)bw * left / 10000);
  gfx->fillRoundRect(CX - bw / 2, UY(350), bw, UY(16), U(5), UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, UY(350), fw, UY(16), U(5), UI_BAR_OK);

  gfx->flush();
}

// fondo del minijuego: hatibat del bicho (cielo por hora + suelo del bioma)
void drawGameScene() {
  int hh = sceneHour();
  bool night = hh < 6 || hh >= 20;
  uint16_t top, bot;
  if (night)       { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (hh < 8) { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }
  else if (hh < 18){ top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }
  else             { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }
  int hor = UY(376);
  for (int y = 0; y < hor; y += UY(8))
    gfx->fillRect(0, y, UI_W, UY(8), lerp565(top, bot, y, hor));
  if (night)
    for (auto &st : STARS) gfx->fillRect(U(st[0]), UY(st[1]), SC(4), SC(4), UI_WHITE);
  uint8_t bio = pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome;
  uint16_t soil = BIOME_SOIL[bio < 6 ? bio : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  gfx->fillRect(0, hor, UI_W, UI_H - hor, soil);
}

void renderGame() {
  // sin fillScreen(NEGRO): drawGameScene cubre el lienzo completo. Si el
  // DMA del flush anterior aun lee el buffer, vera contenido valido (no negro
  // a medio pintar), que era el parpadeo a 25 fps.
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
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(centerX(T(S_NEW_RECORD), TS(2)), UY(214));
      gfx->print(T(S_NEW_RECORD));
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

  // marcador, record y vidas
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", gameScore);
  gfx->setTextColor(ink);
  setSize(TS(4));
  gfx->setCursor(centerX(buf, TS(4)), UY(30));
  gfx->print(buf);
  // las tallas de los buffers de texto las vigila TestBuffersDeTexto en test/test_tools.py
  char rec[24];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.gameHi);
  setSize(TS(2));
  gfx->setCursor(centerX(rec, TS(2)), UY(76));
  gfx->print(rec);
  for (int i = 0; i < 3; i++) {
    // tres puntos separados 28 px de diseno, centrados bajo el marcador
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

  // anillo de impacto que se expande y desvanece (feedback suave del golpe)
  uint32_t ht = millis() - hitTime;
  if (hitTime && ht < 260) {
    int rad = U(22 + (int)(ht / 6));
    gfx->drawCircle((int)hitX, (int)hitY, rad, C565(0xff, 0xe7, 0x9f));
    gfx->drawCircle((int)hitX, (int)hitY, rad - U(2), C565(0xff, 0xd9, 0x8a));
  }

  // la pokeball
  int bs = SC(3);  // pokeball 16x16, centrada en (ballX, ballY)
  drawMap(SPR_ICON_PLAY, 16, (int)ballX - 8 * bs, (int)ballY - 8 * bs, bs, false);

  gfx->flush();
}

// ---------- ficha del bicho (deslizar vertical) ----------

// x donde arrancan las barras de la ficha: tras la etiqueta mas larga (4 letras a escala 2) y
// 12 px de diseno de aire, con 150 como minimo.
static int statBarX() {
  int x = U(96) + 4 * 6 * TS(2) + U(12);
  return x < U(150) ? U(150) : x;
}

void drawCardStat(int y, const char *label, uint16_t val, uint16_t maxBar, uint16_t color) {
  gfx->setTextColor(UI_INK);
  setSize(TS(2));
  gfx->setCursor(U(96), y);
  gfx->print(label);
  char num[8];
  snprintf(num, sizeof(num), "%u", val);
  gfx->setCursor(U(330), y);
  gfx->print(num);
  int bx = statBarX();
  int bw = U(310) - bx;   // la barra siempre acaba en 310, dejando aire hasta el numero
  int fw = (int)val * bw / maxBar;
  if (fw > bw) fw = bw;
  gfx->fillRoundRect(bx, y + UY(2), bw, UY(11), U(3), UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(bx, y + UY(2), fw, UY(11), U(3), color);
}

// ---------- ajuste de hora en pantalla (deslizar abajo) ----------
// El usuario pone su hora LOCAL a ojo; el firmware la usa tal cual, asi que
// no hay que gestionar zona horaria. Preserva el dia (no rompe racha/edad).

void openClock() {
  uint32_t e = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  clockH = (e / 3600) % 24;
  clockM = (e / 60) % 60;
  clockOpen = true;
}

void applyClock() {
  uint32_t base = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  uint32_t e = (base / 86400) * 86400 + (uint32_t)clockH * 3600 + (uint32_t)clockM * 60;
  rtcSetEpoch(e);
  pet.setClock(e);
  clockOpen = false;
}

void drawClockBtn(int x, int y, const char *l) {
  gfx->fillRoundRect(x, y, CLOCK_BTN_W, CLOCK_BTN_H, U(12), UI_WHITE);
  gfx->drawRoundRect(x, y, CLOCK_BTN_W, CLOCK_BTN_H, U(12), UI_INK);
  gfx->setTextColor(UI_INK);
  setSize(TS(4));
  gfx->setCursor(x + (CLOCK_BTN_W - textW(l, TS(4))) / 2, y + (CLOCK_BTN_H - textH()) / 2);
  gfx->print(l);
}

// pildora del interruptor de sonido, centrada
#define SND_PILL_Y UY(296)
#define SND_PILL_H UY(30)

// ancho para el aviso de cancelar: el de la cuerda donde cae la 2a linea de un bloque de
// dos centrado en y=410 (su borde superior queda 2 px de diseno bajo el centro)
int clockCancelW() {
  setSize(TS(2));
  return chordAvail(UY(410) + U(2));
}

void renderClock() {
  drawScreenBg();

  // --- textos ---
  gfx->setTextColor(UI_INK);
  setSize(TS(3));
  gfx->setCursor(centerX(T(S_SET_TIME), TS(3)), UY(44));
  gfx->print(T(S_SET_TIME));

  setSize(TS(2));
  gfx->setTextColor(UI_TRACK);
  // cada etiqueta, centrada bajo su grupo de botones
  gfx->setCursor((CLK_X0 + CLK_X1 + CLOCK_BTN_W - textW(T(S_HOUR), TS(2))) / 2, UY(256));
  gfx->print(T(S_HOUR));
  gfx->setCursor((CLK_X2 + CLK_X3 + CLOCK_BTN_W - textW(T(S_MIN), TS(2))) / 2, UY(256));
  gfx->print(T(S_MIN));

  // interruptor de sonido
  bool snd = audioEnabled();
  const char *sl = snd ? T(S_SND_ON) : T(S_SND_OFF);
  gfx->fillRoundRect(SND_PILL_X, SND_PILL_Y, SND_PILL_W, SND_PILL_H, U(8), snd ? UI_BAR_OK : UI_WHITE);
  gfx->drawRoundRect(SND_PILL_X, SND_PILL_Y, SND_PILL_W, SND_PILL_H, U(8), UI_INK);
  gfx->setTextColor(snd ? UI_BG_DAY : UI_INK);
  setSize(TS(2));
  gfx->setCursor(SND_PILL_X + (SND_PILL_W - textW(sl, TS(2))) / 2, SND_PILL_Y + (SND_PILL_H - textH()) / 2);
  gfx->print(sl);

  gfx->setTextColor(UI_TRACK);
  drawWrapped(T(S_CLOCK_CANCEL), TS(2), CX, UY(410), clockCancelW(), 2, false);

  // --- hora, botones, OK y version ---
  gfx->setTextColor(UI_INK);
  char t[8];
  snprintf(t, sizeof(t), "%02d:%02d", clockH, clockM);
  setSize(TS(7));
  gfx->setCursor(centerX(t, TS(7)), UY(108));
  gfx->print(t);

  drawClockBtn(CLK_X0, CLOCK_BTN_Y, "-");  // hora -
  drawClockBtn(CLK_X1, CLOCK_BTN_Y, "+");  // hora +
  drawClockBtn(CLK_X2, CLOCK_BTN_Y, "-");  // min -
  drawClockBtn(CLK_X3, CLOCK_BTN_Y, "+");  // min +

  gfx->fillRoundRect(CLOCK_OK_X, CLOCK_OK_Y, CLOCK_OK_W, CLOCK_OK_H, U(14), UI_BAR_OK);
  gfx->setTextColor(UI_BG_DAY);
  setSize(TS(3));
  gfx->setCursor(centerX("OK", TS(3)), UY(352));
  gfx->print("OK");

  // version del firmware (discreta, abajo del todo)
  char ver[20];
  snprintf(ver, sizeof(ver), "TamaPoke v%s", FW_VERSION);
  gfx->setTextColor(UI_TRACK);
  setSize(TS(1));
  gfx->setCursor(centerX(ver, TS(1)), UY(436));
  gfx->print(ver);

  gfx->flush();
}

void clockTap(int16_t x, int16_t y) {
  if (y >= CLOCK_BTN_Y && y <= CLOCK_BTN_Y + CLOCK_BTN_H) {  // fila de botones +/-
    if (x >= CLK_X0 && x < CLK_X0 + CLOCK_BTN_W) clockH = (clockH + 23) % 24;
    else if (x >= CLK_X1 && x < CLK_X1 + CLOCK_BTN_W) clockH = (clockH + 1) % 24;
    else if (x >= CLK_X2 && x < CLK_X2 + CLOCK_BTN_W) clockM = (clockM + 59) % 60;
    else if (x >= CLK_X3 && x < CLK_X3 + CLOCK_BTN_W) clockM = (clockM + 1) % 60;
    return;
  }
  if (y >= SND_PILL_Y && y <= SND_PILL_Y + SND_PILL_H && x >= SND_PILL_X && x < SND_PILL_X + SND_PILL_W) {  // interruptor de sonido
    audioSetEnabled(!audioEnabled());
    if (audioEnabled()) sfxPlay(SFX_TAP);    // confirma al encender
    return;
  }
  if (y >= CLOCK_OK_Y && y <= CLOCK_OK_Y + CLOCK_OK_H && x >= CLOCK_OK_X && x <= CLOCK_OK_X + CLOCK_OK_W) { applyClock(); return; }
}

// llama + numero de racha arriba a la izquierda
void drawStreakBadge() {
  if (pet.streak < 1) return;
  int x = U(26), y = UY(16);
  gfx->fillTriangle(x + U(8), y, x + U(1), y + UY(17), x + U(15), y + UY(17), UI_BAR_BAD);
  gfx->fillTriangle(x + U(8), y + UY(7), x + U(4), y + UY(17), x + U(12), y + UY(17), UI_BAR_WARN);
  char s[6];
  snprintf(s, sizeof(s), "%u", pet.streak);
  gfx->setTextColor(inkColor());
  setSize(TS(2));
  gfx->setCursor(x + U(22), y + UY(2));
  gfx->print(s);
}

// banner temporal: medalla nueva o hito de racha
void drawCelebration() {
  const char *l1 = nullptr, *l2 = nullptr;
  // 32 y no 20: "%u にちれんぞく！" ya pasa de 20 con una sola cifra, y snprintf
  // cortaba a mitad de un caracter de 3 bytes.
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
  gfx->fillRoundRect(U(73), UY(150), U(320), UY(96), U(16), UI_BAR_WARN);
  gfx->drawRoundRect(U(73), UY(150), U(320), UY(96), U(16), UI_INK);
  gfx->setTextColor(UI_INK);
  setSize(TS(3));
  gfx->setCursor(centerX(l1, TS(3)), UY(176));
  gfx->print(l1);
  setSize(TS(2));
  gfx->setCursor(centerX(l2, TS(2)), UY(212));
  gfx->print(l2);
}

// pagina 0: perfil (retrato grande, identidad, racha, vinculo, baya)
void renderCardProfile() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  char head[32];
  snprintf(head, sizeof(head), T(S_NAME_FMT), pet.shiny ? "*" : "", nm, pet.level());
  gfx->setTextColor(d.accent);
  // auto-encoge: a tamano 3 los nombres largos no caben en la franja estrecha de
  // arriba de la pantalla redonda, asi que se cortaban por el borde
  setSize(TS(3));
  // cabe a escala 3 si ocupa <= 198 px de diseno (la cuerda de la parte alta del circulo)
  bool big = textW(head, TS(3)) <= U(198);
  int hts = big ? TS(3) : TS(2);
  setSize(hts);
  gfx->setCursor(centerX(head, hts), UY(big ? 34 : 40));
  gfx->print(head);
  if (pet.nick[0]) {  // especie real bajo el apodo
    const char *sp = dexName(pet.speciesId);
    char par[32];
    snprintf(par, sizeof(par), "(%s)", sp);
    gfx->setTextColor(UI_TRACK);
    setSize(TS(2));
    gfx->setCursor(centerX(par, TS(2)), UY(64));
    gfx->print(par);
  }

  // retrato grande animado
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, UY(206), millis(), true, false, SC(4));

  // racha con llama
  int sx = U(138), sy = UY(224);
  gfx->fillTriangle(sx + U(8), sy, sx + U(1), sy + UY(18), sx + U(15), sy + UY(18), UI_BAR_BAD);
  gfx->fillTriangle(sx + U(8), sy + UY(7), sx + U(4), sy + UY(18), sx + U(12), sy + UY(18), UI_BAR_WARN);
  char rl[40];
  snprintf(rl, sizeof(rl), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  gfx->setTextColor(UI_INK);
  setSize(TS(2));
  gfx->setCursor(sx + U(24), sy + UY(2));
  gfx->print(rl);

  drawCardStat(UY(258), T(S_VIN), pet.bond, 100, C565(0xd4, 0x52, 0x7e));

  const char *berry = !pet.berryKnown ? T(S_BERRY_UNK)
                      : pet.lovesBerry(0) ? T(S_BERRY_RED)
                      : pet.lovesBerry(1) ? T(S_BERRY_BLUE)
                                          : T(S_BERRY_GREEN);
  char info[48];
  snprintf(info, sizeof(info), T(S_INFO_FMT), berry,
           (unsigned long)(pet.ageMinutes / 1440));
  gfx->setTextColor(UI_INK);
  setSize(TS(2));
  gfx->setCursor(centerX(info, TS(2)), UY(296));
  gfx->print(info);

  gfx->setTextColor(UI_TRACK);
  gfx->setCursor(centerX(T(S_RENAME_HINT), TS(2)), UY(332));
  gfx->print(T(S_RENAME_HINT));
}

// pagina 1: combate (4 barras + boton de entrenar)
void renderCardStats() {
  gfx->setTextColor(UI_INK);
  setSize(TS(3));
  gfx->setCursor(centerX(T(S_BATTLE), TS(3)), UY(48));
  gfx->print(T(S_BATTLE));

  drawCardStat(UY(118), T(S_STAT_ATK), pet.atkStat(), 260, UI_BAR_BAD);
  drawCardStat(UY(160), T(S_STAT_DEF), pet.defStat(), 260, 0x4C98);
  drawCardStat(UY(202), T(S_STAT_SPE), pet.speStat(), 260, UI_BAR_WARN);
  drawCardStat(UY(244), T(S_STAT_WGT), pet.weight, 100, 0xB3C8);

  // boton: saco de entrenamiento de fuerza
  gfx->fillRoundRect(TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H, U(12), UI_BAR_BAD);
  gfx->setTextColor(UI_BG_DAY);
  setSize(TS(2));
  gfx->setCursor(centerX(T(S_TRAIN_STR), TS(2)), UY(311));
  gfx->print(T(S_TRAIN_STR));
}

// pagina 2: medallas con etiqueta descriptiva
void renderCardMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[24];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  setSize(TS(3));
  gfx->setCursor(centerX(head, TS(3)), UY(48));
  gfx->print(head);

  for (int i = 0; i < MED_COUNT; i++) {
    // dos columnas centradas; las de arriba empiezan en 112 para que la esquina izquierda
    // quede dentro del circulo
    int x = CX - (2 * MED_W + MED_GAP) / 2 + (i % 2) * (MED_W + MED_GAP), y = UY(112 + (i / 2) * 54);
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, MED_W, UY(44), U(10), g ? UI_BAR_OK : UI_TRACK);
    if (g) {  // marca de conseguida
      gfx->fillCircle(x + U(22), y + UY(22), U(11), UI_BG_DAY);
      gfx->setTextColor(UI_BAR_OK);
      setSize(TS(2));
      gfx->setCursor(x + U(16), y + UY(13));
      gfx->print("v");
    }
    gfx->setTextColor(g ? UI_BG_DAY : 0x8410);
    // la descripcion va a la derecha de la marca (44 px); si no cabe en una linea pasa a dos
    drawWrapped(medalDesc(i), TS(2), x + U(44), y + UY(22), MED_W - U(44) - U(6), 2, true);
  }
}

// pagina 3: progreso (nivel, evolucion, descuidos) — saca a la luz mecanicas
// que antes eran invisibles (cuanto falta para subir/evolucionar y por que)
void renderCardProgress() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  gfx->setTextColor(UI_INK);
  setSize(TS(3));
  gfx->setCursor(centerX(T(S_PROGRESS), TS(3)), UY(44));
  gfx->print(T(S_PROGRESS));

  // nivel grande
  char lv[10];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), pet.level());
  setSize(TS(5));
  gfx->setCursor(centerX(lv, TS(5)), UY(86));
  gfx->print(lv);

  // barra de progreso al siguiente nivel (1 nivel = 60 min de juego)
  uint8_t into = pet.ageMinutes % MINUTES_PER_LEVEL;
  int bx = U(93), bw = U(280), by = UY(158), bh = UY(22);
  gfx->fillRoundRect(bx, by, bw, bh, U(6), UI_TRACK);
  int fw = (bw - U(4)) * into / MINUTES_PER_LEVEL;
  if (fw > 0) gfx->fillRoundRect(bx + U(2), by + UY(2), fw, bh - UY(4), U(5), UI_BAR_OK);
  char nx[32];
  snprintf(nx, sizeof(nx), T(S_NEXT_LVL_FMT), MINUTES_PER_LEVEL - into, pet.level() + 1);
  gfx->setTextColor(UI_INK);
  setSize(TS(2));
  gfx->setCursor(centerX(nx, TS(2)), by + UY(32));
  gfx->print(nx);

  // estado de evolucion
  gfx->setTextColor(UI_TRACK);
  gfx->setCursor(centerX(T(S_EVO_LABEL), TS(2)), UY(230));
  gfx->print(T(S_EVO_LABEL));
  char evoBuf[32];
  const char *evo;
  uint16_t evoCol = UI_INK;
  if (d.evolvesTo == 0) {
    evo = T(S_FINAL_FORM);
  } else {
    int needed = d.evolveLevel + pet.careMistakes;
    if (pet.level() >= needed) {
      if (pet.lowestStat() >= 40) { evo = T(S_EVO_READY); evoCol = UI_BAR_OK; }
      else { evo = T(S_EVO_BLOCKED); evoCol = UI_BAR_BAD; }
    } else {
      snprintf(evoBuf, sizeof(evoBuf), T(S_EVO_IN_FMT), needed - pet.level());
      evo = evoBuf;
    }
  }
  gfx->setTextColor(evoCol);
  gfx->setCursor(centerX(evo, TS(2)), UY(256));
  gfx->print(evo);

  // descuidos (retrasan la evolucion)
  char ms[24];
  snprintf(ms, sizeof(ms), T(S_MISTAKES_FMT), pet.careMistakes);
  gfx->setTextColor(pet.careMistakes > 0 ? UI_BAR_BAD : UI_INK);
  gfx->setCursor(centerX(ms, TS(2)), UY(312));
  gfx->print(ms);
}

void renderCard() {
  drawScreenBg();
  if (cardPage == 0) renderCardProfile();
  else if (cardPage == 1) renderCardStats();
  else if (cardPage == 2) renderCardMedals();
  else renderCardProgress();

  // indicador de 4 paginas + ayuda
  for (int i = 0; i < 4; i++) {
    // 4 puntos separados 26 px de diseno, centrados en la pantalla
    if (i == cardPage) gfx->fillCircle(CX - U(39) + i * U(26), UY(374), U(5), UI_INK);
    else gfx->drawCircle(CX - U(39) + i * U(26), UY(374), U(4), UI_INK);
  }
  gfx->setTextColor(UI_TRACK);
  setSize(TS(2));
  gfx->setCursor(centerX(T(S_BACK), TS(2)), UY(398));
  gfx->print(T(S_BACK));
  gfx->flush();
}

// ---------- teclado para renombrar ----------

static const char KB_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-";  // 28 + DEL + OK = 30
#define KB_COLS 6
// 6 columnas x 5 filas centradas; la fila de abajo cabe dentro del circulo
#define KB_W U(56)
#define KB_H UY(46)
#define KB_X (CX - (KB_COLS * KB_W - U(6)) / 2)  // la ultima columna no lleva el hueco de 6 a la derecha
#define KB_Y UY(140)

void openKeyboard() {
  kbOpen = true;
  strncpy(nameBuf, pet.nick, sizeof(nameBuf) - 1);
  nameBuf[sizeof(nameBuf) - 1] = 0;
  nameLen = strlen(nameBuf);
}

void renderKeyboard() {
  drawScreenBg();
  gfx->setTextColor(UI_INK);
  setSize(TS(2));
  gfx->setCursor(centerX(T(S_NAME), TS(2)), UY(56));
  gfx->print(T(S_NAME));
  // buffer actual
  gfx->fillRoundRect(U(83), UY(84), U(300), UY(40), U(8), UI_WHITE);
  gfx->drawRoundRect(U(83), UY(84), U(300), UY(40), U(8), UI_INK);
  setSize(TS(3));
  gfx->setCursor(U(95), UY(94));
  gfx->print(nameLen ? nameBuf : "_");

  for (int i = 0; i < 30; i++) {
    int x = KB_X + (i % KB_COLS) * KB_W, y = KB_Y + (i / KB_COLS) * KB_H;
    bool special = (i >= 28);
    int kw = KB_W - U(6), kh = KB_H - UY(6);  // tecla: la casilla menos 6 px de diseno de separacion
    gfx->fillRoundRect(x, y, kw, kh, U(6), special ? UI_BAR_WARN : UI_WHITE);
    gfx->drawRoundRect(x, y, kw, kh, U(6), UI_INK);
    gfx->setTextColor(UI_INK);
    setSize(TS(2));
    // texto centrado en la tecla (la fuente mide 7 px de alto por escala)
    int ty = y + (kh - 7 * TS(2)) / 2;
    if (i < 28) {
      gfx->setCursor(x + (kw - textW("W", TS(2))) / 2, ty);
      gfx->print(KB_KEYS[i]);
    } else {
      const char *lab = (i == 28) ? "<-" : "OK";
      gfx->setCursor(x + (kw - textW(lab, TS(2))) / 2, ty);
      gfx->print(lab);
    }
  }
  gfx->flush();
}

void keyboardTap(int16_t x, int16_t y) {
  // filtra ANTES de dividir: con x/y menores que KB_X/KB_Y la resta da negativo
  // y la division entera trunca hacia cero (no hacia -inf), asi que col/row
  // saldria 0 en vez de negativo y el filtro de abajo no lo detectaria
  if (x < KB_X || y < KB_Y) return;
  int col = (x - KB_X) / KB_W, row = (y - KB_Y) / KB_H;
  if (col >= KB_COLS || row >= 5) return;
  int i = row * KB_COLS + col;
  if (i >= 30) return;
  if (i == 28) {  // borrar
    if (nameLen) nameBuf[--nameLen] = 0;
  } else if (i == 29) {  // OK
    pet.rename(nameBuf);
    kbOpen = false;
  } else if (nameLen < sizeof(nameBuf) - 1) {
    nameBuf[nameLen++] = KB_KEYS[i];
    nameBuf[nameLen] = 0;
  }
}

// ---------- galeria pokedex ----------

#define GAL_X U(73)
#define GAL_Y UY(84)
#define GAL_CELL U(80)    // ancho de celda
#define GAL_CELL_Y UY(80)  // alto de celda

// dibuja una miniatura centrada en su celda; sil=true la pinta en tinta
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
  if (galleryDetail) {  // vista detalle: se redibuja siempre (animada)
    drawScreenBg();
    const DexEntry &d = DEX_TBL[galleryDetail];
    bool reg = pet.isRegistered(galleryDetail);
    char head[32];
    snprintf(head, sizeof(head), "N.%03d %s%s", galleryDetail,
             pet.isShinyRegistered(galleryDetail) ? "*" : "", reg ? dexName(galleryDetail) : "???");
    gfx->setTextColor(reg ? d.accent : UI_INK);
    // auto-encoge nombres largos (no caben a t3), midiendo en vez de contar
    // bytes; 13 caracteres a tamano 3 son los 234 px de antes.
    setSize(TS(3));
    bool big = textW(head, TS(3)) <= U(234);
    int gts = big ? TS(3) : TS(2);
    setSize(gts);
    gfx->setCursor(centerX(head, gts), UY(big ? 56 : 60));
    gfx->print(head);
    if (galleryPmd.loaded) {
      // animado y a color si esta registrado; silueta estatica si no (estilo "?")
      drawPmdActM(galleryPmd, PMD_IDLE, CX, UY(300), reg ? millis() : 0, true, !reg, SC(6));
    } else {
      const uint8_t *t = thumbs.get(galleryDetail);
      if (t) drawThumb(t, CX - GAL_CELL / 2, UY(135), SC(4), !reg);  // drawThumb centra en su celda
    }
    gfx->setTextColor(UI_INK);
    setSize(TS(2));
    gfx->setCursor(centerX(T(S_DETAIL_BACK), TS(2)), UY(408));
    gfx->print(T(S_DETAIL_BACK));
    gfx->flush();
    return;
  }

  if (!galleryDirty) return;  // la rejilla es estatica
  galleryDirty = false;

  drawScreenBg();
  char head[24];
  snprintf(head, sizeof(head), T(S_POKEDEX_FMT), pet.registeredCount());
  gfx->setTextColor(UI_INK);
  int hs = fitSize(head, TS(3), TS(2), UY(48));  // a 36 el borde de "POKEDEX" salia del circulo
  gfx->setCursor(centerX(head, hs), UY(48));
  gfx->print(head);

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int16_t dex = galleryPage * 16 + r * 4 + c + 1;
      if (dex > 151) break;
      int x = GAL_X + c * GAL_CELL, y = GAL_Y + r * GAL_CELL_Y;
      const uint8_t *t = thumbs.get(dex);
      if (t) {
        drawThumb(t, x, y, SC(2), !pet.isRegistered(dex));
        if (pet.isShinyRegistered(dex)) {
          gfx->setTextColor(UI_BAR_WARN);
          setSize(TS(2));
          gfx->setCursor(x + U(62), y + UY(4));
          gfx->print("*");
        }
      } else {
        char num[6];
        snprintf(num, sizeof(num), "%d", dex);
        gfx->setTextColor(UI_TRACK);
        setSize(TS(2));
        gfx->setCursor(x + U(24), y + UY(32));
        gfx->print(num);
      }
    }
  }
  // puntos de pagina
  for (int i = 0; i < 10; i++) {
    // 10 puntos separados 14 px de diseno, centrados en la pantalla
    if (i == galleryPage) gfx->fillCircle(CX - U(63) + i * U(14), UY(436), U(4), UI_INK);
    else gfx->drawCircle(CX - U(63) + i * U(14), UY(436), U(3), UI_INK);
  }
  gfx->flush();
}

void galleryTap(int16_t x, int16_t y) {
  if (galleryDetail) {  // volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  if (y < HEADER_ZONE_H) {  // tocar la cabecera = salir
    galleryOpen = false;
    galleryPmd.unload();
    return;
  }
  // mismo motivo que en el teclado: filtrar antes de dividir
  if (x < GAL_X || y < GAL_Y) return;
  int c = (x - GAL_X) / GAL_CELL, r = (y - GAL_Y) / GAL_CELL_Y;
  if (c > 3 || r > 3) return;
  int16_t dex = galleryPage * 16 + r * 4 + c + 1;
  if (dex > 151) return;
  galleryDetail = dex;
  galleryPmd.load(dex, pet.isShinyRegistered(dex));
}

void drawBattery() {
  int pc = batPercent();
  if (pc < 0) return;  // sin bateria conectada
  int x = CX - U(14), y = UY(12), w = U(24), h = UY(11);
  bool charging = batCharging();
  uint16_t col = charging ? UI_BAR_OK
                 : (pc >= 40) ? inkColor()
                 : (pc >= 15) ? UI_BAR_WARN
                              : UI_BAR_BAD;
  gfx->drawRoundRect(x, y, w, h, U(2), col);
  gfx->fillRect(x + w, y + UY(3), U(3), UY(5), col);  // borne
  if (charging) {
    // rayo de carga (zigzag) en vez de la barra de nivel
    uint16_t bolt = C565(0xff, 0xd9, 0x4a);
    int bx = x + w / 2;
    gfx->fillTriangle(bx + U(3), y + UY(1), bx - U(4), y + UY(6), bx + U(1), y + UY(6), bolt);
    gfx->fillTriangle(bx - U(1), y + UY(5), bx + U(4), y + UY(5), bx - U(3), y + UY(10), bolt);
  } else {
    int fw = (w - U(4)) * pc / 100;
    if (fw > 0) gfx->fillRect(x + U(2), y + UY(2), fw, h - UY(4), col);
  }
}

void drawHeader(const char *name, uint16_t nameColor, const char *msg) {
  drawBattery();
  gfx->setTextColor(nameColor);
  int hs = fitSize(name, TS(3), TS(2), UY(52));  // nombre + nivel largos no caben a tamano 3 en la parte alta del circulo
  gfx->setCursor(centerX(name, hs), UY(52) + (TS(3) * 7 - textH()) / 2);
  gfx->print(name);
  gfx->setTextColor(inkColor());
  setSize(TS(2));
  gfx->setCursor(centerX(msg, TS(2)), UY(90));
  gfx->print(msg);
}

// animacion de la ceremonia (10s): despedida = reverencia con corazones y se
// aleja caminando; escapada = se asusta y sale corriendo. Sustituye al idle.
void drawCeremony() {
  if (!pmd.loaded) { drawPet(); return; }  // respaldo si no hay sprite PMD
  uint32_t now = millis();
  float t = pet.ceremonyT();               // 0..1 a lo largo de los 10s
  bool panic = (pet.ceremony == CER_RUNAWAY);
  int x = CX, y = PET_GROUND;
  uint8_t act = PMD_IDLE;

  if (panic) {
    // final triste: penumbra azulada + lluvia
    for (int i = 0; i < 46; i++) {
      int rx = U(i * 47 + now / 3) % UI_W;
      int ry = UY(i * 91 + now / 2) % (UI_H + UY(4));
      gfx->drawLine(rx, ry, rx - U(3), ry + UY(12), C565(0x6a, 0x84, 0xb0));
    }
    bool fade = false;
    if (t < 0.30f) {                       // cabizbajo, temblando
      act = pmd.has(PMD_HURT) ? PMD_HURT : PMD_IDLE;
      x = CX + (int)(U(4) * sinf(now * 0.04f));
    } else {                               // se aleja despacio y se desvanece
      act = pmd.has(PMD_WALKL) ? PMD_WALKL : PMD_IDLE;
      x = CX - (int)(((t - 0.30f) / 0.70f) * (CX + U(120)));
      fade = (t > 0.6f) && ((now / 160) % 2 == 0);  // parpadea hacia la silueta
    }
    drawPmdAct(act, x, y, now, true, fade, SC(5));  // fade=silueta: se difumina al irse
    // lagrima cayendo del bicho
    if (t < 0.55f) {
      int ty = y - UY(150) + UY((int)((now / 6) % 40));
      gfx->fillRect(x + U(6), ty, SC(3), UY(6), C565(0x9a, 0xc4, 0xe8));
    }
    return;
  }

  // despedida epica: halo dorado pulsante + chispas y corazones que ascienden
  int gcy = PET_GROUND - UY(96);
  for (int k = 0; k < 4; k++) {
    int r = U(60 + k * 34) + (int)(U(10) * sinf(now * 0.02f));
    gfx->drawCircle(CX, gcy, r, C565(0xff, 0xdf, 0x8a));
  }
  for (int i = 0; i < 16; i++) {
    int px = U(i * 71 + 28) % UI_W;
    int py = UY(410) - UY((int)((now / 8 + i * 70) % 360));   // suben y reaparecen abajo
    if (py < UY(30)) continue;
    if (i % 4 == 0) drawMap(SPR_HEART, 32, px - U(8), py - UY(8), SC(1), false);  // corazoncito
    else gfx->fillRect(px, py, SC(4), SC(4), (i % 2) ? C565(0xff, 0xe7, 0x9f) : C565(0xff, 0x9a, 0xc0));
  }

  if (t < 0.45f) {                         // reverencia / pose de despedida
    act = pmd.has(PMD_POSE) ? PMD_POSE : (pmd.has(PMD_NOD) ? PMD_NOD : PMD_IDLE);
  } else {                                 // se aleja por la derecha
    act = pmd.has(PMD_WALKR) ? PMD_WALKR : PMD_IDLE;
    x = CX + (int)(((t - 0.45f) / 0.55f) * (CX + U(140)));
  }
  drawPmdAct(act, x, y, now, true, false, SC(5));
  if (pet.showHeart())                     // corazon grande siguiendo al bicho
    drawMap(SPR_HEART, 32, x + U(50), y - UY(190), SC(2), false);
}

// dialogo de decision (2 botones apilados): evolucionar/mantener o despedirse/quedaros
void drawChoiceDialog() {
  const char *q, *o1, *o2;
  uint16_t c1, c2, t1, t2;
  if (choiceKind == 1) {  // evolucion
    q = T(S_EVO_Q); o1 = T(S_EVO_TAP); o2 = T(S_EVO_KEEP);
    c1 = UI_BAR_BAD; t1 = UI_WHITE; c2 = UI_TRACK; t2 = UI_INK;
  } else {                // despedida
    q = T(S_FAR_Q); o1 = T(S_FAR_GO); o2 = T(S_FAR_STAY);
    c1 = UI_BAR_WARN; t1 = UI_INK; c2 = UI_BAR_OK; t2 = UI_WHITE;
  }
  gfx->fillRoundRect(U(73), UY(156), U(320), UY(188), U(16), UI_WHITE);
  gfx->drawRoundRect(U(73), UY(156), U(320), UY(188), U(16), UI_INK);
  gfx->setTextColor(UI_INK);
  setSize(TS(2));
  gfx->setCursor(centerX(q, TS(2)), UY(176));
  gfx->print(q);
  gfx->fillRoundRect(CHOICE_X, CHOICE_B1_Y, CHOICE_W, CHOICE_BH, U(12), c1);     // boton accion
  gfx->setTextColor(t1);
  gfx->setCursor(centerX(o1, TS(2)), UY(224));
  gfx->print(o1);
  gfx->fillRoundRect(CHOICE_X, CHOICE_B2_Y, CHOICE_W, CHOICE_BH, U(12), c2);     // boton mantener/quedaros
  gfx->setTextColor(t2);
  gfx->setCursor(centerX(o2, TS(2)), UY(286));
  gfx->print(o2);
}

// boton-CTA rojo y grande para evolucionar (pulsa para llamar la atencion)
void drawEvolveButton() {
  uint32_t now = millis();
  int p = (int)(U(5) * sinf(now * 0.006f));  // late: -5..5
  int x = EVO_BTN_X - p, y = EVO_BTN_Y - p, w = EVO_BTN_W + 2 * p, h = EVO_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, U(18), UI_BAR_BAD);
  gfx->drawRoundRect(x, y, w, h, U(18), UI_WHITE);
  gfx->drawRoundRect(x + U(2), y + UY(2), w - U(4), h - UY(4), U(16), UI_WHITE);
  gfx->setTextColor(UI_WHITE);
  setSize(TS(3));
  const char *t = T(S_EVO_TAP);
  gfx->setCursor(centerX(t, TS(3)), y + h / 2 - UY(11));
  gfx->print(t);
}

// boton-CTA dorado de despedida: "<nombre> quiere decirte algo..."
void drawFarewellButton() {
  uint32_t now = millis();
  int p = (int)(U(4) * sinf(now * 0.005f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, U(16), UI_BAR_WARN);
  gfx->drawRoundRect(x, y, w, h, U(16), UI_INK);
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  snprintf(buf, sizeof(buf), T(S_FAREWELL_BTN), nm);
  gfx->setTextColor(UI_INK);
  drawWrapped(buf, TS(2), CX, y + h / 2, FAR_BTN_W - U(16), 2, false);  // con nombres largos pasa a dos lineas
}

// boton-CTA sombrio de escapada por abandono: "<nombre> se siente abandonado..."
// (final triste: azul-gris oscuro, latido lento y apagado)
void drawRunawayButton() {
  uint32_t now = millis();
  int p = (int)(U(3) * sinf(now * 0.003f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, U(16), C565(0x3a, 0x44, 0x5a));
  gfx->drawRoundRect(x, y, w, h, U(16), C565(0x70, 0x80, 0x98));
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  snprintf(buf, sizeof(buf), T(S_RUNAWAY_BTN), nm);
  gfx->setTextColor(C565(0xc8, 0xd2, 0xe0));
  drawWrapped(buf, TS(2), CX, y + h / 2, FAR_BTN_W - U(16), 2, false);
}

// animacion epica de evolucion: halo radial + rayos giratorios + parpadeo del
// sprite acelerando + chispas que salen disparadas + fogonazo final
void drawEvolveFX(uint32_t now) {
  float t = pet.evolveT();          // 0..1
  int cx = CX, cy = PET_GROUND - UY(96);

  // halo radial que crece y pulsa
  int halo = U(36) + (int)(t * U(150)) + (int)(U(8) * sinf(now * 0.02f));
  for (int k = 0; k < 4; k++) {
    int r = halo - U(k * 7);
    if (r > 0) gfx->drawCircle(cx, cy, r, UI_WHITE);
  }
  // rayos giratorios desde el centro del bicho
  float base = now * 0.004f;
  for (int i = 0; i < 12; i++) {
    float a = base + i * (float)(PI / 6);
    int len = U(90) + (int)(U(70) * (0.5f + 0.5f * sinf(now * 0.012f + i)));
    gfx->drawLine(cx, cy, cx + (int)(cosf(a) * len), cy + (int)(sinf(a) * len), UI_WHITE);
  }
  // parpadeo entre la forma ANTERIOR y la NUEVA (siluetas), acelerando; al
  // final (t>0.9) se queda fija en la nueva para el fogonazo de revelado
  int period = 60 + (int)(220 * (1.0f - t));
  bool showOld = t < 0.9f && evoPmd.loaded && ((now / period) % 2) == 0;
  if (showOld) drawPmdActM(evoPmd, PMD_IDLE, cx, PET_GROUND, 0, true, true, SC(5));
  else drawPmdAct(PMD_IDLE, cx, PET_GROUND, 0, true, true, SC(5));
  // chispas que salen disparadas
  for (int i = 0; i < 10; i++) {
    float a = i * (float)(PI / 5) + t * 4.0f;
    int d = (int)(U(now / 14 + i * 33) % U(200));
    int sx = cx + (int)(cosf(a) * d), sy = cy + (int)(sinf(a) * d);
    int q = SC(5);  // chispa cuadrada centrada en (sx, sy)
    gfx->fillRect(sx - q / 2, sy - q / 2, q, q, (i & 1) ? C565(0xff, 0xe0, 0x70) : UI_WHITE);
  }
  // fogonazo final antes de revelar la forma nueva
  if (t > 0.9f) gfx->fillCircle(cx, cy, (int)(U(300) * (t - 0.9f) / 0.1f), UI_WHITE);
}

// Carga el sprite de un vecino por pase (cada lectura de la SD tarda); suelta los huecos que
// se vaciaron o cambiaron de bicho.
void ensureNeighbors() {
  const NbEntry *sh[NB_MAX_SHOWN];
  int n = nearbyShown(sh);
  for (int i = 0; i < NB_MAX_SHOWN; i++) {
    NbSlot want = i < n ? NbSlot{ sh[i]->id, sh[i]->species, sh[i]->shiny } : NbSlot{ 0, 0, false };
    NbSlot &have = nbSlot[i];
    if (want.id == have.id && want.species == have.species && want.shiny == have.shiny) continue;
    if (want.id) {  // como mucho una carga por segundo: un desconocido que se invente ids no satura la SD
      static uint32_t lastLoad = 0;
      uint32_t now = millis();
      if (!sdReady || (lastLoad && now - lastLoad < 1000)) continue;
      lastLoad = now;
    }
    nbPmd[i].unload();
    have = want;
    if (want.id) {
      nbPmd[i].load(want.species, want.shiny);
      return;  // uno por pase
    }
  }
}

// Los vecinos se quedan quietos detras y a los lados del bicho, mas pequenos y mas arriba, como
// un grupo: dos delante (a los lados) y dos al fondo (entre medias, mas altos y chicos).
void drawNeighbors() {
  const NbEntry *sh[NB_MAX_SHOWN];
  int n = nearbyShown(sh);
  struct Pos { int16_t dx, up; uint8_t s; };
  const Pos pos[NB_MAX_SHOWN] = { { -135, 14, 3 }, { 135, 14, 3 }, { -70, 34, 2 }, { 70, 34, 2 } };
  const uint8_t order[NB_MAX_SHOWN] = { 2, 3, 0, 1 };  // primero el fondo
  uint32_t now = millis();
  for (uint8_t i : order) {
    if (i >= n) continue;
    PmdMon &m = nbPmd[i];
    if (!m.loaded || nbSlot[i].id != sh[i]->id) continue;
    uint8_t act = sh[i]->sleeping && m.has(PMD_SLEEP) ? PMD_SLEEP : PMD_IDLE;
    uint32_t t = now + (sh[i]->id % 997) * 13;  // cada uno en su momento de la animacion
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
    // sin SD y sin sprite de flash: aviso claro de que faltan sprites
    gfx->setTextColor(inkColor());
    setSize(TS(6));
    gfx->setCursor(CX - 3 * TS(6), PET_CY - UY(80));  // "?" mide 6 px por escala: la mitad es 3 * escala
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

  // animacion de evolucion: alterna la silueta de la forma anterior y la nueva
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
  if (m == MOOD_HAPPY && (millis() / 500) % 2) y -= UY(6);  // saltito

  drawMap(sp.sprite, SPRITE_H, x, y, s, false);

  // expresiones superpuestas usando las anclas de la especie
  bool blink = (millis() % 3500 < 300);
  if (m == MOOD_SLEEPING || blink) {
    overlayEye(sp, x, y, s, sp.eyeColL);
    overlayEye(sp, x, y, s, sp.eyeColR);
  }
  if (m == MOOD_EATING) overlayMouth(sp, x, y, s, true);
  else if (m == MOOD_SAD) overlayMouth(sp, x, y, s, false);

  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + 20 * s, y - 2 * s, SC(2), false);
}

// ---------- escena de bano ----------

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
      // pose de alegria al quedar limpio
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
    // espuma: pompas meciendose y subiendo poco a poco
    float t = now / 220.0f;
    for (auto &b : bubbles) {
      int bx = b.x + (int)(sinf(t + b.ph) * U(6));
      int by = b.y - UY((int)((3000 - left) / 90));
      gfx->fillCircle(bx, by, b.r, UI_WHITE);
      gfx->drawCircle(bx, by, b.r, 0x7E3D);
      gfx->fillCircle(bx - b.r / 3, by - b.r / 3, b.r / 4, UI_BG_DAY);
    }
  } else {
    // las pompas revientan: destellos
    for (int i = 0; i < 8; i++) {
      auto &b = bubbles[i];
      int sx = b.x + U((i % 3) * 6 - 6), sy = b.y - UY(18);
      uint16_t col = (i % 2) ? UI_BAR_WARN : UI_WHITE;
      int th = SC(3), len = U(13);  // cruz centrada en (sx, sy)
      gfx->fillRect(sx - len / 2, sy - th / 2, len, th, col);
      gfx->fillRect(sx - th / 2, sy - len / 2, th, len, col);
    }
  }
}

// ---------- mascota PMD: comportamiento ----------

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
  // guarda: nunca mas vueltas que frames, por si un .bin corrupto trae ms[]=0
  // pese al saneo de la carga (defensa en profundidad)
  for (uint8_t guard = 0; guard < a.frames && t >= a.ms[i]; guard++) {
    t -= a.ms[i];
    i = (i + 1) % a.frames;
  }
  return i;
}

// dibuja una accion anclada por la base (centro-x, suelo) y devuelve su escala
// dibuja una accion de un PmdMon concreto (m); drawPmdAct usa el global pmd
void drawPmdActM(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  const PmdAct &a = m.acts[actId];
  if (!a.frames) return;
  int sBase = m.acts[PMD_IDLE].h ? UY(170) / m.acts[PMD_IDLE].h : SC(5);
  if (sBase < SC(2)) sBase = SC(2);
  if (sBase > maxS) sBase = maxS;
  int s = sBase;
  while (s > SC(2) && a.h * s > UY(250)) s--;  // acciones con frame grande (ataque)
  uint8_t fi = pmdFrameAt(a, t, loop);
  const uint8_t *fr = a.data + (uint32_t)fi * a.w * a.h;
  // anclar por los pies (a.base), no por el alto del lienzo: asi las acciones
  // con padding distinto (Hurt, Eat...) quedan todas a la misma altura de suelo
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

// elige el siguiente capricho del bicho cuando esta contento
void behNext() {
  uint32_t now = millis();
  beh.t0 = now;
  int r = random(100);
  if (r < 35 && (pmd.has(PMD_WALKL) || pmd.has(PMD_WALKR))) {
    beh.mode = 1;  // paseo
    beh.targetX = U(150) + random(U(176));
    beh.until = now + 15000;
  } else if (r < 60) {
    // gesto aleatorio entre los disponibles
    // (Hop fuera: salta demasiado alto; Sit fuera: mira hacia atras)
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
    beh.mode = 0;  // mirar al frente
    beh.until = now + 2000 + random(3000);
  }
}

void drawPetPMD() {
  uint32_t now = millis();

  if (pet.evolving()) {
    drawEvolveFX(now);
    return;
  }
  if (evoPmd.loaded) evoPmd.unload();  // termino la evolucion: libera la forma anterior

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
    // contento: el planificador decide (idle / paseo / gesto)
    if (now > beh.until) behNext();
    if (beh.mode != 1) walkLast = 0;  // al volver a caminar no se cuenta el tiempo parado
    if (beh.mode == 1) {
      float d = beh.targetX - beh.x;
      if (fabsf(d) < 4.0f * UI_F) {
        behNext();
        act = PMD_IDLE;
      } else {
        // 30 px/s de diseno (3 px por frame a 10 fps), midiendo el tiempo real: con menos frames
        // por segundo camina igual de rapido
        uint32_t dt = walkLast ? now - walkLast : 100;
        uint32_t cap = frameIntervalMs(powerCtx(now)) + 100;  // un hueco mayor (otra pantalla de por medio) no es un paso largo
        if (dt > cap) dt = cap;
        walkLast = now;
        float step = 30.0f * UI_F * dt / 1000.0f;
        if (step > fabsf(d)) step = fabsf(d);  // un frame lento no sobrepasa el destino
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

// sprite animado desde la SD: zoom entero por pixel, frames a su ritmo
void drawPetSD() {
  int s = UY(200) / mon.h;  // como en la carga (200 / alto), pero con el escenario escalado
  if (s < SC(2)) s = SC(2);
  if (s > SC(5)) s = SC(5);
  int w = mon.w * s, h = mon.h * s;
  int x = CX - w / 2;
  int y = PET_CY - h / 2;

  bool sil = false;
  if (pet.evolving()) {
    sil = (millis() / 300) % 2;
  } else if (pet.mood() == MOOD_HAPPY && (millis() / 500) % 2) {
    y -= UY(6);  // saltito
  }

  uint16_t fm = mon.frameMs ? mon.frameMs : 100;
  uint16_t fi = pet.sleeping ? 0 : (millis() / fm) % mon.frames;
  const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
  for (int r = 0; r < mon.h; r++) {
    const uint8_t *row = fr + r * mon.w;
    forRuns(mon.w, [&](int c) { return indexedKey(row[c], sil); },
            [&](int c0, int len, int k) { gfx->fillRect(x + c0 * s, y + r * s, len * s, s, sil ? INK_K : mon.pal[k]); });
  }

  // emotes en vez de expresiones (los sprites importados no tienen anclas)
  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + w - U(30), y - UY(50), SC(2), false);
}

// ojo cerrado: borra el ojo 3x4 y dibuja el parpado
void overlayEye(const Species &sp, int x, int y, int s, int col) {
  gfx->fillRect(x + col * s, y + sp.eyeRow * s, 3 * s, 4 * s, sp.bodyColor);
  gfx->fillRect(x + col * s, y + (sp.eyeRow + 2) * s, 3 * s, s, INK_K);
}

// borra la sonrisa base y pinta boca abierta (comer) o ceno (triste)
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

void drawBars() {
  drawBar(U(78), UY(318), T(S_BAR_FOOD), pet.fullness);
  drawBar(U(244), UY(318), T(S_BAR_JOY), pet.joy);
  drawBar(U(78), UY(346), T(S_BAR_ENE), pet.energy);
  drawBar(U(244), UY(346), T(S_BAR_HYG), pet.hygiene);
}

void drawBar(int x, int y, const char *label, uint8_t val) {
  gfx->setTextColor(inkColor());
  setSize(TS(2));
  gfx->setCursor(x, y);
  gfx->print(label);
  int gap = U(48);  // etiqueta de 4 letras a escala 2 y un poco de aire
  // las dos columnas empiezan en 78 y 244: la barra de la primera tiene que
  // acabar antes de la etiqueta de la segunda, y ambas se pintan igual de anchas
  int bw = U(232) - (U(78) + gap);
  if (bw > U(100)) bw = U(100);
  int bx = x + gap, bh = UY(15);
  uint16_t fill = (val >= 50) ? UI_BAR_OK : (val >= 25) ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillRoundRect(bx, y, bw, bh, U(4), UI_TRACK);
  int fw = (bw - U(4)) * val / 100;
  if (fw > 0) gfx->fillRoundRect(bx + U(2), y + UY(2), fw, bh - UY(4), U(3), fill);
}

void drawButtons() {
  for (int i = 0; i < 4; i++) {
    bool off = pet.sleeping && i != 2;  // durmiendo solo funciona LUZ
    int bx = buttons[i].cx - BTN_HALF, by = buttons[i].cy - BTN_HALF;
    if (!pet.sleeping) gfx->fillRoundRect(bx, by, 2 * BTN_HALF, 2 * BTN_HALF, U(14), UI_WHITE);
    gfx->drawRoundRect(bx, by, 2 * BTN_HALF, 2 * BTN_HALF, U(14), inkColor());
    if (!off) {
      int is = SC(2);  // icono 16x16 a escala entera, centrado en el boton
      drawMap(buttons[i].icon, 16, buttons[i].cx - 8 * is, buttons[i].cy - 8 * is, is, false);
    }
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
  if (bathUntil) return "Splish splash!";  // onomatopeya universal
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

// dibuja un mapa de n x n pixeles escalado; silhouette=true lo pinta en tinta
void drawMap(const char *const *map, int n, int x, int y, int s, bool silhouette) {
  for (int r = 0; r < n; r++) {
    const char *row = map[r];
    forRuns(n, [&](int c) { return charKey(row[c], silhouette); },
            [&](int c0, int len, int k) { gfx->fillRect(x + c0 * s, y + r * s, len * s, s, silhouette ? INK_K : spriteColor((char)k)); });
  }
}
