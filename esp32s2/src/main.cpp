// ...existing code...

#include "hal.h"
#include "config.h"

// --- Modern rounded rectangle helper ---
// Auto-clamps radius so corners never overflow the rect.
static void fillRoundRect(int x, int y, int w, int h, int r, HalColor c) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) { hal_fillRect(x, y, w, h, c); return; }
    // Center band
    hal_fillRect(x + r, y, w - 2 * r, h, c);
    // Left / right bands (between corners)
    hal_fillRect(x, y + r, r, h - 2 * r, c);
    hal_fillRect(x + w - r, y + r, r, h - 2 * r, c);
    // Four corner circles
    hal_fillCircle(x + r,         y + r,         r, c);
    hal_fillCircle(x + w - r - 1, y + r,         r, c);
    hal_fillCircle(x + r,         y + h - r - 1, r, c);
    hal_fillCircle(x + w - r - 1, y + h - r - 1, r, c);
}
// Syrup Touch Mixer — Shared Application Logic
// Uses hal.h for all hardware access.
// Compiled for both ESP32-S2 (real) and native (SDL2 simulator).

#include "hal.h"
#include "config.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <cmath>

// ============================================
// APPLICATION STATE
// ============================================
enum AppState { MAIN_MENU, VIEW_MIX, SELECT_MIX, EDIT_DASHBOARD, CALIBRATE_DURATION, CLEANING_WARNING };
AppState appState = MAIN_MENU;
int warningMotorId = 1;
enum CalibrationPage { CALIB_PAGE_DURATION, CALIB_PAGE_CLEANING };
static CalibrationPage calibrationPage = CALIB_PAGE_DURATION;

enum UiLanguage { LANG_EN = 0, LANG_TR = 1 };
static UiLanguage currentLanguage = LANG_EN;

// ============================================
// MIX DATA — syrupData[mix 0-8][syrup 0-7]
// ============================================

