// Hallen-Legenden Mini – Geräteseite für Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2)
// Display CO5300 (QSPI), Touch CST816 (I²C 0x15), IMU QMI8658 (0x6B), IO-Expander TCA9554 (0x20), Codec ES8311 (0x18).
//
// Aufteilung:  Kern 1 = Spiel (30 fps, loop())
//              Kern 0 = Display-Task (2× hochskalieren und per QSPI schieben), Eingabe-Task (Touch/IMU, 100 Hz), Audio-Task
#include <Arduino.h>
#include <ESP_I2S.h>
#include <Preferences.h>
#include <Wire.h>

#include "Arduino_GFX_Library.h"
#include "es8311.h"
#include "game.h"

// ---------------- Pins ----------------
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 11
#define LCD_CS 12
#define LCD_W 368
#define LCD_H 448
#define IIC_SDA 15
#define IIC_SCL 14
#define I2S_MCK 16
#define I2S_BCK 9
#define I2S_DI 10
#define I2S_WS 45
#define I2S_DO 8
#define PA_EN 46
#define BOOT_PIN 0

// Lage der IMU auf der Platine → Bildschirmachsen (x rechts, y oben, z aus dem Bildschirm).
// Werte über die serielle Ausgabe „imu“ prüfen und hier anpassen.
#define IMU_X_SRC 0   // 0 = Sensor-x, 1 = Sensor-y, 2 = Sensor-z
#define IMU_X_SIGN 1
#define IMU_Y_SRC 1
#define IMU_Y_SIGN 1
#define IMU_Z_SRC 2
#define IMU_Z_SIGN 1

using namespace hl;

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static Arduino_DataBus* bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
static Arduino_CO5300* gfx = new Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, LCD_W, LCD_H, 16, 0, 0, 0);
static I2SClass i2s;

// ---------------- Speicher (NVS) ----------------
struct NvsStore : Store {
  Preferences prefs;
  void begin() { prefs.begin("hlmini", false); }
  int get(const char* k, int def) override { return prefs.getInt(k, def); }
  void set(const char* k, int v) override { prefs.putInt(k, v); }
};

static Synth synth;
static NvsStore store;
static Game* game;

// ---------------- I²C-Helfer ----------------
static bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// ---------------- Eingabe (Kern 0) ----------------
static portMUX_TYPE inMux = portMUX_INITIALIZER_UNLOCKED;
static TouchSample touchQ[32];
static int touchN = 0;
static float accX = 0, accY = 0, accZ = 1;
static float rawAcc[3] = {0, 0, 1};
static bool imuOk = false;
static volatile uint32_t touchOk = 0, touchFail = 0, touchDown = 0;
static volatile uint8_t touchRaw[5];

