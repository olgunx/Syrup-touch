// Syrup Touch Mixer — Shared Application Logic
// Uses hal.h for all hardware access.
// Compiled for both ESP32-S2 (real) and native (SDL2 simulator).

#include "hal.h"
#include "config.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>

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
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_MS = 5000;
unsigned long heartbeatCount = 0;

// ============================================
// GRID LAYOUT (landscape 320×240)
// ============================================
const int SCREEN_W = 320;
const int SCREEN_H = 240;
const int BOX_W = SCREEN_W / 3;  // 106
const int BOX_H = SCREEN_H / 3;  // 80

HalColor gridColors[3][3];

// ============================================
// COLOUR PALETTE
// ============================================
static HalColor COL_BLACK, COL_WHITE, COL_YELLOW, COL_CYAN;
static HalColor COL_DEFAULT, COL_PURPLE, COL_TEAL, COL_DARK_TEAL, COL_DARK_BLUE;
static HalColor COL_DARK_RED, COL_DARK_GREEN, COL_LIGHT_RED, COL_LIGHT_GREEN, COL_RED;
static HalColor COL_CYAN_BAR, COL_GREEN_BAR, COL_STOP, COL_PAUSE, COL_RESUME;
static HalColor COL_OK_BTN, COL_GRAY, COL_LIGHT_GRAY, COL_TITLE_YELLOW, COL_PAUSE_TITLE;

static void initColors() {
    COL_BLACK        = hal_color(0, 0, 0);
    COL_WHITE        = hal_color(255, 255, 255);
    COL_YELLOW       = hal_color(255, 255, 0);
    COL_CYAN         = hal_color(0, 255, 255);
    COL_DEFAULT      = hal_color(40, 40, 40);
    COL_PURPLE       = hal_color(80, 0, 80);
    COL_TEAL         = hal_color(0, 100, 150);
    COL_DARK_TEAL    = hal_color(0, 50, 50);
    COL_DARK_BLUE    = hal_color(0, 20, 40);
    COL_DARK_RED     = hal_color(150, 0, 0);
    COL_DARK_GREEN   = hal_color(0, 150, 0);
    COL_LIGHT_RED    = hal_color(255, 50, 50);
    COL_LIGHT_GREEN  = hal_color(50, 255, 50);
    COL_RED          = hal_color(200, 0, 0);
    COL_CYAN_BAR     = hal_color(0, 180, 255);
    COL_GREEN_BAR    = hal_color(0, 200, 80);
    COL_STOP         = hal_color(200, 30, 30);
    COL_PAUSE        = hal_color(200, 140, 0);
    COL_RESUME       = hal_color(30, 160, 60);
    COL_OK_BTN       = hal_color(0, 120, 200);
    COL_GRAY         = hal_color(80, 80, 80);
    COL_LIGHT_GRAY   = hal_color(200, 200, 200);
    COL_TITLE_YELLOW = hal_color(255, 200, 0);
    COL_PAUSE_TITLE  = hal_color(255, 160, 0);
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
static void log(const char *msg) {
    hal_log(msg);
}

// Helper: sprintf + log
static void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    hal_log(buf);
}

// ============================================
// TOUCH HELPERS
// ============================================
static bool touchInRect(uint16_t tx, uint16_t ty,
                        int rx, int ry, int rw, int rh) {
    return (int)tx >= rx && (int)tx <= rx + rw &&
           (int)ty >= ry && (int)ty <= ry + rh;
}

// ============================================
// PERSISTENT STORAGE (HAL + manual JSON)
// ============================================
static const char *DATA_FILE = "/syrup_mixes.json";

// Minimal JSON integer extractor: finds "key": <int> in a flat JSON object.
// NOT a general JSON parser — sufficient for our simple data format.
static int jsonGetInt(const char *json, const char *key, int fallback) {
    const char *p = json;
    size_t klen = strlen(key);
    while ((p = strstr(p, key)) != nullptr) {
        // Verify it's a quoted key: check for '"' before key
        if (p > json && *(p - 1) == '"') {
            p += klen;
            // skip '":' and whitespace
            while (*p && (*p == '"' || *p == ':' || *p == ' ' || *p == '\t')) p++;
            if (*p == '-' || (*p >= '0' && *p <= '9')) {
                return (int)strtol(p, nullptr, 10);
            }
        }
        p++;
    }
    return fallback;
}