int syrupData[9][8];
char mixNames[9][2][17]; // 2 lines, 16 chars + null each
int currentEditMix = 1;
int currentViewMix = 1;
float calibrationSecondsPerMotor[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
float cleaningThresholdLiters[8] = {10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f};
int cleaningUsedMl[8] = {0, 0, 0, 0, 0, 0, 0, 0};
unsigned long cleaningSnoozeUntilMs[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int calibrationMotorId = 1;
static volatile bool webActionPending = false;
static volatile int webPourMixId = 0;
static volatile int webMotorActionId = 0;
static volatile int webCalibrationMotorId = 0;
static volatile int webCalibrationDelta = 0;
static volatile bool webCalibrationReset = false;
static volatile bool webCalibrationSave = false;
static volatile bool webToggleBuzzerPending = false;
static volatile bool webToggleLanguagePending = false;
static volatile bool webToggleWifiPending = false;
static volatile int webActionKind = 0;
static bool manualMotorState[8] = {false, false, false, false, false, false, false, false};
static volatile bool webStopPourPending = false;
static volatile bool webPourActiveFlag = false;
static volatile int webPourCurrentMix = 0;
static volatile float webPourElapsedSec = 0.0f;
static volatile float webPourTotalUnits = 0.0f;
static volatile int webPourStatus = 0;
static volatile bool webPourIsPaused = false;

enum WebActionKind {
    WEB_ACTION_NONE = 0,
    WEB_ACTION_POUR = 1,
    WEB_ACTION_MOTOR_ON = 2,
    WEB_ACTION_MOTOR_OFF = 3,
    WEB_ACTION_TOGGLE_BUZZER = 4,
    WEB_ACTION_TOGGLE_LANGUAGE = 5,
    WEB_ACTION_TOGGLE_WIFI = 6,
    WEB_ACTION_SET_CALIB_MOTOR = 7,
    WEB_ACTION_ADJUST_CALIB = 8,
    WEB_ACTION_RESET_CALIB = 9,
    WEB_ACTION_SAVE_CALIB = 10,
};

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
const int STATUS_H = 20;     // top status bar height
const int GRID_Y   = STATUS_H;
const int GRID_H   = SCREEN_H - STATUS_H;
const int GAP      = 3;      // gap between cells
const int BOX_W    = SCREEN_W / 3;   // 106 — grid math unchanged for touch mapping
const int BOX_H    = GRID_H / 3;     // ~73
const int MAX_SYRUP_UNITS = 99; // allow values above 10 for service adjustments

// Config screen (SELECT_MIX) keeps the original top row and adds a second buzzer row.
static const int CONFIG_TOP_ROW_H = 36;
static const int CONFIG_BUZZER_ROW_H = 28;
static const int CONFIG_STATUS_H = CONFIG_TOP_ROW_H + CONFIG_BUZZER_ROW_H;
static const int CFG_BOX_H = (SCREEN_H - CONFIG_STATUS_H) / 3;
static const int CFG_WIFI_BTN_X = 110;
static const int CFG_BUZZER_ROW_X = 6;
static const int CFG_BUZZER_ROW_GAP = 4;
static const int CFG_BUZZER_SLOT_W = (SCREEN_W - 12 - CFG_BUZZER_ROW_GAP * 3) / 4;
static const int CFG_BUZZER_BTN_X = CFG_BUZZER_ROW_X;
static const int CFG_BUZZER_BTN_Y = CONFIG_TOP_ROW_H + 4;
static const int CFG_BUZZER_BTN_W = CFG_BUZZER_SLOT_W;
static const int CFG_BUZZER_BTN_H = CONFIG_BUZZER_ROW_H - 8;

HalColor gridColors[3][3];

// Per-mix color palette (muted, distinct, pleasant)
static HalColor MIX_COLORS[9];
static HalColor MIX_COLORS_LIGHT[9];  // brighter version for press highlight

// ============================================
// COLOUR PALETTE
// ============================================
static HalColor COL_BLACK, COL_WHITE, COL_YELLOW, COL_CYAN;
static HalColor COL_DEFAULT, COL_PURPLE, COL_TEAL, COL_DARK_TEAL, COL_DARK_BLUE;
static HalColor COL_DARK_RED, COL_DARK_GREEN, COL_LIGHT_RED, COL_LIGHT_GREEN, COL_RED;
static HalColor COL_CYAN_BAR, COL_GREEN_BAR, COL_STOP, COL_PAUSE, COL_RESUME;
static HalColor COL_OK_BTN, COL_GRAY, COL_LIGHT_GRAY, COL_TITLE_YELLOW, COL_PAUSE_TITLE;
static bool buzzerEnabled = true;

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

    // Per-mix colors — muted, distinct hues
    MIX_COLORS[0] = hal_color(45, 80, 120);   // steel blue
    MIX_COLORS[1] = hal_color(100, 60, 110);  // muted purple
    MIX_COLORS[2] = hal_color(30, 100, 90);   // teal
    MIX_COLORS[3] = hal_color(120, 70, 40);   // warm brown
    MIX_COLORS[4] = hal_color(90, 40, 60);    // muted burgundy
    MIX_COLORS[5] = hal_color(40, 90, 60);    // forest green
    MIX_COLORS[6] = hal_color(100, 85, 30);   // olive gold
    MIX_COLORS[7] = hal_color(60, 70, 110);   // slate blue
    MIX_COLORS[8] = hal_color(80, 50, 80);    // plum

    // Lighter versions for press highlight (+60 clamped)
    for (int i = 0; i < 9; i++) {
        int r = ((MIX_COLORS[i] >> 16) & 0xFF) + 60;
        int g = ((MIX_COLORS[i] >> 8)  & 0xFF) + 60;
        int b = ( MIX_COLORS[i]        & 0xFF) + 60;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        MIX_COLORS_LIGHT[i] = hal_color(r, g, b);
    }
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

static void drawMainMenu();
static void drawSelectMix();
static void drawEditDashboard();
static void drawCalibrationScreen();
static void drawViewMix(int mixId, const char *activeButton = nullptr);
static void drawSummaryScreen(int mixId, PourStatus status, float elapsed, int motorsUsed, int totalUnits);
static PourResult executePour(int mixId);

int app_webCurrentState() { return (int)appState; }
int app_webCurrentViewMix() { return currentViewMix; }
int app_webCurrentEditMix() { return currentEditMix; }
bool app_webBuzzerEnabled() { return buzzerEnabled; }
int app_webLanguage() { return (int)currentLanguage; }
bool app_webWifiActive() { return hal_wifiIsActive(); }
int app_webCurrentCalibrationMotor() { return calibrationMotorId; }
void app_webGetCalibrationValue(int motorId, float &value) {
    value = 0.0f;
    if (motorId >= 1 && motorId <= 8) value = calibrationSecondsPerMotor[motorId - 1];
}
bool app_webMotorIsManualOn(int motorId) {
    if (motorId < 1 || motorId > 8) return false;
    return manualMotorState[motorId - 1];
}
void app_webGetMixValue(int mixId, int motorId, int &value) {
    value = 0;
    if (mixId >= 1 && mixId <= 9 && motorId >= 1 && motorId <= 8) value = syrupData[mixId - 1][motorId - 1];
}
const char* app_webMixName(int mixId, int line) {
    static const char *empty = "";
    if (mixId < 1 || mixId > 9 || line < 0 || line > 1) return empty;
    return mixNames[mixId - 1][line];
}
bool app_webPourActive() { return webPourActiveFlag; }
int app_webPourMix() { return webPourCurrentMix; }
const char* app_webPourStatusText() {
    if (!webPourActiveFlag) return "Idle";
    if (webPourIsPaused) return "Paused";
    return "Pouring";
}
float app_webPourElapsed() { return webPourElapsedSec; }
float app_webPourTotal() { return webPourTotalUnits * 10.0f; }
void app_webRequestStopPour() { webStopPourPending = true; }
bool app_webHasPendingWebAction() { return webActionPending; }
void app_webClearPendingWebAction() { webActionPending = false; webActionKind = WEB_ACTION_NONE; }
void app_webRequestPourMix(int mixId) { webPourMixId = mixId; webActionKind = WEB_ACTION_POUR; webActionPending = true; }
void app_webRequestMotorOn(int motorId) { webMotorActionId = motorId; webActionKind = WEB_ACTION_MOTOR_ON; webActionPending = true; }
void app_webRequestMotorOff(int motorId) { webMotorActionId = motorId; webActionKind = WEB_ACTION_MOTOR_OFF; webActionPending = true; }
void app_webRequestToggleBuzzer() { webActionKind = WEB_ACTION_TOGGLE_BUZZER; webActionPending = true; }
void app_webRequestToggleLanguage() { webActionKind = WEB_ACTION_TOGGLE_LANGUAGE; webActionPending = true; }
void app_webRequestToggleWifi() { webActionKind = WEB_ACTION_TOGGLE_WIFI; webActionPending = true; }
void app_webRequestSetCalibrationMotor(int motorId) { webCalibrationMotorId = motorId; webActionKind = WEB_ACTION_SET_CALIB_MOTOR; webActionPending = true; }
void app_webRequestAdjustCalibration(int delta) { webCalibrationDelta = delta; webActionKind = WEB_ACTION_ADJUST_CALIB; webActionPending = true; }
void app_webRequestResetCalibration() { webCalibrationReset = true; webActionKind = WEB_ACTION_RESET_CALIB; webActionPending = true; }
void app_webRequestSaveCalibration() { webCalibrationSave = true; webActionKind = WEB_ACTION_SAVE_CALIB; webActionPending = true; }

// ============================================
// TOUCH HELPERS
// ============================================
static bool touchInRect(uint16_t tx, uint16_t ty,
                        int rx, int ry, int rw, int rh) {
    return (int)tx >= rx && (int)tx <= rx + rw &&
           (int)ty >= ry && (int)ty <= ry + rh;
}

static float effectiveSecondsPerUnit(int motorId) {
    if (motorId >= 1 && motorId <= 8) {
        return SECONDS_PER_UNIT + calibrationSecondsPerMotor[motorId - 1]/2;
    }
    return SECONDS_PER_UNIT;
}

// ============================================
// BUZZER HELPERS
// ============================================
static const char *BUZZER_CONFIG_FILE = "/buzzer_config.json";
static const char *LANGUAGE_CONFIG_FILE = "/language_config.json";
static const char *UI_TRANSLATIONS_FILE = "/ui_translations.csv";

enum UiTextId {
    TXT_STATUS_TITLE,
    TXT_STATUS_HINT,
    TXT_RESERVED,
    TXT_BUILD,
    TXT_WIFI_ON,
    TXT_WIFI_OFF,
    TXT_BUZZER,
    TXT_ON,
    TXT_OFF,
    TXT_LANGUAGE,
    TXT_LANG_DETAIL_EN,
    TXT_LANG_DETAIL_TR,
    TXT_SAVE,
    TXT_BACK,
    TXT_EXIT_HOME,
    TXT_POUR,
    TXT_TOTAL_EST_FMT,
    TXT_MOTOR_EST_FMT,
    TXT_VIEW_TITLE_FMT,
    TXT_SYRUP_LABEL_FMT,
    TXT_PAUSED_MIX_FMT,
    TXT_POURING_MIX_FMT,
    TXT_STOP,
    TXT_RESUME,
    TXT_PAUSE,
    TXT_OVERALL_FMT,
    TXT_TIME_REMAINING_FMT,
    TXT_ACTIVE_MOTORS_FMT,
    TXT_POUR_COMPLETE,
    TXT_POUR_STOPPED,
    TXT_SUMMARY_MIX_FMT,
    TXT_SUMMARY_TIME_FMT,
    TXT_SUMMARY_MOTORS_FMT,
    TXT_SUMMARY_VOLUME_FMT,
    TXT_OK,
    TXT_COUNT,
};

static const size_t UI_TEXT_MAX_LEN = 64;
static char uiTextTable[2][TXT_COUNT][UI_TEXT_MAX_LEN];

struct UiTextKeyMap {
    const char *key;
    UiTextId id;
};

static const UiTextKeyMap UI_TEXT_KEYS[] = {
    {"status_title", TXT_STATUS_TITLE},
    {"status_hint", TXT_STATUS_HINT},
    {"reserved", TXT_RESERVED},
    {"build_label", TXT_BUILD},
    {"wifi_on", TXT_WIFI_ON},
    {"wifi_off", TXT_WIFI_OFF},
    {"buzzer_label", TXT_BUZZER},
    {"state_on", TXT_ON},
    {"state_off", TXT_OFF},
    {"language_label", TXT_LANGUAGE},
    {"language_detail_en", TXT_LANG_DETAIL_EN},
    {"language_detail_tr", TXT_LANG_DETAIL_TR},
    {"save", TXT_SAVE},
    {"back", TXT_BACK},
    {"exit_home", TXT_EXIT_HOME},
    {"pour", TXT_POUR},
    {"total_est_fmt", TXT_TOTAL_EST_FMT},
    {"motor_est_fmt", TXT_MOTOR_EST_FMT},
    {"view_title_fmt", TXT_VIEW_TITLE_FMT},
    {"syrup_label_fmt", TXT_SYRUP_LABEL_FMT},
    {"paused_mix_fmt", TXT_PAUSED_MIX_FMT},
    {"pouring_mix_fmt", TXT_POURING_MIX_FMT},
    {"stop", TXT_STOP},
    {"resume", TXT_RESUME},
    {"pause", TXT_PAUSE},
    {"overall_fmt", TXT_OVERALL_FMT},
    {"time_remaining_fmt", TXT_TIME_REMAINING_FMT},
    {"active_motors_fmt", TXT_ACTIVE_MOTORS_FMT},
    {"pour_complete", TXT_POUR_COMPLETE},
    {"pour_stopped", TXT_POUR_STOPPED},
    {"summary_mix_fmt", TXT_SUMMARY_MIX_FMT},
    {"summary_time_fmt", TXT_SUMMARY_TIME_FMT},
    {"summary_motors_fmt", TXT_SUMMARY_MOTORS_FMT},
    {"summary_volume_fmt", TXT_SUMMARY_VOLUME_FMT},
    {"ok", TXT_OK},
};

static bool isTurkishUi() {
    return currentLanguage == LANG_TR;
}

static void copyUiText(char dst[UI_TEXT_MAX_LEN], const char *src) {
    strncpy(dst, src, UI_TEXT_MAX_LEN - 1);
    dst[UI_TEXT_MAX_LEN - 1] = '\0';
}

static void initDefaultUiTexts() {
    static const char *EN[TXT_COUNT] = {
        "SYRUP MIXER",
        "LONG PRESS TO VIEW & POUR",
        "Reserved",
        "BUILD:",
        "WiFi ON",
        "WiFi OFF",
        "Buzzer",
        "ON",
        "OFF",
        "Lang",
        "EN",
        "TR",
        "SAVE/EXIT",
        "BACK",
        "EXIT",
        "POUR",
        "Total est: %d ml / %.1fs",
        "M%d: %d ml",
        "--- MIX %d CONTENTS ---",
        "Syrup %d: %d",
        "PAUSED MIX %d",
        "POURING MIX %d",
        "STOP",
        "RESUME",
        "PAUSE",
        "Overall %d%%",
        "Elapsed: %.1fs Remaining: ~%.1fs",
        "Active motors (%d/%d total)",
        "POUR COMPLETE",
        "POUR STOPPED",
        "Mix %d",
        "Time: %.1fs",
        "Motors: %d",
        "Volume: %d ml",
        "OK",
    };
    static const char *TR[TXT_COUNT] = {
        "SURUP MIKSER",
        "UZUN BAS: GOR VE DOK",
        "Bos",
        "DERLEME:",
        "WiFi Acik",
        "WiFi Kapali",
        "Ses",
        "Acik",
        "Kapali",
        "Dil",
        "EN",
        "TR",
        "KAYDET/ÇIK",
        "GERI",
        "ÇIKIŞ",
        "DOK",
        "Toplam tahm.: %d ml / %.1fs",
        "M%d: %d ml",
        "--- KARISIM %d ---",
        "Surup %d: %d",
        "BEKLEMEDE %d",
        "DOKUM KARISIM %d",
        "DUR",
        "DEVAM",
        "BEKLET",
        "Genel %d%%",
        "Gecen: %.1fs Kalan: ~%.1fs",
        "Aktif motor (%d/%d)",
        "DOKUM TAMAM",
        "DOKUM DURDU",
        "Karisim %d",
        "Sure: %.1fs",
        "Motor: %d",
        "Hacim: %d ml",
        "TAMAM",
    };

    for (int i = 0; i < TXT_COUNT; i++) {
        copyUiText(uiTextTable[LANG_EN][i], EN[i]);
        copyUiText(uiTextTable[LANG_TR][i], TR[i]);
    }
}

static char *trimAscii(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

static int uiTextIdFromKey(const char *key) {
    for (size_t i = 0; i < sizeof(UI_TEXT_KEYS) / sizeof(UI_TEXT_KEYS[0]); i++) {
        if (strcmp(UI_TEXT_KEYS[i].key, key) == 0) return (int)UI_TEXT_KEYS[i].id;
    }
    return -1;
}

static void loadUiTranslations() {
    initDefaultUiTexts();

    char buf[4096];
    int n = hal_readFile(UI_TRANSLATIONS_FILE, buf, sizeof(buf));
    if (n <= 0) return;

    char *saveLine = nullptr;
    for (char *line = strtok_r(buf, "\n", &saveLine);
         line != nullptr;
         line = strtok_r(nullptr, "\n", &saveLine)) {
        char *trimmed = trimAscii(line);
        if (!trimmed[0] || trimmed[0] == '#') continue;

        char *c1 = strchr(trimmed, ',');
        if (!c1) continue;
        *c1 = '\0';
        char *c2 = strchr(c1 + 1, ',');
        if (!c2) continue;
        *c2 = '\0';

        char *key = trimAscii(trimmed);
        char *en = trimAscii(c1 + 1);
        char *tr = trimAscii(c2 + 1);
        if (strcmp(key, "key") == 0) continue;

        int id = uiTextIdFromKey(key);
        if (id < 0) continue;

        copyUiText(uiTextTable[LANG_EN][id], en);
        copyUiText(uiTextTable[LANG_TR][id], tr);
    }
}

static const char *uiText(UiTextId id) {
    return uiTextTable[isTurkishUi() ? LANG_TR : LANG_EN][id];
}

static void uiTextFormat(char *buf, size_t bufSize, UiTextId id, ...) {
    va_list ap;
    va_start(ap, id);
    vsnprintf(buf, bufSize, uiText(id), ap);
    va_end(ap);
}

static bool hasNonAscii(const char *str) {
    if (!str) return false;
    for (const unsigned char *p = (const unsigned char *)str; *p; ++p) {
        if (*p >= 128) return true;
    }
    return false;
}

static void transliterateTurkishAscii(const char *src, char *dst, size_t dstSize) {
    if (!src || !dst || dstSize == 0) return;

    size_t out = 0;
    for (size_t i = 0; src[i] && out + 1 < dstSize; ) {
        unsigned char c = (unsigned char)src[i];
        if (c < 128) {
            dst[out++] = (char)c;
            i++;
            continue;
        }

        unsigned char c2 = (unsigned char)src[i + 1];
        char repl = '?';
        if (c == 0xC3) {
            if (c2 == 0x87) repl = 'C';
            else if (c2 == 0xA7) repl = 'c';
            else if (c2 == 0x96) repl = 'O';
            else if (c2 == 0xB6) repl = 'o';
            else if (c2 == 0x9C) repl = 'U';
            else if (c2 == 0xBC) repl = 'u';
        } else if (c == 0xC4) {
            if (c2 == 0x9E) repl = 'G';
            else if (c2 == 0x9F) repl = 'g';
            else if (c2 == 0xB0) repl = 'I';
            else if (c2 == 0xB1) repl = 'i';
        } else if (c == 0xC5) {
            if (c2 == 0x9E) repl = 'S';
            else if (c2 == 0x9F) repl = 's';
        }

        dst[out++] = repl;
        i += (c2 != 0) ? 2 : 1;
    }
    dst[out] = '\0';
}

static int turkishSizeForFont(int font) {
    switch (font) {
        case 1: return 7;
        case 2: return 9;
        default: return 12;
    }
}

static void drawUiString(const char *str, int x, int y, int font) {
    if (font == 1 && hasNonAscii(str)) {
        char asciiBuf[128];
        transliterateTurkishAscii(str, asciiBuf, sizeof(asciiBuf));
        hal_drawString(asciiBuf, x, y, font);
    } else if (hasNonAscii(str)) {
        hal_drawString_Turkish(str, x, y, turkishSizeForFont(font));
    } else {
        hal_drawString(str, x, y, font);
    }
}

static int buttonFontForLabel(const char *label, int preferredFont) {
    size_t len = label ? strlen(label) : 0;
    if (preferredFont >= 4 && len >= 5) return 2;
    return preferredFont;
}

static void beepTouch() {
    if (!buzzerEnabled) return;
    hal_buzzerTone(2800, 45);
}

static void beepPourDone() {
    if (!buzzerEnabled) return;
    // Friendly, attention-grabbing pattern that is easier to hear in a noisy kitchen
    hal_buzzerTone(1800, 220);
    hal_delay(90);
    hal_buzzerTone(2400, 260);
    hal_delay(90);
    hal_buzzerTone(2800, 360);
    hal_delay(120);
    hal_buzzerTone(2800, 360);
}

static void beepPourStopped() {
    if (!buzzerEnabled) return;
    // Clear but softer warning pattern
    hal_buzzerTone(1000, 220);
    hal_delay(120);
    hal_buzzerTone(800, 360);
}

// ============================================
// PERSISTENT STORAGE (HAL + manual JSON)
// ============================================
static const char *DATA_FILE = "/syrup_mixes.json";
static const char *CALIBRATION_CONFIG_FILE = "/calibration_config.json";

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

static float jsonGetFloat(const char *json, const char *key, float fallback) {
    const char *p = json;
    size_t klen = strlen(key);
    while ((p = strstr(p, key)) != nullptr) {
        if (p > json && *(p - 1) == '"') {
            p += klen;
            while (*p && (*p == '"' || *p == ':' || *p == ' ' || *p == '\t')) p++;
            if (*p == '-' || (*p >= '0' && *p <= '9')) {
                return strtof(p, nullptr);
            }
        }
        p++;
    }
    return fallback;
}

static void loadBuzzerConfig() {
    char buf[64];
    int n = hal_readFile(BUZZER_CONFIG_FILE, buf, sizeof(buf));
    if (n <= 0) {
        buzzerEnabled = true;
        return;
    }
    buzzerEnabled = jsonGetInt(buf, "enabled", 1) != 0;
}

static void saveBuzzerConfig() {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "{\"enabled\":%d}", buzzerEnabled ? 1 : 0);
    if (!hal_writeFile(BUZZER_CONFIG_FILE, buf, len)) {
        log("ERROR: failed to write buzzer config");
    }
}

static void loadLanguageConfig() {
    char buf[64];
    int n = hal_readFile(LANGUAGE_CONFIG_FILE, buf, sizeof(buf));
    if (n <= 0) {
        currentLanguage = LANG_EN;
        return;
    }
    currentLanguage = jsonGetInt(buf, "lang", 0) == 1 ? LANG_TR : LANG_EN;
}

static void saveLanguageConfig() {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "{\"lang\":%d}", currentLanguage == LANG_TR ? 1 : 0);
    if (!hal_writeFile(LANGUAGE_CONFIG_FILE, buf, len)) {
        log("ERROR: failed to write language config");
    }
}

static void loadCalibrationConfig() {
    char buf[512];
    int n = hal_readFile(CALIBRATION_CONFIG_FILE, buf, sizeof(buf));
    if (n <= 0) {
        for (int i = 0; i < 8; i++) {
            calibrationSecondsPerMotor[i] = 0.0f;
            cleaningThresholdLiters[i] = 10.0f;
            cleaningUsedMl[i] = 0;
        }
        return;
    }
    char keyT[16], keyU[16];
    for (int i = 0; i < 8; i++) {
        char key[16];
        snprintf(key, sizeof(key), "motor_%d", i + 1);
        calibrationSecondsPerMotor[i] = jsonGetFloat(buf, key, 0.0f);
        
        snprintf(keyT, sizeof(keyT), "cThresh_%d", i + 1);
        cleaningThresholdLiters[i] = jsonGetFloat(buf, keyT, 10.0f);
        
        snprintf(keyU, sizeof(keyU), "cUsed_%d", i + 1);
        cleaningUsedMl[i] = (int)jsonGetFloat(buf, keyU, 0.0f);
    }
}

static void saveCalibrationConfig() {
    char buf[512];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{");
    for (int i = 0; i < 8; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s\"motor_%d\":%.2f,\"cThresh_%d\":%.2f,\"cUsed_%d\":%d",
                        (i > 0) ? "," : "",
                        i + 1, calibrationSecondsPerMotor[i],
                        i + 1, cleaningThresholdLiters[i],
                        i + 1, cleaningUsedMl[i]);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "}");
    if (!hal_writeFile(CALIBRATION_CONFIG_FILE, buf, pos)) {
        log("ERROR: failed to write calibration config");
    }
}

static void processPendingWebAction() {
    if (!webActionPending) return;
    int action = webActionKind;
    webActionPending = false;
    webActionKind = WEB_ACTION_NONE;

    switch (action) {
        case WEB_ACTION_POUR: {
            if (webPourMixId >= 1 && webPourMixId <= 9) {
                currentViewMix = webPourMixId;
                appState = VIEW_MIX;
                drawViewMix(currentViewMix);
                PourResult result = executePour(currentViewMix);
                drawSummaryScreen(currentViewMix, result.status, result.elapsed, result.motors, result.units);
            }
            break;
        }
        case WEB_ACTION_MOTOR_ON:
            if (webMotorActionId >= 1 && webMotorActionId <= 8) {
                hal_motorOn(webMotorActionId);
                manualMotorState[webMotorActionId - 1] = true;
            }
            break;
        case WEB_ACTION_MOTOR_OFF:
            if (webMotorActionId >= 1 && webMotorActionId <= 8) {
                hal_motorOff(webMotorActionId);
                manualMotorState[webMotorActionId - 1] = false;
            }
            break;
        case WEB_ACTION_TOGGLE_BUZZER: {
            bool wasEnabled = buzzerEnabled;
            buzzerEnabled = !buzzerEnabled;
            saveBuzzerConfig();
            if (!wasEnabled && buzzerEnabled) beepTouch();
            break;
        }
        case WEB_ACTION_TOGGLE_LANGUAGE:
            currentLanguage = (currentLanguage == LANG_EN) ? LANG_TR : LANG_EN;
            saveLanguageConfig();
            break;
        case WEB_ACTION_TOGGLE_WIFI:
            if (hal_wifiIsActive()) hal_wifiStop(); else hal_wifiStart();
            break;
        case WEB_ACTION_SET_CALIB_MOTOR:
            if (webCalibrationMotorId >= 1 && webCalibrationMotorId <= 8) {
                calibrationMotorId = webCalibrationMotorId;
                appState = CALIBRATE_DURATION;
                drawCalibrationScreen();
            }
            break;
        case WEB_ACTION_ADJUST_CALIB:
            if (calibrationMotorId >= 1 && calibrationMotorId <= 8) {
                calibrationSecondsPerMotor[calibrationMotorId - 1] += (float)webCalibrationDelta * 0.1f;
                drawCalibrationScreen();
            }
            break;
        case WEB_ACTION_RESET_CALIB:
            for (int i = 0; i < 8; i++) calibrationSecondsPerMotor[i] = 0.0f;
            drawCalibrationScreen();
            break;
        case WEB_ACTION_SAVE_CALIB:
            saveCalibrationConfig();
            break;
        default:
            break;
    }
}

// Helper: extract a string value from a JSON object ("key": ["line1", "line2"])
static void jsonGetMixName(const char *json, const char *key, char out[2][17]) {
    const char *p = strstr(json, key);
    if (!p) { out[0][0] = 0; out[1][0] = 0; return; }
    p = strchr(p, '[');
    if (!p) { out[0][0] = 0; out[1][0] = 0; return; }
    p++;
    for (int i = 0; i < 2; i++) {
        while (*p && *p != '"') p++;
        if (!*p) { out[i][0] = 0; continue; }
        p++;
        int j = 0;
        while (*p && *p != '"' && j < 16) out[i][j++] = *p++;
        out[i][j] = 0;
        while (*p && *p != ',' && *p != ']') p++;
        if (*p == ',') p++;
    }
}

static void loadMixes() {
    char buf[4096];
    int n = hal_readFile(DATA_FILE, buf, sizeof(buf));
    if (n <= 0) {
        log("No data file — using defaults");
        memset(syrupData, 0, sizeof(syrupData));
        for (int i = 0; i < 9; ++i) { mixNames[i][0][0] = 0; mixNames[i][1][0] = 0; }
        return;
    }

    // Parse: top-level keys "1".."9", each containing "syrup_1".."syrup_8" and "name"
    for (int m = 1; m <= 9; m++) {
        char mixKey[8];
        snprintf(mixKey, sizeof(mixKey), "\"%d\"", m);
        const char *mixStart = strstr(buf, mixKey);
        if (!mixStart) continue;
        const char *objStart = strchr(mixStart, '{');
        if (!objStart) continue;
        const char *objEnd = strchr(objStart, '}');
        if (!objEnd) continue;
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
        // Load mix name (2 lines)
        jsonGetMixName(objBuf, "name", mixNames[m - 1]);
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
                            "    \"syrup_%d\": %d,\n",
                            s, syrupData[m - 1][s - 1]);
        }
        // Write mix name as JSON array
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "    \"name\": [\"%s\", \"%s\"]\n",
            mixNames[m - 1][0], mixNames[m - 1][1]);
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
        for (int c = 0; c < 3; c++) {
            int mix = r * 3 + c;
            gridColors[r][c] = MIX_COLORS[mix];
        }
}

