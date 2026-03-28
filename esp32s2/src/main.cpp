// Syrup Touch Mixer — ESP32-S2 Mini C++ port
// ILI9341 display + XPT2046 touch + 8× L9110S motor channels
// PlatformIO / Arduino framework

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"

// ============================================
// DISPLAY & TOUCH
// ============================================
TFT_eSPI tft = TFT_eSPI();

// Touch calibration — run the TFT_eSPI Touch_calibrate example to obtain
// values for your specific panel, then update these five entries.
uint16_t touchCal[5] = {300, 3600, 300, 3600, 7};
const uint16_t TOUCH_THRESHOLD = 600;

// Interrupt-driven touch detection
volatile bool touchPressed = false;      // Set HIGH by ISR when TIRQ goes LOW
static unsigned long lastTouchISRTime = 0;
const unsigned long TOUCH_ISR_DEBOUNCE_MS = 10;  // debounce time

// ISR: called when TIRQ pin goes LOW (touch detected)
static void IRAM_ATTR touchISR() {
    unsigned long now = millis();
    // Debounce: ignore if triggered within 10ms of last ISR
    if (now - lastTouchISRTime < TOUCH_ISR_DEBOUNCE_MS) {
        return;
    }
    lastTouchISRTime = now;
    touchPressed = true;
}

// ============================================
// APPLICATION STATE
// ============================================
enum AppState { MAIN_MENU, VIEW_MIX, SELECT_MIX, EDIT_DASHBOARD };
AppState appState = MAIN_MENU;

// ============================================
// MIX DATA — syrupData[mix 0-8][syrup 0-7]
// ============================================
int syrupData[9][8];
int currentEditMix = 1;
int currentViewMix = 1;

// ============================================
// PASSCODE
// ============================================
int passcodeBuffer[4] = {0, 0, 0, 0};
int passcodeLen = 0;

// ============================================
// HEARTBEAT
// ============================================
const int LED_PIN = 15;           // onboard LED on S2 Mini
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_MS = 5000;  // every 5 seconds
unsigned long heartbeatCount = 0;

// ============================================
// GRID LAYOUT (landscape 320×240)
// ============================================
const int SCREEN_W = 320;
const int SCREEN_H = 240;
const int BOX_W = SCREEN_W / 3;  // 106
const int BOX_H = SCREEN_H / 3;  // 80

uint16_t gridColors[3][3];

// ============================================
// COLOUR PALETTE (initialised in setup)
// ============================================
uint16_t COL_DEFAULT, COL_PURPLE, COL_TEAL, COL_DARK_TEAL, COL_DARK_BLUE;
uint16_t COL_DARK_RED, COL_DARK_GREEN, COL_LIGHT_RED, COL_LIGHT_GREEN, COL_RED;
uint16_t COL_CYAN_BAR, COL_GREEN_BAR, COL_STOP, COL_PAUSE, COL_RESUME;
uint16_t COL_OK_BTN, COL_GRAY, COL_LIGHT_GRAY, COL_TITLE_YELLOW, COL_PAUSE_TITLE;

static void initColors() {
    COL_DEFAULT      = tft.color565(40, 40, 40);
    COL_PURPLE       = tft.color565(80, 0, 80);
    COL_TEAL         = tft.color565(0, 100, 150);
    COL_DARK_TEAL    = tft.color565(0, 50, 50);
    COL_DARK_BLUE    = tft.color565(0, 20, 40);
    COL_DARK_RED     = tft.color565(150, 0, 0);
    COL_DARK_GREEN   = tft.color565(0, 150, 0);
    COL_LIGHT_RED    = tft.color565(255, 50, 50);
    COL_LIGHT_GREEN  = tft.color565(50, 255, 50);
    COL_RED          = tft.color565(200, 0, 0);
    COL_CYAN_BAR     = tft.color565(0, 180, 255);
    COL_GREEN_BAR    = tft.color565(0, 200, 80);
    COL_STOP         = tft.color565(200, 30, 30);
    COL_PAUSE        = tft.color565(200, 140, 0);
    COL_RESUME       = tft.color565(30, 160, 60);
    COL_OK_BTN       = tft.color565(0, 120, 200);
    COL_GRAY         = tft.color565(80, 80, 80);
    COL_LIGHT_GRAY   = tft.color565(200, 200, 200);
    COL_TITLE_YELLOW = tft.color565(255, 200, 0);
    COL_PAUSE_TITLE  = tft.color565(255, 160, 0);
}