static void loadMixes() {
    char buf[4096];
    int n = hal_readFile(DATA_FILE, buf, sizeof(buf));
    if (n <= 0) {
        log("No data file — using defaults");
        memset(syrupData, 0, sizeof(syrupData));
        return;
    }

    // Parse: top-level keys "1".."9", each containing "syrup_1".."syrup_8"
    for (int m = 1; m <= 9; m++) {
        // Find the block for this mix by locating its key
        char mixKey[8];
        snprintf(mixKey, sizeof(mixKey), "\"%d\"", m);
        const char *mixStart = strstr(buf, mixKey);
        if (!mixStart) continue;
        // Find the opening { for this mix object
        const char *objStart = strchr(mixStart, '{');
        if (!objStart) continue;
        // Find the closing }
        const char *objEnd = strchr(objStart, '}');
        if (!objEnd) continue;
        // Extract substring for this object
        char objBuf[512];
        size_t objLen = (size_t)(objEnd - objStart + 1);
        if (objLen >= sizeof(objBuf)) objLen = sizeof(objBuf) - 1;
        memcpy(objBuf, objStart, objLen);
        objBuf[objLen] = '\0';

        for (int s = 1; s <= 8; s++) {
            char sk[12];
            snprintf(sk, sizeof(sk), "syrup_%d", s);
            syrupData[m - 1][s - 1] = jsonGetInt(objBuf, sk, 0);
        }
    }
    log("Mixes loaded");
}

static void saveMixes() {
    char buf[4096];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\n");
    for (int m = 1; m <= 9; m++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "  \"%d\": {\n", m);
        for (int s = 1; s <= 8; s++) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "    \"syrup_%d\": %d%s\n",
                            s, syrupData[m - 1][s - 1],
                            (s < 8) ? "," : "");
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "  }%s\n", (m < 9) ? "," : "");
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "}\n");

    if (hal_writeFile(DATA_FILE, buf, pos)) {
        log("Mixes saved");
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

// --- Draw a single grid cell (avoids full-screen redraw) ---
static void drawGridCell(int row, int col, HalColor color) {
    int x1  = col * BOX_W;
    int y1  = row * BOX_H;
    int num = row * 3 + col + 1;
    int cx  = x1 + BOX_W / 2;
    int cy  = y1 + BOX_H / 2;

    hal_fillRect(x1, y1, BOX_W, BOX_H, color);
    hal_drawRect(x1, y1, BOX_W, BOX_H, COL_WHITE);
    hal_fillCircle(cx, cy, 24, COL_BLACK);
    hal_drawCircle(cx, cy, 24, COL_WHITE);
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE);
    hal_drawNumber(num, cx, cy, 4);
}

// --- Shared 3×3 numbered grid ---
static void drawGrid(HalColor cellColor, bool useGridColors) {
    hal_fillScreen(COL_BLACK);
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            HalColor color = useGridColors ? gridColors[row][col] : cellColor;
            drawGridCell(row, col, color);
        }
    }
}

static void drawMainMenu()  { drawGrid(COL_DEFAULT, true);  }
static void drawSelectMix()  { drawGrid(COL_PURPLE,  false); }

// --- Edit dashboard (syrup amounts + SAVE) ---
static void drawEditDashboard() {
    hal_fillScreen(COL_BLACK);
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int x1  = col * BOX_W;
            int y1  = row * BOX_H;
            int num = row * 3 + col + 1;
            int cx  = x1 + BOX_W / 2;
            int cy  = y1 + BOX_H / 2;

            if (num == 9) {
                hal_fillRect(x1, y1, BOX_W, BOX_H, COL_RED);
                hal_drawRect(x1, y1, BOX_W, BOX_H, COL_WHITE);
                hal_setTextDatum(HAL_DATUM_MC);
                hal_setTextColor(COL_WHITE, COL_RED);
                hal_drawString("SAVE", cx, cy, 4);
            } else {
                hal_fillRect(x1, y1, BOX_W, BOX_H, COL_TEAL);
                hal_drawRect(x1, y1, BOX_W, BOX_H, COL_WHITE);
                hal_setTextDatum(HAL_DATUM_MC);
                hal_setTextColor(COL_WHITE, COL_TEAL);
                char label[8];
                snprintf(label, sizeof(label), "S%d", num);
                hal_drawString(label, cx, cy - 14, 2);
                hal_drawNumber(syrupData[currentEditMix - 1][num - 1],
                               cx, cy + 14, 4);
            }
        }
    }
}