// Count how many syrups are configured for a mix (0-8)
static int mixSyrupCount(int mixIdx) {
    int count = 0;
    for (int s = 0; s < 8; s++)
        if (syrupData[mixIdx][s] > 0) count++;
    return count;
}

// ============================================
// UI SCREENS
// ============================================

// WiFi icon position in status bar (tappable area)
static const int WIFI_ICON_X = SCREEN_W - 18;
static const int WIFI_ICON_W = 16;

static void drawWifiIcon() {
    HalColor bg = hal_color(20, 20, 30);
    hal_fillRect(WIFI_ICON_X - 2, 0, WIFI_ICON_W + 4, STATUS_H - 1, bg);

    const bool active = hal_wifiIsActive();
    HalColor activeColor = hal_color(120, 255, 140);
    HalColor inactiveColor = hal_color(95, 105, 120);
    HalColor color = active ? activeColor : inactiveColor;
    int baseX = WIFI_ICON_X + 1;
    int baseY = STATUS_H - 4;
    const int barWidth = 2;
    const int barGap = 1;
    const int barHeights[4] = {3, 5, 7, 9};

    hal_fillCircle(baseX, baseY, 1, color);
    for (int i = 0; i < 4; i++) {
        int x = baseX + 3 + i * (barWidth + barGap);
        int h = barHeights[i];
        int y = baseY - h + 1;
        if (active) {
            hal_fillRect(x, y, barWidth, h, color);
        } else {
            hal_drawRect(x, y, barWidth, h, color);
        }
    }
}