// ============================================
// POUR RESULT
// ============================================
enum PourStatus { POUR_COMPLETE, POUR_STOPPED, POUR_EMPTY };

struct PourResult {
    PourStatus status;
    float elapsed;
    int   motors;
    int   units;
};

struct MotorDisplay {
    int id;
    int amount;
    int fracPercent;   // 0-100
};

// ============================================
// LOGGING
// ============================================
static void log(const String &msg) {
    Serial.printf("[%lu] %s\n", millis(), msg.c_str());
}

// ============================================
// TOUCH HELPERS
// ============================================
static bool getTouch(uint16_t &x, uint16_t &y) {
    return tft.getTouch(&x, &y, TOUCH_THRESHOLD);
}

static void waitForRelease() {
    uint16_t x, y;
    while (tft.getTouch(&x, &y, TOUCH_THRESHOLD)) {
        delay(20);
    }
    delay(50);
}

static bool touchInRect(uint16_t tx, uint16_t ty,
                        int rx, int ry, int rw, int rh) {
    return (int)tx >= rx && (int)tx <= rx + rw &&
           (int)ty >= ry && (int)ty <= ry + rh;
}

// ============================================
// MOTOR FUNCTIONS
// ============================================
static void initMotors() {
    for (int i = 0; i < 8; i++) {
        pinMode(MOTOR_PINS[i].in1, OUTPUT);
        pinMode(MOTOR_PINS[i].in2, OUTPUT);
        digitalWrite(MOTOR_PINS[i].in1, LOW);
        digitalWrite(MOTOR_PINS[i].in2, LOW);
    }
}

static void motorOn(int id) {          // id: 1-8
    digitalWrite(MOTOR_PINS[id - 1].in1, HIGH);
    digitalWrite(MOTOR_PINS[id - 1].in2, LOW);
}

static void motorOff(int id) {
    digitalWrite(MOTOR_PINS[id - 1].in1, LOW);
    digitalWrite(MOTOR_PINS[id - 1].in2, LOW);
}

static void allMotorsOff() {
    for (int i = 1; i <= 8; i++) motorOff(i);
}

// ============================================
// PERSISTENT STORAGE (LittleFS + JSON)
// ============================================
static const char *DATA_FILE = "/syrup_mixes.json";

static void loadMixes() {
    File file = LittleFS.open(DATA_FILE, "r");
    if (!file) {
        log("No data file — creating defaults");
        memset(syrupData, 0, sizeof(syrupData));
        // Write a default JSON so it exists on flash
        JsonDocument doc;
        for (int m = 1; m <= 9; m++) {
            char mk[4]; sprintf(mk, "%d", m);
            JsonObject mo = doc[mk].to<JsonObject>();
            for (int s = 1; s <= 8; s++) {
                char sk[12]; sprintf(sk, "syrup_%d", s);
                mo[sk] = 0;
            }
        }
        File f = LittleFS.open(DATA_FILE, "w");
        if (f) { serializeJson(doc, f); f.close(); }
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        log("JSON parse error: " + String(err.c_str()));
        memset(syrupData, 0, sizeof(syrupData));
        return;
    }

    for (int m = 1; m <= 9; m++) {
        char mk[4]; sprintf(mk, "%d", m);
        for (int s = 1; s <= 8; s++) {
            char sk[12]; sprintf(sk, "syrup_%d", s);
            syrupData[m - 1][s - 1] = doc[mk][sk] | 0;
        }
    }
    log("Mixes loaded from flash");
}