// --- Redraw a single edit dashboard cell ---
static void drawEditCell(int row, int col) {
    int x1  = col * BOX_W;
    int y1  = row * BOX_H;
    int num = row * 3 + col + 1;
    int cx  = x1 + BOX_W / 2;
    int cy  = y1 + BOX_H / 2;

    hal_fillRect(x1, y1, BOX_W, BOX_H, COL_TEAL);
    hal_drawRect(x1, y1, BOX_W, BOX_H, COL_WHITE);
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, COL_TEAL);
    char label[8];
    snprintf(label, sizeof(label), "S%d", num);
    hal_drawString(label, cx, cy - 14, 2);
    hal_drawNumber(syrupData[currentEditMix - 1][num - 1],
                   cx, cy + 14, 4);
}

// --- View mix contents (with BACK / POUR buttons) ---
static void drawViewMix(int mixId, const char *activeButton = nullptr) {
    hal_fillScreen(COL_DARK_TEAL);

    // Title
    hal_setTextDatum(HAL_DATUM_TC);
    hal_setTextColor(COL_YELLOW, COL_DARK_TEAL);
    char title[32];
    snprintf(title, sizeof(title), "--- MIX %d CONTENTS ---", mixId);
    hal_drawString(title, 160, 10, 2);

    // Syrup values — two columns
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, COL_DARK_TEAL);
    for (int i = 0; i < 4; i++) {
        int y = 55 + i * 28;
        char left[20], right[20];
        snprintf(left,  sizeof(left),  "Syrup %d:  %d", i + 1, syrupData[mixId - 1][i]);
        snprintf(right, sizeof(right), "Syrup %d:  %d", i + 5, syrupData[mixId - 1][i + 4]);
        hal_drawString(left,  80,  y, 2);
        hal_drawString(right, 240, y, 2);
    }

    // BACK button
    HalColor backCol = (activeButton && strcmp(activeButton, "BACK") == 0)
                       ? COL_LIGHT_RED : COL_DARK_RED;
    hal_fillRect(10, 180, 140, 45, backCol);
    hal_drawRect(10, 180, 140, 45, COL_WHITE);
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, backCol);
    hal_drawString("BACK", 80, 202, 4);

    // POUR button
    HalColor pourCol = (activeButton && strcmp(activeButton, "POUR") == 0)
                       ? COL_LIGHT_GREEN : COL_DARK_GREEN;
    hal_fillRect(170, 180, 140, 45, pourCol);
    hal_drawRect(170, 180, 140, 45, COL_WHITE);
    hal_setTextColor(COL_WHITE, pourCol);
    hal_drawString("POUR", 240, 202, 4);
}