// --- Status bar at top ---
static void drawStatusBar() {
    hal_fillRect(0, 0, SCREEN_W, STATUS_H, hal_color(20, 20, 30));
    hal_drawHLine(0, STATUS_H - 1, SCREEN_W, COL_GRAY);
    hal_setTextDatum(HAL_DATUM_ML);
    hal_setTextColor(hal_color(180, 180, 200), hal_color(20, 20, 30));
    drawUiString(uiText(TXT_STATUS_TITLE), 6, STATUS_H / 2, 1);
    hal_setTextDatum(HAL_DATUM_MR);
    hal_setTextColor(hal_color(180, 180, 200), hal_color(20, 20, 30));
    drawUiString(uiText(TXT_STATUS_HINT), SCREEN_W - 24, STATUS_H / 2, 1);
    drawWifiIcon();
}

// --- Draw a single grid cell with gap, no circle, syrup dots ---
static void drawGridCell(int row, int col, HalColor color, bool pressed = false) {
    int num = row * 3 + col + 1;

    // Cell rect with gaps
    int x1 = col * BOX_W + GAP;
    int y1 = GRID_Y + row * BOX_H + GAP;
    int cw = BOX_W - GAP * 2;
    int ch = BOX_H - GAP * 2;

    // Darken base color when pressed
    HalColor base = pressed ? hal_color(
        (((color >> 16) & 0xFF) > 40) ? (((color >> 16) & 0xFF) - 40) : 0,
        (((color >> 8)  & 0xFF) > 40) ? (((color >> 8)  & 0xFF) - 40) : 0,
        (( color        & 0xFF) > 40) ? (( color        & 0xFF) - 40) : 0
    ) : color;

    // Fill cell background
    hal_fillRect(x1, y1, cw, ch, base);

    if (pressed) {
        // Pressed: shadow on top, highlight on bottom → sunken look
        HalColor shadow = hal_color(
            (((base >> 16) & 0xFF) > 30) ? (((base >> 16) & 0xFF) - 30) : 0,
            (((base >> 8)  & 0xFF) > 30) ? (((base >> 8)  & 0xFF) - 30) : 0,
            (( base        & 0xFF) > 30) ? (( base        & 0xFF) - 30) : 0
        );
        hal_fillRect(x1, y1, cw, 3, shadow);

        HalColor highlight = hal_color(
            (((base >> 16) & 0xFF) + 25) > 255 ? 255 : (((base >> 16) & 0xFF) + 25),
            (((base >> 8)  & 0xFF) + 25) > 255 ? 255 : (((base >> 8)  & 0xFF) + 25),
            (( base        & 0xFF) + 25) > 255 ? 255 : (( base        & 0xFF) + 25)
        );
        hal_fillRect(x1, y1 + ch - 3, cw, 3, highlight);
    } else {
        // Unpressed: highlight on top, shadow on bottom → raised look
        HalColor highlight = hal_color(
            (((color >> 16) & 0xFF) + 30) > 255 ? 255 : (((color >> 16) & 0xFF) + 30),
            (((color >> 8)  & 0xFF) + 30) > 255 ? 255 : (((color >> 8)  & 0xFF) + 30),
            (( color        & 0xFF) + 30) > 255 ? 255 : (( color        & 0xFF) + 30)
        );
        hal_fillRect(x1, y1, cw, 3, highlight);

        HalColor shadow = hal_color(
            (((color >> 16) & 0xFF) > 25) ? (((color >> 16) & 0xFF) - 25) : 0,
            (((color >> 8)  & 0xFF) > 25) ? (((color >> 8)  & 0xFF) - 25) : 0,
            (( color        & 0xFF) > 25) ? (( color        & 0xFF) - 25) : 0
        );
        hal_fillRect(x1, y1 + ch - 3, cw, 3, shadow);
    }

    // Content offset: shift down+right 2px when pressed
    int ox = pressed ? 2 : 0;
    int oy = pressed ? 2 : 0;

    // Number — top-left corner, configurable font size
    int num_x = x1 + 8 + ox;
    int num_y = y1 + 8 + oy;
    hal_setTextDatum(HAL_DATUM_TL);
    hal_setTextColor(COL_WHITE, base);
    hal_drawNumber(num, num_x, num_y, FONT_SIZE_BOXNUMBER / 20);

    // Mix name — two lines, centered lower in cell, configurable font size
    int name_cx = x1 + cw / 2 + ox;
    int name_cy = y1 + (ch * 0.60) + oy; // move names to ~10% from top
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, base);
    int name_offset = 10; // vertical offset between lines
    // Use Turkish-aware drawString for mix names
    if (mixNames[num - 1][0][0])
        hal_drawString_Turkish(mixNames[num - 1][0], name_cx, name_cy - name_offset, FONT_SIZE_BOXNAME);
    if (mixNames[num - 1][1][0])
        hal_drawString_Turkish(mixNames[num - 1][1], name_cx, name_cy + name_offset, FONT_SIZE_BOXNAME);
}

static void drawConfigToggleButton(int x, int y, int w, int h,
                                   bool on, const char *label, const char *detail) {
    HalColor btnBg = on ? hal_color(0, 100, 70) : hal_color(55, 55, 65);
    fillRoundRect(x, y, w, h, 6, btnBg);

    int dotX = x + 14;
    int dotY = y + h / 2;
    if (on) {
        hal_fillCircle(dotX, dotY, 5, hal_color(0, 230, 160));
    } else {
        hal_drawCircle(dotX, dotY, 5, hal_color(140, 140, 150));
    }

    hal_setTextDatum(HAL_DATUM_ML);
    hal_setTextColor(COL_WHITE, btnBg);
    drawUiString(label, dotX + 10, dotY - 6, 1);

    HalColor detailColor = on ? hal_color(200, 200, 120) : hal_color(160, 160, 170);
    hal_setTextColor(detailColor, btnBg);
    drawUiString(detail, dotX + 10, dotY + 7, 1);
}

static void drawConfigReservedButton(int x, int y, int w, int h) {
    HalColor btnBg = hal_color(45, 45, 54);
    fillRoundRect(x, y, w, h, 6, btnBg);
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(hal_color(150, 150, 160), btnBg);
    drawUiString(uiText(TXT_RESERVED), x + w / 2, y + h / 2 - 5, 1);
    hal_setTextColor(hal_color(110, 110, 120), btnBg);
    hal_drawString("--", x + w / 2, y + h / 2 + 7, 1);
}

static void drawConfigHomeButton(int x, int y, int w, int h) {
    HalColor btnBg = hal_color(65, 55, 90);
    fillRoundRect(x, y, w, h, 6, btnBg);
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(hal_color(240, 220, 255), btnBg);
    drawUiString(uiText(TXT_EXIT_HOME), x + w / 2, y + h / 2 - 2, 1);
}