static void saveMixes() {
    JsonDocument doc;
    for (int m = 1; m <= 9; m++) {
        char mk[4]; sprintf(mk, "%d", m);
        JsonObject mo = doc[mk].to<JsonObject>();
        for (int s = 1; s <= 8; s++) {
            char sk[12]; sprintf(sk, "syrup_%d", s);
            mo[sk] = syrupData[m - 1][s - 1];
        }
    }
    File f = LittleFS.open(DATA_FILE, "w");
    if (f) {
        serializeJson(doc, f);
        f.close();
        log("Mixes saved to flash");
    } else {
        log("ERROR: failed to write mixes");
    }
}

// ============================================
// GRID HELPERS
// ============================================
static void resetGridColors() {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            gridColors[r][c] = COL_DEFAULT;
}

// ============================================
// UI SCREENS
// ============================================

// --- Shared 3×3 numbered grid ---
static void drawGrid(uint16_t cellColor, bool useGridColors) {
    tft.fillScreen(TFT_BLACK);
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int x1  = col * BOX_W;
            int y1  = row * BOX_H;
            int num = row * 3 + col + 1;
            int cx  = x1 + BOX_W / 2;
            int cy  = y1 + BOX_H / 2;

            uint16_t color = useGridColors ? gridColors[row][col] : cellColor;
            tft.fillRect(x1, y1, BOX_W, BOX_H, color);
            tft.drawRect(x1, y1, BOX_W, BOX_H, TFT_WHITE);
            tft.fillCircle(cx, cy, 24, TFT_BLACK);
            tft.drawCircle(cx, cy, 24, TFT_WHITE);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_WHITE);
            tft.drawNumber(num, cx, cy, 4);
        }
    }
}

static void drawMainMenu()  { drawGrid(COL_DEFAULT, true);  }
static void drawSelectMix()  { drawGrid(COL_PURPLE,  false); }

// --- Edit dashboard (syrup amounts + SAVE) ---
static void drawEditDashboard() {
    tft.fillScreen(TFT_BLACK);
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int x1  = col * BOX_W;
            int y1  = row * BOX_H;
            int num = row * 3 + col + 1;
            int cx  = x1 + BOX_W / 2;
            int cy  = y1 + BOX_H / 2;

            if (num == 9) {
                tft.fillRect(x1, y1, BOX_W, BOX_H, COL_RED);
                tft.drawRect(x1, y1, BOX_W, BOX_H, TFT_WHITE);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_WHITE, COL_RED);
                tft.drawString("SAVE", cx, cy, 4);
            } else {
                tft.fillRect(x1, y1, BOX_W, BOX_H, COL_TEAL);
                tft.drawRect(x1, y1, BOX_W, BOX_H, TFT_WHITE);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_WHITE, COL_TEAL);
                char label[8];
                sprintf(label, "S%d", num);
                tft.drawString(label, cx, cy - 14, 2);
                tft.drawNumber(syrupData[currentEditMix - 1][num - 1],
                               cx, cy + 14, 4);
            }
        }
    }
}