// --- Pouring progress screen: static layout (call once) ---
static void drawPouringLayout(int mixId, int totalMotors, bool paused) {
    hal_fillScreen(COL_DARK_BLUE);

    // Title
    hal_setTextDatum(HAL_DATUM_TC);
    char titleBuf[24];
    if (paused) {
        snprintf(titleBuf, sizeof(titleBuf), "PAUSED MIX %d", mixId);
        hal_setTextColor(COL_PAUSE_TITLE, COL_DARK_BLUE);
    } else {
        snprintf(titleBuf, sizeof(titleBuf), "POURING MIX %d", mixId);
        hal_setTextColor(COL_TITLE_YELLOW, COL_DARK_BLUE);
    }
    hal_drawString(titleBuf, 160, 8, 2);

    // Overall progress bar outline
    const int barX = 20, barY = 52, barW = 280, barH = 24;
    hal_drawRect(barX, barY, barW, barH, COL_WHITE);

    // Separator
    hal_drawHLine(20, 108, 280, COL_GRAY);

    // Motor bar outlines
    for (int i = 0; i < MAX_CONCURRENT; i++) {
        int yBase = 140 + i * 24;
        const int mbX = 100, mbW = 200, mbH = 10;
        hal_drawRect(mbX, yBase - 5, mbW, mbH, COL_LIGHT_GRAY);
    }

    // STOP button
    const int btnY = 200, btnH = 36;
    hal_fillRect(20, btnY, 138, btnH, COL_STOP);
    hal_drawRect(20, btnY, 138, btnH, hal_color(255, 100, 100));
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, COL_STOP);
    hal_drawString("STOP", 89, btnY + btnH / 2, 2);

    // PAUSE / RESUME button
    if (paused) {
        hal_fillRect(166, btnY, 138, btnH, COL_RESUME);
        hal_drawRect(166, btnY, 138, btnH, hal_color(100, 220, 100));
        hal_setTextColor(COL_WHITE, COL_RESUME);
        hal_drawString("RESUME", 235, btnY + btnH / 2, 2);
    } else {
        hal_fillRect(166, btnY, 138, btnH, COL_PAUSE);
        hal_drawRect(166, btnY, 138, btnH, hal_color(255, 200, 80));
        hal_setTextColor(COL_WHITE, COL_PAUSE);
        hal_drawString("PAUSE", 235, btnY + btnH / 2, 2);
    }
}

// --- Pouring progress screen: update dynamic parts only ---
static void updatePouringScreen(int mixId,
                                MotorDisplay *mDisp, int mCount,
                                int totalMotors,
                                float overallDone, float overallTotal,
                                float elapsedSec, bool paused) {
    float overallFrac = (overallTotal > 0) ? overallDone / overallTotal : 0;
    int   pct = (int)(overallFrac * 100);

    // Overall progress label
    hal_fillRect(0, 24, 320, 22, COL_DARK_BLUE);
    hal_setTextDatum(HAL_DATUM_TC);
    hal_setTextColor(COL_WHITE, COL_DARK_BLUE);
    char pctStr[24]; snprintf(pctStr, sizeof(pctStr), "Overall  %d%%", pct);
    hal_drawString(pctStr, 160, 32, 2);

    // Overall progress bar fill
    const int barX = 20, barY = 52, barW = 280, barH = 24;
    int fillW = (int)(barW * overallFrac);
    hal_fillRect(barX + 1, barY + 1, barW - 2, barH - 2, COL_DARK_BLUE);
    if (fillW > 0)
        hal_fillRect(barX + 1, barY + 1, fillW - 1, barH - 2, COL_CYAN_BAR);

    // Volume text
    hal_setTextDatum(HAL_DATUM_MC);
    char volStr[24]; snprintf(volStr, sizeof(volStr), "%.1f/%.0f ml", overallDone, overallTotal);
    hal_setTextColor(COL_WHITE);
    hal_drawString(volStr, 160, barY + barH / 2, 1);

    // Time info
    hal_fillRect(0, 84, 320, 18, COL_DARK_BLUE);
    float remaining = (overallDone > 0.01f)
        ? elapsedSec * ((overallTotal - overallDone) / overallDone)
        : 0;
    if (remaining < 0) remaining = 0;
    char timeStr[48];
    snprintf(timeStr, sizeof(timeStr), "Elapsed: %.1fs  Remaining: ~%.1fs", elapsedSec, remaining);
    hal_setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
    hal_drawString(timeStr, 160, 92, 1);

    // Active motors label
    hal_fillRect(0, 112, 320, 18, COL_DARK_BLUE);
    char activeStr[32];
    snprintf(activeStr, sizeof(activeStr), "Active motors (%d/%d total)", mCount, totalMotors);
    hal_drawString(activeStr, 160, 120, 1);

    // Per-motor progress bars
    for (int i = 0; i < MAX_CONCURRENT; i++) {
        int yBase = 140 + i * 24;
        const int mbX = 100, mbW = 200, mbH = 10;
        hal_fillRect(0, yBase - 8, 98, 16, COL_DARK_BLUE);
        hal_fillRect(mbX + 1, yBase - 4, mbW - 2, mbH - 2, COL_DARK_BLUE);

        if (i < mCount) {
            char label[16];
            snprintf(label, sizeof(label), "M%d: %d%%", mDisp[i].id, mDisp[i].fracPercent);
            hal_setTextDatum(HAL_DATUM_ML);
            hal_setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
            hal_drawString(label, 20, yBase, 1);

            int mfW = mbW * mDisp[i].fracPercent / 100;
            if (mfW > 0)
                hal_fillRect(mbX + 1, yBase - 4, mfW - 1, mbH - 2, COL_GREEN_BAR);
        }
    }
}