// --- Config screen status bar ---
static void drawConfigStatusBar() {
    HalColor barBg = hal_color(30, 20, 40);
    hal_fillRect(0, 0, SCREEN_W, CONFIG_STATUS_H, barBg);
    hal_drawHLine(0, CONFIG_TOP_ROW_H - 1, SCREEN_W, COL_GRAY);
    hal_drawHLine(0, CONFIG_STATUS_H - 1, SCREEN_W, COL_GRAY);

    // Build date/time label (DD-MM-YY HH:MM)
    // __DATE__ = "Mmm DD YYYY", __TIME__ = "HH:MM:SS"
    static const char _bd[] = __DATE__;   // e.g. "Apr  9 2026"
    static const char _bt[] = __TIME__;
    static const char *_months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    static char _buildStr[20] = {};
    if (_buildStr[0] == '\0') {
        int mm = 0;
        for (int i = 0; i < 12; i++)
            if (_bd[0]==_months[i*3] && _bd[1]==_months[i*3+1] && _bd[2]==_months[i*3+2]) { mm=i+1; break; }
        int dd = (_bd[4]==' ' ? 0 : (_bd[4]-'0')*10) + (_bd[5]-'0');
        int yy = (_bd[9]-'0')*10 + (_bd[10]-'0');
        snprintf(_buildStr, sizeof(_buildStr), "%02d-%02d-%02d %.5s", dd, mm, yy, _bt);
    }
    hal_setTextDatum(HAL_DATUM_ML);
    hal_setTextColor(hal_color(140, 130, 160), barBg);
    drawUiString(uiText(TXT_BUILD), 6, 10, 1);
    drawUiString(_buildStr, 6, 24, 1);

    // WiFi toggle button (original top row layout)
    bool on = hal_wifiIsActive();
    int btnX = CFG_WIFI_BTN_X, btnY = 4, btnW = SCREEN_W - CFG_WIFI_BTN_X - 4, btnH = CONFIG_TOP_ROW_H - 8;
    HalColor btnBg = on ? hal_color(0, 100, 70) : hal_color(55, 55, 65);
    fillRoundRect(btnX, btnY, btnW, btnH, 6, btnBg);

    int dotX = btnX + 14, dotY = btnY + btnH / 2;
    if (on) {
        hal_fillCircle(dotX, dotY, 5, hal_color(0, 230, 160));
    } else {
        hal_drawCircle(dotX, dotY, 5, hal_color(140, 140, 150));
    }

    hal_setTextDatum(HAL_DATUM_ML);
    if (on) {
        hal_setTextColor(COL_WHITE, btnBg);
        drawUiString(uiText(TXT_WIFI_ON), dotX + 10, dotY - 6, 1);
        char info[64];
        snprintf(info, sizeof(info), "%s  pw:%s", hal_wifiGetSSID(), hal_wifiGetPass());
        hal_setTextColor(hal_color(200, 200, 120), btnBg);
        drawUiString(info, dotX + 10, dotY + 7, 1);
    } else {
        hal_setTextColor(hal_color(160, 160, 170), btnBg);
        drawUiString(uiText(TXT_WIFI_OFF), dotX + 10, dotY, 1);
    }

    for (int slot = 0; slot < 4; slot++) {
        int slotX = CFG_BUZZER_ROW_X + slot * (CFG_BUZZER_SLOT_W + CFG_BUZZER_ROW_GAP);
        if (slot == 0) {
            drawConfigToggleButton(slotX, CFG_BUZZER_BTN_Y,
                                   CFG_BUZZER_BTN_W, CFG_BUZZER_BTN_H,
                                   buzzerEnabled, uiText(TXT_BUZZER),
                                   buzzerEnabled ? uiText(TXT_ON) : uiText(TXT_OFF));
        } else if (slot == 1) {
            drawConfigToggleButton(slotX, CFG_BUZZER_BTN_Y,
                                   CFG_BUZZER_SLOT_W, CFG_BUZZER_BTN_H,
                                   currentLanguage == LANG_TR,
                                   uiText(TXT_LANGUAGE),
                                   currentLanguage == LANG_TR ? uiText(TXT_LANG_DETAIL_TR) : uiText(TXT_LANG_DETAIL_EN));
        } else if (slot == 3) {
            drawConfigHomeButton(slotX, CFG_BUZZER_BTN_Y,
                                 CFG_BUZZER_SLOT_W, CFG_BUZZER_BTN_H);
        } else {
            drawConfigReservedButton(slotX, CFG_BUZZER_BTN_Y,
                                     CFG_BUZZER_SLOT_W, CFG_BUZZER_BTN_H);
        }
    }
}

// --- Draw a single grid cell for select-mix mode (original 3x3 layout with reduced height) ---
static void drawSelectCell(int row, int col, HalColor color) {
    int num = row * 3 + col + 1;

    int x1 = col * BOX_W + GAP;
    int y1 = CONFIG_STATUS_H + row * CFG_BOX_H + GAP;
    int cw = BOX_W - GAP * 2;
    int ch = CFG_BOX_H - GAP * 2;

    hal_fillRect(x1, y1, cw, ch, color);
    hal_fillRect(x1, y1, cw, 3, hal_color(130, 50, 130));
    hal_fillRect(x1, y1 + ch - 3, cw, 3, hal_color(50, 0, 50));

    int cx = x1 + cw / 2;
    int cy = y1 + ch / 2;
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, color);
    hal_drawNumber(num, cx, cy, 4);
}

static void getSelectCellRect(int row, int col, int &x, int &y, int &w, int &h) {
    x = col * BOX_W + GAP;
    y = CONFIG_STATUS_H + row * CFG_BOX_H + GAP;
    w = BOX_W - GAP * 2;
    h = CFG_BOX_H - GAP * 2;
}

static bool handleManualMotorHold(int row, int col, int number) {
    if (number < 1 || number > 8) return false;

    int cellX, cellY, cellW, cellH;
    getSelectCellRect(row, col, cellX, cellY, cellW, cellH);

    const unsigned long holdMs = 300;
    unsigned long pressStart = hal_millis();
    bool motorRunning = false;

    drawSelectCell(row, col, hal_color(120, 40, 120));

    while (!hal_shouldQuit()) {
        uint16_t holdX = 0, holdY = 0;
        bool touching = hal_getTouch(holdX, holdY);
        bool stillInside = touching && touchInRect(holdX, holdY, cellX, cellY, cellW, cellH);

        if (!stillInside) break;

        if (!motorRunning && hal_millis() - pressStart >= holdMs) {
            motorRunning = true;
            hal_motorOn(number);
            logf("Manual motor %d ON", number);
            drawSelectCell(row, col, hal_color(150, 55, 150));
        }

        hal_delay(20);
    }

    if (motorRunning) {
        hal_motorOff(number);
        logf("Manual motor %d OFF", number);
        uint16_t releaseX = 0, releaseY = 0;
        if (hal_getTouch(releaseX, releaseY)) {
            hal_waitForRelease();
        }
    }

    drawSelectCell(row, col, COL_PURPLE);
    return motorRunning;
}

static void drawSelectMix() {
    hal_fillScreen(hal_color(15, 15, 20));
    drawConfigStatusBar();

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            drawSelectCell(row, col, COL_PURPLE);
        }
    }
}

// --- Full grid draw ---
static void drawGrid(bool useGridColors, bool selectMode) {
    hal_fillScreen(hal_color(15, 15, 20));
    if (selectMode) {
        drawConfigStatusBar();
        return;
    }
    drawStatusBar();
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            HalColor color = useGridColors ? gridColors[row][col] : COL_DEFAULT;
            drawGridCell(row, col, color);
        }
    }
}

static void drawMainMenu()   { drawGrid(true,  false); }

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
                const char *saveLabel = uiText(TXT_SAVE);
                drawUiString(saveLabel, cx, cy, buttonFontForLabel(saveLabel, 4));
            } else {
                hal_fillRect(x1, y1, BOX_W, BOX_H, COL_TEAL);
                hal_drawRect(x1, y1, BOX_W, BOX_H, COL_WHITE);
                hal_setTextDatum(HAL_DATUM_MC);
                hal_setTextColor(COL_WHITE, COL_TEAL);
                char label[8];
                snprintf(label, sizeof(label), "S%d", num);
                hal_drawString(label, cx, cy - 18, 2);
                hal_drawString("-", cx - 32, cy + 10, 2);
                hal_drawNumber(syrupData[currentEditMix - 1][num - 1],
                               cx, cy + 10, 4);
                hal_drawString("+", cx + 32, cy + 10, 2);
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
    hal_drawString(label, cx, cy - 18, 2);
    hal_drawString("-", cx - 32, cy + 10, 2);
    hal_drawNumber(syrupData[currentEditMix - 1][num - 1],
                   cx, cy + 10, 4);
    if (num != 9) {
        hal_drawString("+", cx + 32, cy + 10, 2);
    }
}

static void drawCalibrationScreen() {
    hal_fillScreen(COL_DARK_BLUE);

    hal_setTextDatum(HAL_DATUM_TC);
    hal_setTextColor(COL_YELLOW, COL_DARK_BLUE);
    char title[24];
    snprintf(title, sizeof(title), "MOTOR %d", calibrationMotorId);
    drawUiString(title, 160, 4, 2);

    const int tabW = 140, tabH = 30;
    HalColor colDur = (calibrationPage == CALIB_PAGE_DURATION) ? COL_OK_BTN : COL_GRAY;
    HalColor colCln = (calibrationPage == CALIB_PAGE_CLEANING) ? COL_OK_BTN : COL_GRAY;
    HalColor colDurBorder = (calibrationPage == CALIB_PAGE_DURATION) ? COL_YELLOW : COL_GRAY;
    HalColor colClnBorder = (calibrationPage == CALIB_PAGE_CLEANING) ? COL_YELLOW : COL_GRAY;
    hal_fillRect(10, 30, tabW, tabH, colDur);
    hal_drawRect(10, 30, tabW, tabH, colDurBorder);
    hal_fillRect(170, 30, tabW, tabH, colCln);
    hal_drawRect(170, 30, tabW, tabH, colClnBorder);
    
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, colDur);
    drawUiString("DURATION", 10 + tabW/2, 30 + tabH/2, 1);
    hal_setTextColor(COL_WHITE, colCln);
    drawUiString("CLEANING", 170 + tabW/2, 30 + tabH/2, 1);

    hal_setTextDatum(HAL_DATUM_TC);
    if (calibrationPage == CALIB_PAGE_DURATION) {
        hal_setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
        drawUiString("0 = CODE DEFAULT", 160, 68, 1);

        char valueBuf[24];
        snprintf(valueBuf, sizeof(valueBuf), "%+.1f s", calibrationSecondsPerMotor[calibrationMotorId - 1]);
        hal_setTextColor(COL_WHITE, COL_DARK_BLUE);
        drawUiString(valueBuf, 160, 88, 1);
    } else {
        hal_setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
        char usedBuf[32];
        snprintf(usedBuf, sizeof(usedBuf), "USED: %.1f L", cleaningUsedMl[calibrationMotorId - 1] / 1000.0f);
        drawUiString(usedBuf, 160, 68, 1);

        char valueBuf[24];
        snprintf(valueBuf, sizeof(valueBuf), "LIMIT: %.1f L", cleaningThresholdLiters[calibrationMotorId - 1]);
        hal_setTextColor(COL_WHITE, COL_DARK_BLUE);
        drawUiString(valueBuf, 160, 88, 1);
    }

    const int btnW = 96;
    const int btnH = 54;
    const int btnY = 120;
    const int leftX = 40;
    const int rightX = 184;
    hal_fillRect(leftX, btnY, btnW, btnH, COL_DARK_RED);
    hal_drawRect(leftX, btnY, btnW, btnH, COL_WHITE);
    hal_fillRect(rightX, btnY, btnW, btnH, COL_DARK_GREEN);
    hal_drawRect(rightX, btnY, btnW, btnH, COL_WHITE);
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, COL_DARK_RED);
    drawUiString("-", leftX + btnW / 2, btnY + btnH / 2, 4);
    hal_setTextColor(COL_WHITE, COL_DARK_GREEN);
    drawUiString("+", rightX + btnW / 2, btnY + btnH / 2, 4);

    const int footerY = 190;
    hal_fillRect(20, footerY, 120, 36, COL_GRAY);
    hal_drawRect(20, footerY, 120, 36, COL_WHITE);
    hal_setTextColor(COL_WHITE, COL_GRAY);
    drawUiString("DEFAULT", 80, footerY + 18, 2);

    hal_fillRect(180, footerY, 120, 36, COL_OK_BTN);
    hal_drawRect(180, footerY, 120, 36, COL_WHITE);
    hal_setTextColor(COL_WHITE, COL_OK_BTN);
    drawUiString("SAVE", 240, footerY + 18, 2);
}