// --- View mix contents (with BACK / POUR buttons) ---
static void drawViewMix(int mixId, const char *activeButton = nullptr) {
    tft.fillScreen(COL_DARK_TEAL);

    // Title
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_YELLOW, COL_DARK_TEAL);
    char title[32];
    sprintf(title, "--- MIX %d CONTENTS ---", mixId);
    tft.drawString(title, 160, 10, 2);

    // Syrup values — two columns
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, COL_DARK_TEAL);
    for (int i = 0; i < 4; i++) {
        int y = 55 + i * 28;
        char left[20], right[20];
        sprintf(left,  "Syrup %d:  %d", i + 1, syrupData[mixId - 1][i]);
        sprintf(right, "Syrup %d:  %d", i + 5, syrupData[mixId - 1][i + 4]);
        tft.drawString(left,  80,  y, 2);
        tft.drawString(right, 240, y, 2);
    }

    // BACK button
    uint16_t backCol = (activeButton && strcmp(activeButton, "BACK") == 0)
                       ? COL_LIGHT_RED : COL_DARK_RED;
    tft.fillRect(10, 180, 140, 45, backCol);
    tft.drawRect(10, 180, 140, 45, TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, backCol);
    tft.drawString("BACK", 80, 202, 4);

    // POUR button
    uint16_t pourCol = (activeButton && strcmp(activeButton, "POUR") == 0)
                       ? COL_LIGHT_GREEN : COL_DARK_GREEN;
    tft.fillRect(170, 180, 140, 45, pourCol);
    tft.drawRect(170, 180, 140, 45, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, pourCol);
    tft.drawString("POUR", 240, 202, 4);
}

// --- Pouring progress screen ---
static void drawPouringScreen(int mixId,
                              MotorDisplay *mDisp, int mCount,
                              int totalMotors,
                              float overallDone, float overallTotal,
                              float elapsedSec, bool paused) {
    tft.fillScreen(COL_DARK_BLUE);

    float overallFrac = (overallTotal > 0) ? overallDone / overallTotal : 0;
    int   pct = (int)(overallFrac * 100);

    // Title
    tft.setTextDatum(TC_DATUM);
    char titleBuf[24];
    if (paused) {
        sprintf(titleBuf, "PAUSED MIX %d", mixId);
        tft.setTextColor(COL_PAUSE_TITLE, COL_DARK_BLUE);
    } else {
        sprintf(titleBuf, "POURING MIX %d", mixId);
        tft.setTextColor(COL_TITLE_YELLOW, COL_DARK_BLUE);
    }
    tft.drawString(titleBuf, 160, 8, 2);

    // Overall progress label
    tft.setTextColor(TFT_WHITE, COL_DARK_BLUE);
    char pctStr[24]; sprintf(pctStr, "Overall  %d%%", pct);
    tft.drawString(pctStr, 160, 32, 2);

    // Overall progress bar
    const int barX = 20, barY = 52, barW = 280, barH = 24;
    tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
    int fillW = (int)(barW * overallFrac);
    if (fillW > 0)
        tft.fillRect(barX + 1, barY + 1, fillW - 1, barH - 2, COL_CYAN_BAR);

    // Volume text on top of bar
    tft.setTextDatum(MC_DATUM);
    char volStr[24]; sprintf(volStr, "%.1f/%.0f ml", overallDone, overallTotal);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(volStr, 160, barY + barH / 2, 1);

    // Time info
    float remaining = (overallDone > 0.01f)
        ? elapsedSec * ((overallTotal - overallDone) / overallDone)
        : 0;
    if (remaining < 0) remaining = 0;
    char timeStr[48];
    sprintf(timeStr, "Elapsed: %.1fs  Remaining: ~%.1fs", elapsedSec, remaining);
    tft.setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
    tft.drawString(timeStr, 160, 92, 1);

    // Separator
    tft.drawFastHLine(20, 108, 280, COL_GRAY);

    // Active motors label
    char activeStr[32];
    sprintf(activeStr, "Active motors (%d/%d total)", mCount, totalMotors);
    tft.drawString(activeStr, 160, 120, 1);

    // Per-motor progress bars
    for (int i = 0; i < mCount && i < MAX_CONCURRENT; i++) {
        int yBase = 140 + i * 24;
        char label[16];
        sprintf(label, "M%d: %d%%", mDisp[i].id, mDisp[i].fracPercent);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
        tft.drawString(label, 20, yBase, 1);

        const int mbX = 100, mbW = 200, mbH = 10;
        tft.drawRect(mbX, yBase - 5, mbW, mbH, COL_LIGHT_GRAY);
        int mfW = mbW * mDisp[i].fracPercent / 100;
        if (mfW > 0)
            tft.fillRect(mbX + 1, yBase - 4, mfW - 1, mbH - 2, COL_GREEN_BAR);
    }

    // STOP button
    const int btnY = 200, btnH = 36;
    tft.fillRect(20, btnY, 138, btnH, COL_STOP);
    tft.drawRect(20, btnY, 138, btnH, tft.color565(255, 100, 100));
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, COL_STOP);
    tft.drawString("STOP", 89, btnY + btnH / 2, 2);

    // PAUSE / RESUME button
    if (paused) {
        tft.fillRect(166, btnY, 138, btnH, COL_RESUME);
        tft.drawRect(166, btnY, 138, btnH, tft.color565(100, 220, 100));
        tft.setTextColor(TFT_WHITE, COL_RESUME);
        tft.drawString("RESUME", 235, btnY + btnH / 2, 2);
    } else {
        tft.fillRect(166, btnY, 138, btnH, COL_PAUSE);
        tft.drawRect(166, btnY, 138, btnH, tft.color565(255, 200, 80));
        tft.setTextColor(TFT_WHITE, COL_PAUSE);
        tft.drawString("PAUSE", 235, btnY + btnH / 2, 2);
    }
}