// --- Summary screen (after pour completes or is stopped) ---
static void drawSummaryScreen(int mixId, PourStatus status,
                              float elapsed, int motorsUsed, int totalUnits) {
    hal_fillScreen(COL_DARK_BLUE);

    hal_setTextDatum(HAL_DATUM_TC);
    if (status == POUR_COMPLETE) {
        hal_setTextColor(hal_color(0, 220, 80), COL_DARK_BLUE);
        hal_drawString("POUR COMPLETE", 160, 22, 4);
    } else {
        hal_setTextColor(hal_color(220, 60, 60), COL_DARK_BLUE);
        hal_drawString("POUR STOPPED", 160, 22, 4);
    }

    hal_drawHLine(40, 55, 240, COL_GRAY);

    hal_setTextDatum(HAL_DATUM_MC);
    char buf[32];
    hal_setTextColor(COL_WHITE, COL_DARK_BLUE);
    snprintf(buf, sizeof(buf), "Mix %d", mixId);
    hal_drawString(buf, 160, 80, 2);

    hal_setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
    snprintf(buf, sizeof(buf), "Time: %.1fs", elapsed);
    hal_drawString(buf, 160, 110, 2);
    snprintf(buf, sizeof(buf), "Motors: %d", motorsUsed);
    hal_drawString(buf, 160, 140, 2);
    snprintf(buf, sizeof(buf), "Volume: %d ml", totalUnits);
    hal_drawString(buf, 160, 170, 2);

    // OK button
    hal_fillRect(110, 195, 100, 38, COL_OK_BTN);
    hal_drawRect(110, 195, 100, 38, hal_color(100, 180, 255));
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, COL_OK_BTN);
    hal_drawString("OK", 160, 214, 4);
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
        logf("Mix %d is empty", mixId);
        return {POUR_EMPTY, 0, 0, 0};
    }

    int totalMotors = pendingCount;
    int totalUnits  = 0;
    for (int i = 0; i < pendingCount; i++) totalUnits += pending[i].amount;

    logf("DISPENSING Mix %d — %d motors, %d ml, max %d concurrent",
         mixId, totalMotors, totalUnits, MAX_CONCURRENT);

    // --- Runtime state ---
    RunningMotor running[MAX_CONCURRENT];
    int   runningCount  = 0;
    int   nextPending   = 0;
    float finishedUnits = 0;

    unsigned long t0            = hal_millis();
    bool          cancelled     = false;
    bool          paused        = false;
    unsigned long pauseStart    = 0;
    unsigned long totalPauseMs  = 0;
    unsigned long lastDisplayUpdate = 0;
    const unsigned long DISPLAY_UPDATE_MS = 500;

    // Draw the static layout once
    drawPouringLayout(mixId, totalMotors, false);

    // --- Main pour loop ---
    while ((nextPending < pendingCount || runningCount > 0) && !hal_shouldQuit()) {

        // ---- Check touch for STOP / PAUSE ----
        uint16_t tx, ty;
        if (hal_getTouch(tx, ty)) {
            if (ty >= 200 && ty <= 236) {
                if (tx >= 20 && tx <= 158) {
                    log("STOPPED by user");
                    cancelled = true;
                    break;
                }
                if (tx >= 166 && tx <= 304) {
                    paused = !paused;
                    if (paused) {
                        pauseStart = hal_millis();
                        for (int i = 0; i < runningCount; i++)
                            hal_motorOff(running[i].id);
                        log("PAUSED");
                    } else {
                        totalPauseMs += hal_millis() - pauseStart;
                        for (int i = 0; i < runningCount; i++)
                            hal_motorOn(running[i].id);
                        log("RESUMED");
                    }
                    drawPouringLayout(mixId, totalMotors, paused);
                    hal_delay(300);   // debounce
                }
            }
        }

        float activeTime = (hal_millis() - t0 - totalPauseMs) / 1000.0f;

        // ---- Paused — only update display periodically ----
        if (paused) {
            if (hal_millis() - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
                lastDisplayUpdate = hal_millis();
                MotorDisplay md[MAX_CONCURRENT];
                float runFrac = 0;
                for (int i = 0; i < runningCount; i++) {
                    float dur  = running[i].amount * SECONDS_PER_UNIT;
                    float frac = (activeTime - running[i].startTime) / dur;
                    if (frac > 1.0f) frac = 1.0f;
                    md[i] = {running[i].id, running[i].amount, (int)(frac * 100)};
                    runFrac += running[i].amount * frac;
                }
                updatePouringScreen(mixId, md, runningCount, totalMotors,
                                  finishedUnits + runFrac, (float)totalUnits,
                                  activeTime, true);
            }
            hal_delay(50);
            continue;
        }

        // ---- Start new motors up to MAX_CONCURRENT ----
        while (nextPending < pendingCount && runningCount < MAX_CONCURRENT) {
            activeTime = (hal_millis() - t0 - totalPauseMs) / 1000.0f;
            int mId  = pending[nextPending].id;
            int mAmt = pending[nextPending].amount;
            running[runningCount++] = {mId, mAmt, activeTime};
            hal_motorOn(mId);
            logf("Motor %d: START — %d ml (%.1fs)",
                 mId, mAmt, mAmt * SECONDS_PER_UNIT);
            nextPending++;

            // Inrush-current stagger (with stop-button polling)
            if (nextPending < pendingCount && runningCount < MAX_CONCURRENT) {
                logf("Inrush stagger %dms", STAGGER_DELAY_MS);
                unsigned long staggerEnd = hal_millis() + STAGGER_DELAY_MS;
                while (hal_millis() < staggerEnd && !hal_shouldQuit()) {
                    uint16_t bx, by;
                    if (hal_getTouch(bx, by) && by >= 200
                        && bx >= 20 && bx <= 158) {
                        cancelled = true;
                        break;
                    }
                    hal_delay(20);
                }
                if (cancelled) break;
            }
        }
        if (cancelled) break;

        activeTime = (hal_millis() - t0 - totalPauseMs) / 1000.0f;

        // ---- Check for completed motors ----
        for (int i = runningCount - 1; i >= 0; i--) {
            float dur = running[i].amount * SECONDS_PER_UNIT;
            if (activeTime - running[i].startTime >= dur) {
                hal_motorOff(running[i].id);
                finishedUnits += running[i].amount;
                logf("Motor %d: DONE — %d ml", running[i].id, running[i].amount);
                running[i] = running[--runningCount];   // swap-remove
            }
        }

        // ---- Update display periodically ----
        if (hal_millis() - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
            lastDisplayUpdate = hal_millis();
            MotorDisplay md[MAX_CONCURRENT];
            float runFrac = 0;
            for (int i = 0; i < runningCount; i++) {
                float dur  = running[i].amount * SECONDS_PER_UNIT;
                float frac = (activeTime - running[i].startTime) / dur;
                if (frac > 1.0f) frac = 1.0f;
                md[i] = {running[i].id, running[i].amount, (int)(frac * 100)};
                runFrac += running[i].amount * frac;
            }
            updatePouringScreen(mixId, md, runningCount, totalMotors,
                              finishedUnits + runFrac, (float)totalUnits,
                              activeTime, false);
        }

        hal_delay(50);
    }

    // Safety: ensure all motors off
    hal_allMotorsOff();

    float elapsed = (hal_millis() - t0 - totalPauseMs) / 1000.0f;
    PourStatus st = cancelled ? POUR_STOPPED : POUR_COMPLETE;
    if (cancelled)
        logf("Pour STOPPED after %.1fs", elapsed);
    else
        logf("Pour COMPLETE in %.1fs", elapsed);

    return {st, elapsed, totalMotors, totalUnits};
}