// --- View mix contents (with BACK / POUR buttons) ---
static void drawViewMix(int mixId, const char *activeButton) {
    hal_fillScreen(COL_DARK_TEAL);

    // Title
    hal_setTextDatum(HAL_DATUM_TC);
    hal_setTextColor(COL_YELLOW, COL_DARK_TEAL);
    char title[48];
    const char *nameLine = nullptr;
    if (mixId >= 1 && mixId <= 9) {
        const char *line1 = mixNames[mixId - 1][0];
        const char *line2 = mixNames[mixId - 1][1];
        if (line1[0] != '\0') {
            nameLine = line1;
        } else if (line2[0] != '\0') {
            nameLine = line2;
        }
    }

    if (nameLine && nameLine[0] != '\0') {
        snprintf(title, sizeof(title), "%s", nameLine);
    } else {
        uiTextFormat(title, sizeof(title), TXT_VIEW_TITLE_FMT, mixId);
    }
    drawUiString(title, 160, 10, 2);

    int totalVolume = 0;
    float totalEstTime = 0.0f;
    int activeMotors[8];
    int activeCount = 0;
    for (int i = 0; i < 8; i++) {
        int amt = syrupData[mixId - 1][i];
        if (amt > 0) {
            totalVolume += amt;
            totalEstTime += amt * effectiveSecondsPerUnit(i + 1);
        }
        activeMotors[activeCount++] = i + 1;
    }

    if (activeCount > 0) {
        char summary[40];
        uiTextFormat(summary, sizeof(summary), TXT_TOTAL_EST_FMT, totalVolume, totalEstTime);
        hal_setTextDatum(HAL_DATUM_TC);
        hal_setTextColor(hal_color(180, 240, 255), COL_DARK_TEAL);
        drawUiString(summary, 160, 42, 2);
    }

    const int listY = 74;
    const int lineH = 22;
    const int colW = 140;
    for (int idx = 0; idx < activeCount; idx++) {
        int motor = activeMotors[idx];
        int amt = syrupData[mixId - 1][motor - 1];
        char line[32];
        uiTextFormat(line, sizeof(line), TXT_MOTOR_EST_FMT, motor, amt);

        int col = idx % 2;
        int row = idx / 2;
        int x = 14 + col * colW;
        int y = listY + row * lineH;

        hal_setTextDatum(HAL_DATUM_ML);
        hal_setTextColor(COL_WHITE, COL_DARK_TEAL);
        drawUiString(line, x, y, 2);
    }

    // BACK button
    HalColor backCol = (activeButton && strcmp(activeButton, "BACK") == 0)
                       ? COL_LIGHT_RED : COL_DARK_RED;
    hal_fillRect(10, 190, 140, 38, backCol);
    hal_drawRect(10, 190, 140, 38, COL_WHITE);
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, backCol);
    drawUiString(uiText(TXT_BACK), 80, 208, 4);

    // POUR button
    HalColor pourCol = (activeButton && strcmp(activeButton, "POUR") == 0)
                       ? COL_LIGHT_GREEN : COL_DARK_GREEN;
    hal_fillRect(170, 190, 140, 38, pourCol);
    hal_drawRect(170, 190, 140, 38, COL_WHITE);
    hal_setTextColor(COL_WHITE, pourCol);
    drawUiString(uiText(TXT_POUR), 240, 209, 4);
}