static void inputTask(void*) {
  bool wasDown = false;
  int16_t lx = 0, ly = 0;
  uint32_t lastImu = 0;
  for (;;) {
    uint8_t t[5];
    bool down = false;
    int16_t x = lx, y = ly;
    if (i2cRead(0x15, 0x02, t, 5)) {
      touchOk++;
      for (int i = 0; i < 5; i++) touchRaw[i] = t[i];
      down = (t[0] & 0x0F) > 0;
      if (down) touchDown++;
      if (down) {
        x = ((t[1] & 0x0F) << 8) | t[2];
        y = ((t[3] & 0x0F) << 8) | t[4];
      }
    } else touchFail++;
    if (down || wasDown) {
      TouchSample s{down, int16_t(x / 2), int16_t(y / 2), millis()};
      portENTER_CRITICAL(&inMux);
      if (touchN < 32) touchQ[touchN++] = s;
      else touchQ[31] = s;
      portEXIT_CRITICAL(&inMux);
      wasDown = down;
      lx = x; ly = y;
    }
    if (imuOk && millis() - lastImu >= 20) {
      lastImu = millis();
      uint8_t d[6];
      if (i2cRead(0x6B, 0x35, d, 6)) {
        float v[3];
        for (int i = 0; i < 3; i++) v[i] = int16_t(d[2 * i] | (d[2 * i + 1] << 8)) / 8192.f;   // ±4 g
        portENTER_CRITICAL(&inMux);
        rawAcc[0] = v[0]; rawAcc[1] = v[1]; rawAcc[2] = v[2];
        accX = IMU_X_SIGN * v[IMU_X_SRC];
        accY = IMU_Y_SIGN * v[IMU_Y_SRC];
        accZ = IMU_Z_SIGN * v[IMU_Z_SRC];
        portEXIT_CRITICAL(&inMux);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(8));
  }
}

// ---------------- Display (Kern 0) ----------------
// Das Spiel zeichnet in einen internen Puffer; fertige Bilder werden in einen PSRAM-Puffer kopiert,
// den der Display-Task 2× hochskaliert und bandweise per QSPI-DMA ans Panel schiebt.
static uint16_t* fbGame;                 // Spiel zeichnet hier (internes RAM, schnell)
static uint16_t* fbFront;                // fertiges Bild für den Display-Task (PSRAM)
static SemaphoreHandle_t frameReady, frameFree;
static uint16_t* band;
static const int BAND_ROWS = 10;         // 368×10×2 = 7360 Byte, passt in eine QSPI-Übertragung
static volatile uint32_t pushUs = 0;
static volatile int brightness = 210, curBrightness = -1;

static void displayTask(void*) {
  for (;;) {
    xSemaphoreTake(frameReady, portMAX_DELAY);
    uint32_t t0 = micros();
    gfx->startWrite();
    gfx->writeAddrWindow(0, 0, LCD_W, LCD_H);
    for (int by = 0; by < LCD_H; by += BAND_ROWS) {
      for (int r = 0; r < BAND_ROWS; r += 2) {
        const uint16_t* s = fbFront + ((by + r) / 2) * W;
        uint32_t* d = reinterpret_cast<uint32_t*>(band + r * LCD_W);
        for (int x = 0; x < W; x++) {
          uint32_t p = __builtin_bswap16(s[x]);   // Panel erwartet Big Endian
          d[x] = p | (p << 16);
        }
        memcpy(band + (r + 1) * LCD_W, band + r * LCD_W, LCD_W * 2);
      }
      bus->writeBytes(reinterpret_cast<uint8_t*>(band), LCD_W * BAND_ROWS * 2);
    }
    gfx->endWrite();
    pushUs = micros() - t0;
    if (curBrightness != brightness) { curBrightness = brightness; gfx->setBrightness(curBrightness); }
    xSemaphoreGive(frameFree);
    vTaskDelay(1);   // Leerlauf-Task auf Kern 0 bedienen (Watchdog)
  }
}

// ---------------- Audio (Kern 0) ----------------
static void audioTask(void*) {
  static int16_t mono[256];
  static int16_t stereo[512];
  for (;;) {
    synth.render(mono, 256);
    for (int i = 0; i < 256; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[i];
    i2s.write(reinterpret_cast<uint8_t*>(stereo), sizeof stereo);
  }
}

static bool audioInit() {
  pinMode(PA_EN, OUTPUT);
  digitalWrite(PA_EN, HIGH);
  i2s.setPins(I2S_BCK, I2S_WS, I2S_DO, I2S_DI, I2S_MCK);
  if (!i2s.begin(I2S_MODE_STD, Synth::RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) return false;
  es8311_handle_t es = es8311_create(0, ES8311_ADDRRES_0);
  if (!es) return false;
  const es8311_clock_config_t clk = {false, false, true, Synth::RATE * 256, Synth::RATE};
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
  es8311_voice_volume_set(es, 70, nullptr);
  es8311_microphone_config(es, false);
  return true;
}

// ---------------- Start ----------------
void setup() {
  Serial.begin(115200);
  // Liest am Rechner niemand mit, läuft der USB-Puffer voll und jedes print blockiert (gemessen: bis 6 s Stillstand).
  // Ohne Timeout gehen überzählige Zeilen einfach verloren.
  Serial.setTxTimeoutMs(0);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  Wire.begin(IIC_SDA, IIC_SCL, 400000);

  // IO-Expander: Display, Touch und Co. aus dem Reset holen
  i2cWriteReg(0x20, 0x03, 0xF8);   // P0..P2 als Ausgang
  i2cWriteReg(0x20, 0x01, 0x00);
  delay(20);
  i2cWriteReg(0x20, 0x01, 0x07);
  delay(60);

  if (!gfx->begin(80000000)) Serial.println("Display-Init fehlgeschlagen");
  gfx->fillScreen(0x0000);
  gfx->setBrightness(0);

  // Touch: nicht automatisch schlafen
  i2cWriteReg(0x15, 0xFE, 0x01);

  // IMU: Softreset, dann Beschleunigung ±4 g, ~250 Hz
  uint8_t who = 0;
  i2cWriteReg(0x6B, 0x60, 0xB0);
  delay(20);
  if (i2cRead(0x6B, 0x00, &who, 1) && who == 0x05) {
    i2cWriteReg(0x6B, 0x02, 0x40);   // CTRL1: Adressen automatisch weiterzählen, Little Endian
    i2cWriteReg(0x6B, 0x03, 0x15);   // CTRL2: ±4 g, 250 Hz
    i2cWriteReg(0x6B, 0x08, 0x01);   // CTRL7: Beschleunigung an
    delay(30);
    imuOk = true;
    for (int i = 0; i < 4; i++) {
      uint8_t r[16] = {};
      bool ok = i2cRead(0x6B, 0x2D, r, 14);
      Serial.printf("imu-diag ok=%d st=%02X %02X  ax=%d ay=%d az=%d  ctrl:", ok, r[0], r[1], int16_t(r[8] | r[9] << 8), int16_t(r[10] | r[11] << 8), int16_t(r[12] | r[13] << 8));
      for (int k = 2; k <= 8; k++) { uint8_t v = 0; i2cRead(0x6B, k, &v, 1); Serial.printf(" %02X", v); }
      Serial.println();
      delay(50);
    }
  }
  Serial.printf("IMU %s (WHO_AM_I=0x%02X)\n", imuOk ? "ok" : "fehlt", who);

  bool audio = audioInit();
  Serial.printf("Audio %s\n", audio ? "ok" : "fehlt");

  store.begin();
  game = new Game(synth, store, esp_random());

  fbGame = static_cast<uint16_t*>(heap_caps_malloc(W * H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  fbFront = static_cast<uint16_t*>(heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM));
  band = static_cast<uint16_t*>(heap_caps_malloc(LCD_W * BAND_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!fbGame || !fbFront || !band) Serial.println("Zu wenig RAM für die Bildpuffer");
  frameReady = xSemaphoreCreateBinary();
  frameFree = xSemaphoreCreateBinary();
  xSemaphoreGive(frameFree);

  xTaskCreatePinnedToCore(displayTask, "display", 4096, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(inputTask, "input", 4096, nullptr, 4, nullptr, 0);
  if (audio) xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 5, nullptr, 0);
}

// Serieller Befehl „log“: Zwei-Sekunden-Protokoll ein/aus (startet aus)
static bool logOn = false;
static void pollSerial() {
  static char line[32];
  static int n = 0;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < int(sizeof line) - 1) line[n++] = c; continue; }
    line[n] = 0;
    n = 0;
    if (!strcmp(line, "log")) { logOn = !logOn; Serial.printf("protokoll %s\n", logOn ? "an" : "aus"); }
  }
}

void loop() {
  static uint32_t next = millis();
  static uint32_t lastLog = 0, frames = 0, workUs = 0;
  static Input in;

  pollSerial();

  uint32_t t0 = micros();
  portENTER_CRITICAL(&inMux);
  in.nTouch = touchN;
  memcpy(in.touch, touchQ, sizeof(TouchSample) * touchN);
  touchN = 0;
  in.ax = accX; in.ay = accY; in.az = accZ;
  portEXIT_CRITICAL(&inMux);
  in.boot = digitalRead(BOOT_PIN) == LOW;
  in.ms = millis();

  game->tick(in);
  game->render(fbGame);
  workUs += micros() - t0;
  brightness = game->wantsDim() ? 30 : 210;

  xSemaphoreTake(frameFree, portMAX_DELAY);
  memcpy(fbFront, fbGame, W * H * 2);
  xSemaphoreGive(frameReady);

  frames++;
  if (millis() - lastLog > 2000 && !logOn) { lastLog = millis(); frames = 0; workUs = 0; }
  if (millis() - lastLog > 2000) {
    Serial.printf("touch ok %lu fail %lu down %lu raw %02X %02X %02X %02X %02X\n", (unsigned long)touchOk, (unsigned long)touchFail, (unsigned long)touchDown,
                  touchRaw[0], touchRaw[1], touchRaw[2], touchRaw[3], touchRaw[4]);
    Serial.printf("geste: %s\n", game->debugInfo());
    Serial.printf("fps %.1f  spiel %lu us  display %lu us  imu %.2f %.2f %.2f  heap %u psram %u\n", frames * 1000.f / (millis() - lastLog),
                  (unsigned long)(workUs / frames), (unsigned long)pushUs, rawAcc[0], rawAcc[1], rawAcc[2], ESP.getFreeHeap(), ESP.getFreePsram());
    lastLog = millis(); frames = 0; workUs = 0;
  }

  next += 33;
  int32_t wait = int32_t(next - millis());
  if (wait > 0) delay(wait);
  else next = millis();
}