// ============================================
// APP SETUP (called by HAL after hardware init)
// ============================================
void app_setup() {
    hal_log("=== Syrup Touch Mixer ===");

    // Colours
    initColors();

    // Mix data
    loadMixes();

    // Initial screen
    resetGridColors();
    drawMainMenu();

    hal_log("System Ready. Waiting for input...");
}

// ============================================
// APP LOOP — state machine (called repeatedly by HAL)
// ============================================
void app_loop() {
    if (hal_shouldQuit()) return;

    // --- Heartbeat ---
    if (hal_millis() - lastHeartbeat >= HEARTBEAT_MS) {
        lastHeartbeat = hal_millis();
        heartbeatCount++;
        hal_ledOn();
        logf("ALIVE #%lu | state=%d | uptime=%lus",
             heartbeatCount, (int)appState, hal_millis() / 1000);
        hal_delay(50);
        hal_ledOff();
    }

    // --- Check for touch ---
    uint16_t tx, ty;
    if (!hal_getTouch(tx, ty)) {
        hal_delay(20);
        return;
    }

    // Bounds check
    if (tx >= SCREEN_W || ty >= SCREEN_H) {
        hal_delay(20);
        return;
    }

    // Map touch to 3×3 grid
    int col    = tx / BOX_W;  if (col > 2) col = 2;
    int row    = ty / BOX_H;  if (row > 2) row = 2;
    int number = row * 3 + col + 1;

    switch (appState) {

    // --------------------------------------------------
    case MAIN_MENU: {
        // Visual highlight — only redraw the touched cell
        drawGridCell(row, col, COL_YELLOW);

        // Measure press duration
        unsigned long pressStart = hal_millis();
        hal_waitForRelease();
        unsigned long duration = hal_millis() - pressStart;

        // Restore the touched cell
        drawGridCell(row, col, COL_DEFAULT);

        if (duration >= 500) {
            // ---- Long press → view mix ----
            passcodeLen = 0;
            currentViewMix = number;
            logf("Viewing Mix %d", number);
            for (int s = 0; s < 8; s++) {
                int amt = syrupData[number - 1][s];
                if (amt > 0)
                    logf("  Motor %d: %d ml", s + 1, amt);
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
                log("PASSCODE ACCEPTED!");
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
            hal_waitForRelease();
            appState = MAIN_MENU;
            resetGridColors();
            drawMainMenu();

        } else if (touchInRect(tx, ty, 170, 180, 140, 45)) {
            // POUR
            drawViewMix(currentViewMix, "POUR");
            hal_waitForRelease();

            PourResult result = executePour(currentViewMix);

            if (result.status != POUR_EMPTY) {
                drawSummaryScreen(currentViewMix, result.status,
                                  result.elapsed, result.motors,
                                  result.units);
                // Wait for OK tap
                uint16_t ox, oy;
                while (!hal_getTouch(ox, oy) && !hal_shouldQuit()) hal_delay(20);
                hal_waitForRelease();
            }

            appState = MAIN_MENU;
            resetGridColors();
            drawMainMenu();
        }
        break;
    }

    // --------------------------------------------------
    case SELECT_MIX: {
        hal_waitForRelease();
        currentEditMix = number;
        logf("Editing Mix %d", number);
        appState = EDIT_DASHBOARD;
        drawEditDashboard();
        break;
    }

    // --------------------------------------------------
    case EDIT_DASHBOARD: {
        hal_waitForRelease();
        if (number == 9) {
            // SAVE
            saveMixes();
            logf("Mix %d saved", currentEditMix);
            for (int s = 0; s < 8; s++) {
                int amt = syrupData[currentEditMix - 1][s];
                if (amt > 0)
                    logf("  Motor %d: %d ml", s + 1, amt);
                else
                    logf("  Motor %d: OFF", s + 1);
            }
            appState = MAIN_MENU;
            resetGridColors();
            drawMainMenu();
        } else {
            // Increment syrup value (0→1→…→10→0)
            int &val = syrupData[currentEditMix - 1][number - 1];
            val = (val + 1) % 11;
            logf("Motor %d set to %d (Mix %d)", number, val, currentEditMix);

            // Flash feedback then redraw just this cell
            int x1 = col * BOX_W;
            int y1 = row * BOX_H;
            hal_fillRect(x1, y1, BOX_W, BOX_H, COL_CYAN);
            hal_delay(50);
            drawEditCell(row, col);
        }
        break;
    }
    }  // switch

    hal_delay(20);
}
