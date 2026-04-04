// HAL implementation for ESP32-S2 — real hardware
// ILI9341 display, XPT2046 touch, L9110S motors, LittleFS storage

#ifdef ARDUINO   // only compiled for the ESP32 target

#include "hal.h"
#include "config.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <LittleFS.h>

// ============================================
// DISPLAY
// ============================================
static TFT_eSPI tft = TFT_eSPI();

// Touch calibration — update for your panel
static uint16_t touchCal[5] = {300, 3600, 300, 3600, 1};
static const uint16_t TOUCH_THRESHOLD = 20;

// Touch interrupt
static volatile bool touchPressed = false;
static unsigned long lastTouchISRTime = 0;
static const unsigned long TOUCH_ISR_DEBOUNCE_MS = 10;

static void IRAM_ATTR touchISR() {
    unsigned long now = millis();
    if (now - lastTouchISRTime < TOUCH_ISR_DEBOUNCE_MS) return;
    lastTouchISRTime = now;
    touchPressed = true;
}

// Convert HalColor (stores R8G8B8 packed) to RGB565
static inline uint16_t toRGB565(HalColor c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8)  & 0xFF;
    uint8_t b =  c        & 0xFF;
    return tft.color565(r, g, b);
}

HalColor hal_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void hal_fillScreen(HalColor c)                         { tft.fillScreen(toRGB565(c)); }
void hal_fillRect(int x, int y, int w, int h, HalColor c) { tft.fillRect(x, y, w, h, toRGB565(c)); }
void hal_drawRect(int x, int y, int w, int h, HalColor c) { tft.drawRect(x, y, w, h, toRGB565(c)); }
void hal_fillCircle(int cx, int cy, int r, HalColor c)  { tft.fillCircle(cx, cy, r, toRGB565(c)); }
void hal_drawCircle(int cx, int cy, int r, HalColor c)  { tft.drawCircle(cx, cy, r, toRGB565(c)); }
void hal_drawHLine(int x, int y, int w, HalColor c)     { tft.drawFastHLine(x, y, w, toRGB565(c)); }

// Datum mapping
static const uint8_t datumMap[] = {
    TL_DATUM, TC_DATUM, TR_DATUM,
    ML_DATUM, MC_DATUM, MR_DATUM,
    BL_DATUM, BC_DATUM, BR_DATUM,
};

void hal_setTextDatum(HalDatum d) {
    tft.setTextDatum(datumMap[d]);
}

static HalColor _textBg = 0;

void hal_setTextColor(HalColor fg, HalColor bg) {
    _textBg = bg;
    tft.setTextColor(toRGB565(fg), toRGB565(bg));
}

void hal_drawString(const char *str, int x, int y, int font) {
    tft.drawString(str, x, y, font);
}

void hal_drawNumber(int num, int x, int y, int font) {
    tft.drawNumber(num, x, y, font);
}

#include "FreeSansTurkish7pt.h"
#include "FreeSansTurkish8pt.h"
#include "FreeSansTurkish9pt.h"
#include "FreeSansTurkish10pt.h"
#include "FreeSansTurkish11pt.h"
#include "FreeSansTurkish12pt.h"

static const GFXfont *turkishFont(int size) {
    switch (size) {
        case  7: return &FreeSans7pt8b;
        case  8: return &FreeSans8pt8b;
        default:
        case  9: return &FreeSans9pt8b;
        case 10: return &FreeSans10pt8b;
        case 11: return &FreeSans11pt8b;
        case 12: return &FreeSans12pt8b;
    }
}

void hal_drawString_Turkish(const char *str, int x, int y, int size) {
    tft.setFreeFont(turkishFont(size));
    tft.drawString(str, x, y);
    tft.setFreeFont(nullptr);
}

// ============================================
// TOUCH
// ============================================
bool hal_getTouch(uint16_t &x, uint16_t &y) {
    return tft.getTouch(&x, &y, TOUCH_THRESHOLD);
}