// --- Summary screen (after pour completes or is stopped) ---
static void drawSummaryScreen(int mixId, PourStatus status,
                              float elapsed, int motorsUsed, int totalUnits) {
    tft.fillScreen(COL_DARK_BLUE);

    tft.setTextDatum(TC_DATUM);
    if (status == POUR_COMPLETE) {
        tft.setTextColor(tft.color565(0, 220, 80), COL_DARK_BLUE);
        tft.drawString("POUR COMPLETE", 160, 22, 4);
    } else {
        tft.setTextColor(tft.color565(220, 60, 60), COL_DARK_BLUE);
        tft.drawString("POUR STOPPED", 160, 22, 4);
    }

    tft.drawFastHLine(40, 55, 240, COL_GRAY);

    tft.setTextDatum(MC_DATUM);
    char buf[32];
    tft.setTextColor(TFT_WHITE, COL_DARK_BLUE);
    sprintf(buf, "Mix %d", mixId);
    tft.drawString(buf, 160, 80, 2);

    tft.setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
    sprintf(buf, "Time: %.1fs", elapsed);
    tft.drawString(buf, 160, 110, 2);
    sprintf(buf, "Motors: %d", motorsUsed);
    tft.drawString(buf, 160, 140, 2);
    sprintf(buf, "Volume: %d ml", totalUnits);
    tft.drawString(buf, 160, 170, 2);

    // OK button
    tft.fillRect(110, 195, 100, 38, COL_OK_BTN);
    tft.drawRect(110, 195, 100, 38, tft.color565(100, 180, 255));
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, COL_OK_BTN);
    tft.drawString("OK", 160, 214, 4);
}

// ============================================
// POUR EXECUTION
// ============================================

struct RunningMotor {
    int   id;
    int   amount;
    float startTime;   // "active seconds" when motor started
};