// --- Pouring progress screen: static layout (call once) ---
static void drawPouringLayout(int mixId, int totalMotors, bool paused) {
    hal_fillScreen(COL_DARK_BLUE);

    // Title
    hal_setTextDatum(HAL_DATUM_TC);
    char titleBuf[40];
    const char *nameLine = nullptr;
    if (mixId >= 1 && mixId <= 9) {
        const char *line1 = mixNames[mixId - 1][0];
        const char *line2 = mixNames[mixId - 1][1];
        if (line1[0] != '\0') {
            nameLine = line1;
        } else if (line2[0] != '\0') {
            nameLine = line2;
        }
    }

    if (nameLine && nameLine[0] != '\0') {
        snprintf(titleBuf, sizeof(titleBuf), "%s", nameLine);
    } else {
        if (paused) {
            uiTextFormat(titleBuf, sizeof(titleBuf), TXT_PAUSED_MIX_FMT, mixId);
        } else {
            uiTextFormat(titleBuf, sizeof(titleBuf), TXT_POURING_MIX_FMT, mixId);
        }
    }

    if (paused) {
        hal_setTextColor(COL_PAUSE_TITLE, COL_DARK_BLUE);
    } else {
        hal_setTextColor(COL_TITLE_YELLOW, COL_DARK_BLUE);
    }
    drawUiString(titleBuf, 160, 8, 2);

    // Overall progress bar track (larger, high-visibility)
    const int barX = 16, barY = 48, barW = 288, barH = 34;
    hal_fillRect(barX, barY, barW, barH, hal_color(40, 40, 50));

    // Critical overall metrics
    hal_drawHLine(16, 98, 288, COL_GRAY);
    hal_drawHLine(16, 150, 288, COL_GRAY);

    // STOP and PAUSE/RESUME buttons
    const int btnH = 36;
    const int btnY = 198;
    hal_fillRect(20, btnY, 138, btnH, COL_STOP);
    hal_drawRect(20, btnY, 138, btnH, hal_color(255, 100, 100));
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, COL_STOP);
    drawUiString(uiText(TXT_STOP), 89, btnY + btnH / 2, 2);

    // PAUSE / RESUME button
    if (paused) {
        hal_fillRect(166, btnY, 138, btnH, COL_RESUME);
        hal_drawRect(166, btnY, 138, btnH, hal_color(100, 220, 100));
        hal_setTextColor(COL_WHITE, COL_RESUME);
        drawUiString(uiText(TXT_RESUME), 235, btnY + btnH / 2, 2);
    } else {
        hal_fillRect(166, btnY, 138, btnH, COL_PAUSE);
        hal_drawRect(166, btnY, 138, btnH, hal_color(255, 200, 80));
        hal_setTextColor(COL_WHITE, COL_PAUSE);
        drawUiString(uiText(TXT_PAUSE), 235, btnY + btnH / 2, 2);
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
    hal_setTextDatum(HAL_DATUM_TL);
    hal_setTextColor(COL_WHITE, COL_DARK_BLUE);
    char pctStr[24];
    uiTextFormat(pctStr, sizeof(pctStr), TXT_OVERALL_FMT, pct);
    drawUiString(pctStr, 16, 24, 2);

    char volStr[24];
    snprintf(volStr, sizeof(volStr), "%.1f/%.0f ml", overallDone * 10.0f, overallTotal * 10.0f);
    hal_setTextDatum(HAL_DATUM_TR);
    hal_setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
    drawUiString(volStr, 304, 24, 2);

    // Overall progress bar fill (flat rectangle, no rounded corners)
    const int barX = 16, barY = 48, barW = 288, barH = 34;
    int fillW = (int)roundf(barW * overallFrac);
    // Draw background as flat rectangle
    hal_fillRect(barX, barY, barW, barH, hal_color(40, 40, 50));
    // Draw fill as flat rectangle
    if (fillW > 0) {
        int minFill = (overallFrac > 0 && fillW < 2) ? 2 : fillW;
        hal_fillRect(barX, barY, minFill, barH, COL_CYAN_BAR);
    }

    // Time / active summary
    float remaining = (overallDone > 0.01f)
        ? elapsedSec * ((overallTotal - overallDone) / overallDone)
        : 0;
    if (remaining < 0) remaining = 0;
    hal_fillRect(0, 84, 320, 16, COL_DARK_BLUE);
    char timeStr[48];
    uiTextFormat(timeStr, sizeof(timeStr), TXT_TIME_REMAINING_FMT, elapsedSec, remaining);
    hal_setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
    drawUiString(timeStr, 160, 94, 1);

    // Active motors label
    hal_fillRect(0, 108, 320, 18, COL_DARK_BLUE);
    char activeStr[32];
    uiTextFormat(activeStr, sizeof(activeStr), TXT_ACTIVE_MOTORS_FMT, mCount, totalMotors);
    drawUiString(activeStr, 160, 116, 1);

    // Active motor info list: text-only, two columns
    const int leftColX = 16;
    const int rightColX = 176;
    int rowCount = (MAX_CONCURRENT + 1) / 2;
    for (int row = 0; row < rowCount; row++) {
        int yBase = 134 + row * 18;
        hal_fillRect(0, yBase - 6, 320, 14, COL_DARK_BLUE);

        for (int col = 0; col < 2; col++) {
            int i = row * 2 + col;
            if (i >= mCount) continue;

            int xBase = col == 0 ? leftColX : rightColX;
            char label[28];
            snprintf(label, sizeof(label), "M%d  %d%%", mDisp[i].id, mDisp[i].fracPercent);
            hal_setTextDatum(HAL_DATUM_ML);
            hal_setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
            drawUiString(label, xBase, yBase, 1);
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
        drawUiString(uiText(TXT_POUR_COMPLETE), 160, 22, 4);
    } else {
        hal_setTextColor(hal_color(220, 60, 60), COL_DARK_BLUE);
        drawUiString(uiText(TXT_POUR_STOPPED), 160, 22, 4);
    }

    hal_drawHLine(40, 55, 240, COL_GRAY);

    hal_setTextDatum(HAL_DATUM_MC);
    char buf[48];
    hal_setTextColor(COL_WHITE, COL_DARK_BLUE);

    const char *nameLine = nullptr;
    if (mixId >= 1 && mixId <= 9) {
        const char *line1 = mixNames[mixId - 1][0];
        const char *line2 = mixNames[mixId - 1][1];
        if (line1[0] != '\0') {
            nameLine = line1;
        } else if (line2[0] != '\0') {
            nameLine = line2;
        }
    }

    if (nameLine && nameLine[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", nameLine);
    } else {
        uiTextFormat(buf, sizeof(buf), TXT_SUMMARY_MIX_FMT, mixId);
    }
    drawUiString(buf, 160, 80, 2);

    hal_setTextColor(COL_LIGHT_GRAY, COL_DARK_BLUE);
    uiTextFormat(buf, sizeof(buf), TXT_SUMMARY_TIME_FMT, elapsed);
    drawUiString(buf, 160, 110, 2);
    uiTextFormat(buf, sizeof(buf), TXT_SUMMARY_MOTORS_FMT, motorsUsed);
    drawUiString(buf, 160, 140, 2);
    uiTextFormat(buf, sizeof(buf), TXT_SUMMARY_VOLUME_FMT, totalUnits * 10);
    drawUiString(buf, 160, 170, 2);

    // OK button
    const int okBtnW = 140;
    const int okBtnX = (SCREEN_W - okBtnW) / 2;
    hal_fillRect(okBtnX, 195, okBtnW, 38, COL_OK_BTN);
    hal_drawRect(okBtnX, 195, okBtnW, 38, hal_color(100, 180, 255));
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, COL_OK_BTN);
    const char *okLabel = uiText(TXT_OK);
    drawUiString(okLabel, 160, 214, buttonFontForLabel(okLabel, 4));
}

// ============================================
// POUR EXECUTION
// ============================================

struct RunningMotor {
    int   id;
    int   amount;
    float startTime;   // "active seconds" when motor started
};

static float getActivePourTime(unsigned long startMs,
                               unsigned long totalPauseMs,
                               bool paused,
                               unsigned long pauseStartMs) {
    unsigned long nowMs = paused ? pauseStartMs : hal_millis();
    return (nowMs - startMs - totalPauseMs) / 1000.0f;
}

static void refreshPouringScreen(int mixId,
                                 RunningMotor *running,
                                 int runningCount,
                                 int totalMotors,
                                 float finishedUnits,
                                 int totalUnits,
                                 float activeTime,
                                 bool paused) {
    MotorDisplay md[MAX_CONCURRENT];
    float runFrac = 0;
    for (int i = 0; i < runningCount; i++) {
        float dur = running[i].amount * effectiveSecondsPerUnit(running[i].id);
        float frac = dur > 0 ? (activeTime - running[i].startTime) / dur : 1.0f;
        if (frac < 0) frac = 0;
        if (frac > 1.0f) frac = 1.0f;
        md[i] = {running[i].id, running[i].amount, (int)(frac * 100)};
        runFrac += running[i].amount * frac;
    }

    updatePouringScreen(mixId, md, runningCount, totalMotors,
                        finishedUnits + runFrac, (float)totalUnits,
                        activeTime, paused);
}

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
    webPourActiveFlag = true;
    webPourCurrentMix = mixId;
    webPourElapsedSec = 0.0f;
    webPourTotalUnits = (float)totalUnits;
    webPourStatus = 1;
    webPourIsPaused = false;
    webStopPourPending = false;

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
    refreshPouringScreen(mixId, running, runningCount, totalMotors,
                         finishedUnits, totalUnits,
                         getActivePourTime(t0, totalPauseMs, paused, pauseStart),
                         false);
    lastDisplayUpdate = hal_millis();

    // --- Main pour loop ---
    while ((nextPending < pendingCount || runningCount > 0) && !hal_shouldQuit()) {
        hal_wifiProcess();
        if (webStopPourPending) {
            cancelled = true;
            break;
        }
        webPourElapsedSec = getActivePourTime(t0, totalPauseMs, paused, pauseStart);
        webPourIsPaused = paused;

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
                    refreshPouringScreen(mixId, running, runningCount, totalMotors,
                                         finishedUnits, totalUnits,
                                         getActivePourTime(t0, totalPauseMs, paused, pauseStart),
                                         paused);
                    lastDisplayUpdate = hal_millis();
                    hal_delay(300);   // debounce
                }
            }
        }

        float activeTime = getActivePourTime(t0, totalPauseMs, paused, pauseStart);

        // ---- Paused — only update display periodically ----
        if (paused) {
            if (hal_millis() - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
                lastDisplayUpdate = hal_millis();
                refreshPouringScreen(mixId, running, runningCount, totalMotors,
                                     finishedUnits, totalUnits, activeTime, true);
            }
            hal_delay(50);
            continue;
        }

        // ---- Start new motors up to MAX_CONCURRENT ----
        while (nextPending < pendingCount && runningCount < MAX_CONCURRENT) {
            activeTime = getActivePourTime(t0, totalPauseMs, paused, pauseStart);
            int mId  = pending[nextPending].id;
            int mAmt = pending[nextPending].amount;
            running[runningCount++] = {mId, mAmt, activeTime};
            hal_motorOn(mId);
            logf("Motor %d: START — %d ml (%.1fs)",
                 mId, mAmt, mAmt * effectiveSecondsPerUnit(mId));
            refreshPouringScreen(mixId, running, runningCount, totalMotors,
                                 finishedUnits, totalUnits, activeTime, false);
            lastDisplayUpdate = hal_millis();
            nextPending++;

            // Inrush-current stagger (with stop-button polling)
            if (nextPending < pendingCount && runningCount < MAX_CONCURRENT) {
                logf("Inrush stagger %dms", STAGGER_DELAY_MS);
                unsigned long staggerEnd = hal_millis() + STAGGER_DELAY_MS;
                while (hal_millis() < staggerEnd && !hal_shouldQuit()) {
                    hal_wifiProcess();
                    if (webStopPourPending) {
                        cancelled = true;
                        break;
                    }
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

        activeTime = getActivePourTime(t0, totalPauseMs, paused, pauseStart);

        // ---- Check for completed motors ----
        for (int i = runningCount - 1; i >= 0; i--) {
            float dur = running[i].amount * effectiveSecondsPerUnit(running[i].id);
            if (activeTime - running[i].startTime >= dur) {
                hal_motorOff(running[i].id);
                finishedUnits += running[i].amount;
                int motorIdx = running[i].id - 1;
                cleaningUsedMl[motorIdx] += running[i].amount * 10;
                logf("Motor %d: DONE — %d ml", running[i].id, running[i].amount * 10);
                running[i] = running[--runningCount];   // swap-remove
            }
        }

        // ---- Update display periodically ----
        if (hal_millis() - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
            lastDisplayUpdate = hal_millis();
            refreshPouringScreen(mixId, running, runningCount, totalMotors,
                                 finishedUnits, totalUnits, activeTime, false);
        }

        hal_delay(50);
    }

    // Safety: ensure all motors off
    hal_allMotorsOff();

    float elapsed = getActivePourTime(t0, totalPauseMs, paused, pauseStart);
    PourStatus st = cancelled ? POUR_STOPPED : POUR_COMPLETE;
    if (cancelled)
        logf("Pour STOPPED after %.1fs", elapsed);
    else
        logf("Pour COMPLETE in %.1fs", elapsed);

    webPourActiveFlag = false;
    webPourCurrentMix = 0;
    webPourElapsedSec = elapsed;
    webPourTotalUnits = (float)totalUnits;
    webPourStatus = cancelled ? 2 : 3;
    webPourIsPaused = false;
    
    // Save calibration config after pour to persist cleaningUsedMl
    saveCalibrationConfig();
    
    return {st, elapsed, totalMotors, totalUnits};
}

// ============================================
// APP SETUP (called by HAL after hardware init)
// ============================================
void app_setup() {
    hal_log("=== Syrup Touch Mixer ===");

    // Colours
    initColors();

    // Persistent config
    loadUiTranslations();
    loadLanguageConfig();
    loadBuzzerConfig();
    loadCalibrationConfig();

    // Mix data
    loadMixes();

    // Initial screen
    resetGridColors();
    drawMainMenu();

    hal_log("System Ready. Waiting for input...");
}

static void drawCleaningWarningScreen(int motorId) {
    hal_fillScreen(COL_DARK_RED);

    hal_setTextDatum(HAL_DATUM_TC);
    hal_setTextColor(COL_YELLOW, COL_DARK_RED);
    char title[32];
    snprintf(title, sizeof(title), "CLEAN MOTOR %d", motorId);
    drawUiString(title, 160, 20, 2);

    hal_setTextColor(COL_WHITE, COL_DARK_RED);
    drawUiString("Maintenance Required!", 160, 60, 1);
    
    char usedBuf[32];
    snprintf(usedBuf, sizeof(usedBuf), "Used: %.1f L", cleaningUsedMl[motorId - 1] / 1000.0f);
    drawUiString(usedBuf, 160, 100, 2);

    const int btnW = 120;
    const int btnH = 46;
    const int btnY = 160;
    const int leftX = 20;
    const int rightX = 180;
    
    hal_fillRect(leftX, btnY, btnW, btnH, COL_DARK_GREEN);
    hal_drawRect(leftX, btnY, btnW, btnH, COL_WHITE);
    hal_setTextDatum(HAL_DATUM_MC);
    hal_setTextColor(COL_WHITE, COL_DARK_GREEN);
    drawUiString("RESET", leftX + btnW / 2, btnY + btnH / 2, 2);
    
    hal_fillRect(rightX, btnY, btnW, btnH, COL_GRAY);
    hal_drawRect(rightX, btnY, btnW, btnH, COL_WHITE);
    hal_setTextColor(COL_WHITE, COL_GRAY);
    drawUiString("LATER(5M)", rightX + btnW / 2, btnY + btnH / 2, 2);
}

// ============================================
// APP LOOP — state machine (called repeatedly by HAL)
// ============================================
void app_loop() {
    if (hal_shouldQuit()) return;

    // --- WiFi web server processing ---
    hal_wifiProcess();

    // --- Check if mixes were updated via web interface ---
    if (hal_wifiMixesUpdated()) {
        loadMixes();
        if (appState == MAIN_MENU) {
            resetGridColors();
            drawMainMenu();
        }
        hal_log("Mixes reloaded from web update");
    }

    processPendingWebAction();

    if (appState == MAIN_MENU) {
        for (int i = 0; i < 8; i++) {
            if (cleaningThresholdLiters[i] > 0.0f) {
                if (cleaningUsedMl[i] >= cleaningThresholdLiters[i] * 1000.0f) {
                    if (cleaningSnoozeUntilMs[i] == 0 || hal_millis() >= cleaningSnoozeUntilMs[i]) {
                        appState = CLEANING_WARNING;
                        warningMotorId = i + 1;
                        drawCleaningWarningScreen(warningMotorId);
                        return; // return so it doesn't process main menu touch in this frame
                    }
                }
            }
        }
    }

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

    // --- Config screen (SELECT_MIX): original top row + dedicated buzzer row ---
    if (appState == SELECT_MIX && ty < CONFIG_STATUS_H) {
        if (ty < CONFIG_TOP_ROW_H) {
            if (tx >= CFG_WIFI_BTN_X) {
                beepTouch();
                hal_waitForRelease();
                if (hal_wifiIsActive()) hal_wifiStop(); else hal_wifiStart();
                drawConfigStatusBar();
                hal_delay(300); // debounce — prevent ghost re-toggle
            }
        } else if (touchInRect(tx, ty, CFG_BUZZER_BTN_X, CFG_BUZZER_BTN_Y, CFG_BUZZER_BTN_W, CFG_BUZZER_BTN_H)) {
            hal_waitForRelease();
            bool wasEnabled = buzzerEnabled;
            if (wasEnabled) beepTouch();
            buzzerEnabled = !buzzerEnabled;
            saveBuzzerConfig();
            if (!wasEnabled && buzzerEnabled) beepTouch();
            drawSelectMix();
            hal_delay(300);
        } else if (touchInRect(tx, ty,
                               CFG_BUZZER_ROW_X + (CFG_BUZZER_SLOT_W + CFG_BUZZER_ROW_GAP),
                               CFG_BUZZER_BTN_Y,
                               CFG_BUZZER_SLOT_W,
                               CFG_BUZZER_BTN_H)) {
            hal_waitForRelease();
            currentLanguage = (currentLanguage == LANG_EN) ? LANG_TR : LANG_EN;
            saveLanguageConfig();
            drawSelectMix();
            hal_delay(300);
        } else if (touchInRect(tx, ty,
                               CFG_BUZZER_ROW_X + 3 * (CFG_BUZZER_SLOT_W + CFG_BUZZER_ROW_GAP),
                               CFG_BUZZER_BTN_Y,
                               CFG_BUZZER_SLOT_W,
                               CFG_BUZZER_BTN_H)) {
            hal_waitForRelease();
            beepTouch();
            appState = MAIN_MENU;
            resetGridColors();
            drawMainMenu();
            hal_delay(300);
        }
        hal_delay(20);
        return;
    }

    // --- Normal status bar: WiFi icon tap ---
    if (ty < GRID_Y) {
        if (tx >= WIFI_ICON_X - 10) {
            beepTouch();
            hal_waitForRelease();
            if (hal_wifiIsActive()) {
                hal_wifiStop();
            } else {
                hal_wifiStart();
            }
            drawWifiIcon();
            hal_delay(300); // debounce — prevent ghost re-toggle
        }
        hal_delay(20);
        return;
    }

    // Map touch to 3×3 grid (config screen uses thicker status bar)
    int gridY = (appState == SELECT_MIX) ? CONFIG_STATUS_H : GRID_Y;
    int boxH  = (appState == SELECT_MIX) ? CFG_BOX_H : BOX_H;
    int col    = tx / BOX_W;  if (col > 2) col = 2;
    int row    = (ty - gridY) / boxH;  if (row < 0) row = 0; if (row > 2) row = 2;
    int number = row * 3 + col + 1;
    int localX = tx - col * BOX_W;

    switch (appState) {

    // --------------------------------------------------
    case MAIN_MENU: {
        beepTouch();
        // Visual feedback — 3D pressed (sunken) effect
        drawGridCell(row, col, MIX_COLORS[number - 1], true);

        // Measure press duration
        unsigned long pressStart = hal_millis();
        hal_waitForRelease();
        unsigned long duration = hal_millis() - pressStart;

        // Restore the touched cell to its normal raised state
        drawGridCell(row, col, MIX_COLORS[number - 1], false);

        if (duration >= 300) {
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
            beepTouch();
            drawViewMix(currentViewMix, "BACK");
            hal_waitForRelease();
            appState = MAIN_MENU;
            resetGridColors();
            drawMainMenu();

        } else if (touchInRect(tx, ty, 170, 180, 140, 45)) {
            // POUR
            beepTouch();
            drawViewMix(currentViewMix, "POUR");
            hal_waitForRelease();

            PourResult result = executePour(currentViewMix);

            if (result.status != POUR_EMPTY) {
                // Play signal based on pour outcome
                if (result.status == POUR_COMPLETE) {
                    beepPourDone();
                } else {
                    beepPourStopped();
                }

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
        beepTouch();
        if (handleManualMotorHold(row, col, number)) {
            hal_delay(120);
            break;
        }
        hal_waitForRelease();
        currentEditMix = number;
        logf("Editing Mix %d", number);
        appState = EDIT_DASHBOARD;
        drawEditDashboard();
        break;
    }

    // --------------------------------------------------
    case EDIT_DASHBOARD: {
        if (number == 9) {
            beepTouch();
            hal_waitForRelease();
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
            break;
        }

        unsigned long pressStart = hal_millis();
        bool longPressed = false;
        int cellX = col * BOX_W;
        int cellY = row * BOX_H;
        while (!hal_shouldQuit()) {
            uint16_t touchX = 0, touchY = 0;
            bool touching = hal_getTouch(touchX, touchY);
            if (!touching) break;
            if (touchInRect(touchX, touchY, cellX, cellY, BOX_W, BOX_H)) {
                if (hal_millis() - pressStart >= 500) {
                    longPressed = true;
                    break;
                }
            } else {
                break;
            }
            hal_delay(20);
        }

        if (longPressed) {
            calibrationMotorId = number;
            appState = CALIBRATE_DURATION;
            drawCalibrationScreen();
            break;
        }

        beepTouch();
        hal_waitForRelease();
        int &val = syrupData[currentEditMix - 1][number - 1];
        if (localX < BOX_W / 2) {
            if (val > 0) val -= 1;
        } else {
            if (val < MAX_SYRUP_UNITS) val += 1;
        }
        logf("Motor %d set to %d (Mix %d)", number, val, currentEditMix);

        // Flash feedback then redraw just this cell
        hal_fillRect(cellX, cellY, BOX_W, BOX_H, COL_CYAN);
        hal_delay(50);
        drawEditCell(row, col);
        break;
    }

    case CALIBRATE_DURATION: {
        if (touchInRect(tx, ty, 10, 30, 140, 30)) {
            beepTouch();
            calibrationPage = CALIB_PAGE_DURATION;
            drawCalibrationScreen();
        } else if (touchInRect(tx, ty, 170, 30, 140, 30)) {
            beepTouch();
            calibrationPage = CALIB_PAGE_CLEANING;
            drawCalibrationScreen();
        } else if (touchInRect(tx, ty, 40, 120, 96, 54)) {
            beepTouch();
            if (calibrationPage == CALIB_PAGE_DURATION) {
                calibrationSecondsPerMotor[calibrationMotorId - 1] -= 1.0f;
            } else {
                cleaningThresholdLiters[calibrationMotorId - 1] -= 1.0f;
                if (cleaningThresholdLiters[calibrationMotorId - 1] < 0.0f) 
                    cleaningThresholdLiters[calibrationMotorId - 1] = 0.0f;
            }
            drawCalibrationScreen();
        } else if (touchInRect(tx, ty, 184, 120, 96, 54)) {
            beepTouch();
            if (calibrationPage == CALIB_PAGE_DURATION) {
                calibrationSecondsPerMotor[calibrationMotorId - 1] += 1.0f;
            } else {
                cleaningThresholdLiters[calibrationMotorId - 1] += 1.0f;
            }
            drawCalibrationScreen();
        } else if (touchInRect(tx, ty, 20, 190, 120, 36)) {
            beepTouch();
            if (calibrationPage == CALIB_PAGE_DURATION) {
                calibrationSecondsPerMotor[calibrationMotorId - 1] = 0.0f;
            } else {
                cleaningThresholdLiters[calibrationMotorId - 1] = 10.0f;
            }
            drawCalibrationScreen();
        } else if (touchInRect(tx, ty, 180, 190, 120, 36)) {
            beepTouch();
            saveCalibrationConfig();
            appState = EDIT_DASHBOARD;
            drawEditDashboard();
        }
        break;
    }
    case CLEANING_WARNING: {
        if (touchInRect(tx, ty, 20, 160, 120, 46)) { // RESET
            beepTouch();
            cleaningUsedMl[warningMotorId - 1] = 0;
            cleaningSnoozeUntilMs[warningMotorId - 1] = 0;
            saveCalibrationConfig();
            appState = MAIN_MENU;
            resetGridColors();
            drawMainMenu();
        } else if (touchInRect(tx, ty, 180, 160, 120, 46)) { // LATER(5M)
            beepTouch();
            cleaningSnoozeUntilMs[warningMotorId - 1] = hal_millis() + 5 * 60 * 1000;
            appState = MAIN_MENU;
            resetGridColors();
            drawMainMenu();
        }
        break;
    }
    }  // switch

    hal_delay(20);
}