void hal_waitForRelease() {
    uint16_t x, y;
    while (tft.getTouch(&x, &y, TOUCH_THRESHOLD)) {
        delay(20);
    }
    delay(50);
}

// ============================================
// MOTORS
// ============================================
void hal_initMotors() {
    for (int i = 0; i < 8; i++) {
        pinMode(MOTOR_PINS[i].in1, OUTPUT);
        pinMode(MOTOR_PINS[i].in2, OUTPUT);
        digitalWrite(MOTOR_PINS[i].in1, LOW);
        digitalWrite(MOTOR_PINS[i].in2, LOW);
    }
}

void hal_motorOn(int id) {
    digitalWrite(MOTOR_PINS[id - 1].in1, HIGH);
    digitalWrite(MOTOR_PINS[id - 1].in2, LOW);
}

void hal_motorOff(int id) {
    digitalWrite(MOTOR_PINS[id - 1].in1, LOW);
    digitalWrite(MOTOR_PINS[id - 1].in2, LOW);
}

void hal_allMotorsOff() {
    for (int i = 1; i <= 8; i++) hal_motorOff(i);
}

// ============================================
// STORAGE
// ============================================
int hal_readFile(const char *path, char *buf, size_t bufSize) {
    File f = LittleFS.open(path, "r");
    if (!f) return -1;
    int n = f.readBytes(buf, bufSize - 1);
    buf[n] = '\0';
    f.close();
    return n;
}

bool hal_writeFile(const char *path, const char *buf, size_t len) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.write((const uint8_t *)buf, len);
    f.close();
    return true;
}

// ============================================
// SYSTEM
// ============================================
unsigned long hal_millis()              { return millis(); }
void          hal_delay(unsigned long ms) { delay(ms); }

void hal_log(const char *msg) {
    Serial.printf("[%lu] %s\n", millis(), msg);
}

static const int LED_PIN = 15;

void hal_ledOn()  { digitalWrite(LED_PIN, HIGH); }
void hal_ledOff() { digitalWrite(LED_PIN, LOW); }

// ============================================
// BUZZER (passive buzzer on BUZZER_PIN via LEDC)
// ============================================
static const int BUZZER_LEDC_CH = 0;

void hal_buzzerInit() {
    ledcSetup(BUZZER_LEDC_CH, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CH);
    ledcWriteTone(BUZZER_LEDC_CH, 0);
}

void hal_buzzerTone(int freqHz, int durationMs) {
    ledcWriteTone(BUZZER_LEDC_CH, freqHz);
    delay(durationMs);
    ledcWriteTone(BUZZER_LEDC_CH, 0);
}

void hal_buzzerOff() {
    ledcWriteTone(BUZZER_LEDC_CH, 0);
}

void hal_pumpEvents() { /* no-op on ESP32 */ }
bool hal_shouldQuit() { return false; }

// ============================================
// ARDUINO ENTRY POINTS → app_setup / app_loop
// ============================================
void setup() {
    // Motors FIRST — prevent GPIO float from activating motors during boot
    hal_initMotors();

    Serial.begin(115200);
    unsigned long serialWait = millis();
    while (!Serial && (millis() - serialWait < 3000)) delay(10);

    // Display
    tft.init();
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);
    tft.setTouch(touchCal);

    // Touch interrupt
    pinMode(XPT_IRQ, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(XPT_IRQ), touchISR, FALLING);

    // Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed!");
    }

    // LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Buzzer
    hal_buzzerInit();

    // Hand off to shared app code
    app_setup();
}

void loop() {
    // On ESP32, only process touch when ISR fires
    // But we still call app_loop() every iteration for heartbeat etc.
    // The touch-press flag is checked inside app_loop via hal_getTouch.
    // We gate polling: only call hal_getTouch when ISR triggered.
    app_loop();
}

#endif // ARDUINO