static PourResult executePour(int mixId) {
    // --- Collect motors that need to run ---
    struct Pending { int id; int amount; };
    Pending pending[8];
    int pendingCount = 0;

    for (int s = 0; s < 8; s++) {
        int amt = syrupData[mixId - 1][s];
        if (amt > 0)
            pending[pendingCount++] = {s + 1, amt};
    }

    if (pendingCount == 0) {
        log("Mix " + String(mixId) + " is empty");
        return {POUR_EMPTY, 0, 0, 0};
    }

    int totalMotors = pendingCount;
    int totalUnits  = 0;
    for (int i = 0; i < pendingCount; i++) totalUnits += pending[i].amount;

    log("DISPENSING Mix " + String(mixId) + " — "
        + String(totalMotors) + " motors, "
        + String(totalUnits)  + " ml, max "
        + String(MAX_CONCURRENT) + " concurrent");

    // --- Runtime state ---
    RunningMotor running[MAX_CONCURRENT];
    int   runningCount  = 0;
    int   nextPending   = 0;
    float finishedUnits = 0;

    unsigned long t0            = millis();
    bool          cancelled     = false;
    bool          paused        = false;
    unsigned long pauseStart    = 0;
    unsigned long totalPauseMs  = 0;

    // --- Main pour loop ---
    while (nextPending < pendingCount || runningCount > 0) {

        // ---- Check touch for STOP / PAUSE ----
        uint16_t tx, ty;
        if (getTouch(tx, ty)) {
            if (ty >= 200 && ty <= 236) {
                if (tx >= 20 && tx <= 158) {
                    log("STOPPED by user");
                    cancelled = true;
                    break;
                }
                if (tx >= 166 && tx <= 304) {
                    paused = !paused;
                    if (paused) {
                        pauseStart = millis();
                        for (int i = 0; i < runningCount; i++)
                            motorOff(running[i].id);
                        log("PAUSED");
                    } else {
                        totalPauseMs += millis() - pauseStart;
                        for (int i = 0; i < runningCount; i++)
                            motorOn(running[i].id);
                        log("RESUMED");
                    }
                    delay(300);   // debounce
                }
            }
        }

        float activeTime = (millis() - t0 - totalPauseMs) / 1000.0f;

        // ---- Paused — only update display ----
        if (paused) {
            MotorDisplay md[MAX_CONCURRENT];
            float runFrac = 0;
            for (int i = 0; i < runningCount; i++) {
                float dur  = running[i].amount * SECONDS_PER_UNIT;
                float frac = (activeTime - running[i].startTime) / dur;
                if (frac > 1.0f) frac = 1.0f;
                md[i] = {running[i].id, running[i].amount, (int)(frac * 100)};
                runFrac += running[i].amount * frac;
            }
            drawPouringScreen(mixId, md, runningCount, totalMotors,
                              finishedUnits + runFrac, (float)totalUnits,
                              activeTime, true);
            delay(50);
            continue;
        }

        // ---- Start new motors up to MAX_CONCURRENT ----
        while (nextPending < pendingCount && runningCount < MAX_CONCURRENT) {
            activeTime = (millis() - t0 - totalPauseMs) / 1000.0f;
            int mId  = pending[nextPending].id;
            int mAmt = pending[nextPending].amount;
            running[runningCount++] = {mId, mAmt, activeTime};
            motorOn(mId);
            log("Motor " + String(mId) + ": START — "
                + String(mAmt) + " ml ("
                + String(mAmt * SECONDS_PER_UNIT, 1) + "s)");
            nextPending++;

            // Inrush-current stagger (with stop-button polling)
            if (nextPending < pendingCount && runningCount < MAX_CONCURRENT) {
                log("Inrush stagger " + String(STAGGER_DELAY_MS) + "ms");
                unsigned long staggerEnd = millis() + STAGGER_DELAY_MS;
                while (millis() < staggerEnd) {
                    uint16_t bx, by;
                    if (getTouch(bx, by) && by >= 200
                        && bx >= 20 && bx <= 158) {
                        cancelled = true;
                        break;
                    }
                    delay(20);
                }
                if (cancelled) break;
            }
        }
        if (cancelled) break;

        activeTime = (millis() - t0 - totalPauseMs) / 1000.0f;

        // ---- Check for completed motors ----
        for (int i = runningCount - 1; i >= 0; i--) {
            float dur = running[i].amount * SECONDS_PER_UNIT;
            if (activeTime - running[i].startTime >= dur) {
                motorOff(running[i].id);
                finishedUnits += running[i].amount;
                log("Motor " + String(running[i].id)
                    + ": DONE — " + String(running[i].amount) + " ml");
                running[i] = running[--runningCount];   // swap-remove
            }
        }

        // ---- Update display ----
        MotorDisplay md[MAX_CONCURRENT];
        float runFrac = 0;
        for (int i = 0; i < runningCount; i++) {
            float dur  = running[i].amount * SECONDS_PER_UNIT;
            float frac = (activeTime - running[i].startTime) / dur;
            if (frac > 1.0f) frac = 1.0f;
            md[i] = {running[i].id, running[i].amount, (int)(frac * 100)};
            runFrac += running[i].amount * frac;
        }
        drawPouringScreen(mixId, md, runningCount, totalMotors,
                          finishedUnits + runFrac, (float)totalUnits,
                          activeTime, false);

        delay(100);   // 100 ms tick
    }

    // Safety: ensure all motors off
    allMotorsOff();

    float elapsed = (millis() - t0 - totalPauseMs) / 1000.0f;
    PourStatus st = cancelled ? POUR_STOPPED : POUR_COMPLETE;
    if (cancelled)
        log("Pour STOPPED after " + String(elapsed, 1) + "s");
    else
        log("Pour COMPLETE in " + String(elapsed, 1) + "s");

    return {st, elapsed, totalMotors, totalUnits};
}

// ============================================
// SETUP
// ============================================
void setup() {
    Serial.begin(115200);
    // Wait for USB CDC connection (up to 3s)
    unsigned long serialWait = millis();
    while (!Serial && (millis() - serialWait < 3000)) {
        delay(10);
    }
    Serial.println("\n=== Syrup Touch Mixer (ESP32-S2 Mini) ===");

    // Display
    tft.init();
    tft.setRotation(1);          // landscape 320×240
    tft.fillScreen(TFT_BLACK);
    tft.setTouch(touchCal);

    // Touch interrupt pin (TIRQ = active LOW when touched)
    pinMode(TOUCH_IRQ, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TOUCH_IRQ), touchISR, FALLING);
    log("Touch interrupt enabled on GPIO " + String(TOUCH_IRQ));

    // Colours (must be after tft.init)
    initColors();

    // Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed!");
    }

    // Heartbeat LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Motors
    initMotors();

    // Mix data
    loadMixes();

    // Initial screen
    resetGridColors();
    drawMainMenu();

    Serial.println("System Ready. Waiting for input...");
}

// ============================================
// MAIN LOOP — state machine
// ============================================
void loop() {
    // --- Heartbeat: blink LED + serial print every 5s ---
    if (millis() - lastHeartbeat >= HEARTBEAT_MS) {
        lastHeartbeat = millis();
        heartbeatCount++;
        digitalWrite(LED_PIN, HIGH);
        Serial.printf("[%lu] ALIVE #%lu | state=%d | uptime=%lus\n",
                      millis(), heartbeatCount, (int)appState, millis() / 1000);
        delay(50);
        digitalWrite(LED_PIN, LOW);
    }

    // Check if touch interrupt has triggered
    if (!touchPressed) {
        delay(20);
        return;
    }

    // Clear the flag and read touch coordinates
    touchPressed = false;
    
    uint16_t tx, ty;
    if (!getTouch(tx, ty)) {
        delay(20);
        return;
    }

    // Bounds check (guard against bad calibration)
    if (tx >= SCREEN_W || ty >= SCREEN_H) {
        delay(20);
        return;
    }

    // Map touch to 3×3 grid
    int col    = tx / BOX_W;  if (col > 2) col = 2;
    int row    = ty / BOX_H;  if (row > 2) row = 2;
    int number = row * 3 + col + 1;

    switch (appState) {

    // --------------------------------------------------
    case MAIN_MENU: {
        // Visual highlight
        resetGridColors();
        gridColors[row][col] = TFT_YELLOW;
        drawMainMenu();

        // Measure press duration
        unsigned long pressStart = millis();
        waitForRelease();
        unsigned long duration = millis() - pressStart;

        resetGridColors();
        drawMainMenu();

        if (duration >= 500) {
            // ---- Long press → view mix ----
            passcodeLen = 0;
            currentViewMix = number;
            log("Viewing Mix " + String(number));
            for (int s = 0; s < 8; s++) {
                int amt = syrupData[number - 1][s];
                if (amt > 0)
                    log("  Motor " + String(s + 1) + ": " + String(amt) + " ml");
            }
            appState = VIEW_MIX;
            drawViewMix(currentViewMix);
        } else {
            // ---- Short tap → passcode entry ----
            if (passcodeLen >= 4) {
                passcodeBuffer[0] = passcodeBuffer[1];
                passcodeBuffer[1] = passcodeBuffer[2];
                passcodeBuffer[2] = passcodeBuffer[3];
                passcodeBuffer[3] = number;
            } else {
                passcodeBuffer[passcodeLen++] = number;
            }

            if (passcodeLen == 4 &&
                passcodeBuffer[0] == SECRET_CODE[0] &&
                passcodeBuffer[1] == SECRET_CODE[1] &&
                passcodeBuffer[2] == SECRET_CODE[2] &&
                passcodeBuffer[3] == SECRET_CODE[3]) {
                Serial.println("PASSCODE ACCEPTED!");
                appState = SELECT_MIX;
                passcodeLen = 0;
                drawSelectMix();
            }
        }
        break;
    }

    // --------------------------------------------------
    case VIEW_MIX: {
        if (touchInRect(tx, ty, 10, 180, 140, 45)) {
            // BACK
            drawViewMix(currentViewMix, "BACK");
            waitForRelease();
            appState = MAIN_MENU;
            resetGridColors();
            drawMainMenu();

        } else if (touchInRect(tx, ty, 170, 180, 140, 45)) {
            // POUR
            drawViewMix(currentViewMix, "POUR");
            waitForRelease();

            PourResult result = executePour(currentViewMix);

            if (result.status != POUR_EMPTY) {
                drawSummaryScreen(currentViewMix, result.status,
                                  result.elapsed, result.motors,
                                  result.units);
                // Wait for OK tap
                uint16_t ox, oy;
                while (!getTouch(ox, oy)) delay(20);
                waitForRelease();
            }

            appState = MAIN_MENU;
            resetGridColors();
            drawMainMenu();
        }
        break;
    }

    // --------------------------------------------------
    case SELECT_MIX: {
        waitForRelease();
        currentEditMix = number;
        log("Editing Mix " + String(number));
        appState = EDIT_DASHBOARD;
        drawEditDashboard();
        break;
    }

    // --------------------------------------------------
    case EDIT_DASHBOARD: {
        waitForRelease();
        if (number == 9) {
            // SAVE
            saveMixes();
            log("Mix " + String(currentEditMix) + " saved");
            for (int s = 0; s < 8; s++) {
                int amt = syrupData[currentEditMix - 1][s];
                String st = (amt > 0) ? String(amt) + " ml" : "OFF";
                log("  Motor " + String(s + 1) + ": " + st);
            }
            appState = MAIN_MENU;
            resetGridColors();
            drawMainMenu();
        } else {
            // Increment syrup value (0→1→…→10→0)
            int &val = syrupData[currentEditMix - 1][number - 1];
            val = (val + 1) % 11;
            log("Motor " + String(number) + " set to " + String(val)
                + " (Mix " + String(currentEditMix) + ")");

            // Flash feedback
            int x1 = col * BOX_W;
            int y1 = row * BOX_H;
            tft.fillRect(x1, y1, BOX_W, BOX_H, TFT_CYAN);
            delay(50);
            drawEditDashboard();
        }
        break;
    }
    }  // switch

    delay(20);
}
