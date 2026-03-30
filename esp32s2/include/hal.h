#ifndef HAL_H
#define HAL_H

// ============================================
// Hardware Abstraction Layer — Syrup Touch Mixer
// ============================================
// Two implementations:
//   hal_esp32.cpp  — real ILI9341 + XPT2046 + GPIO motors + LittleFS
//   hal_sim.cpp    — SDL2 window + mouse + console motors + filesystem
//
// Application code in main.cpp calls only these functions.

#include <cstdint>
#include <cstddef>

// ============================================
// COLOUR TYPE
// ============================================
// On ESP32 this maps to RGB565; on SDL2 it encodes 8-bit RGB.
// All code uses hal_color() to create values.
typedef uint32_t HalColor;

// ============================================
// TEXT DATUM (anchor point for drawString / drawNumber)
// ============================================
enum HalDatum {
    HAL_DATUM_TL = 0,   // top-left
    HAL_DATUM_TC = 1,   // top-center
    HAL_DATUM_TR = 2,   // top-right
    HAL_DATUM_ML = 3,   // middle-left
    HAL_DATUM_MC = 4,   // middle-center
    HAL_DATUM_MR = 5,   // middle-right
    HAL_DATUM_BL = 6,   // bottom-left
    HAL_DATUM_BC = 7,   // bottom-center
    HAL_DATUM_BR = 8,   // bottom-right
};

// ============================================
// DISPLAY
// ============================================
HalColor hal_color(uint8_t r, uint8_t g, uint8_t b);

void hal_fillScreen(HalColor c);
void hal_fillRect(int x, int y, int w, int h, HalColor c);
void hal_drawRect(int x, int y, int w, int h, HalColor c);
void hal_fillCircle(int cx, int cy, int r, HalColor c);
void hal_drawCircle(int cx, int cy, int r, HalColor c);
void hal_drawHLine(int x, int y, int w, HalColor c);

void hal_setTextDatum(HalDatum d);
void hal_setTextColor(HalColor fg, HalColor bg = 0);
void hal_drawString(const char *str, int x, int y, int font);
void hal_drawNumber(int num, int x, int y, int font);

// ============================================
// TOUCH
// ============================================
// Returns true if screen is currently being touched.
// Writes touch coordinates (0-319, 0-239) into x, y.
bool hal_getTouch(uint16_t &x, uint16_t &y);

// Blocks until the current touch is released (+ short debounce).
void hal_waitForRelease();

// ============================================
// MOTORS (1-indexed: 1–8)
// ============================================
void hal_initMotors();
void hal_motorOn(int id);
void hal_motorOff(int id);
void hal_allMotorsOff();

// ============================================
// PERSISTENT STORAGE
// ============================================
// Read entire file contents into buf (up to bufSize-1, null-terminated).
// Returns bytes read, or -1 on error / not found.
int  hal_readFile(const char *path, char *buf, size_t bufSize);

// Write buf (len bytes) to file. Returns true on success.
bool hal_writeFile(const char *path, const char *buf, size_t len);

// ============================================
// SYSTEM
// ============================================
unsigned long hal_millis();
void          hal_delay(unsigned long ms);
void          hal_log(const char *msg);

// LED heartbeat
void hal_ledOn();
void hal_ledOff();

// ============================================
// LIFECYCLE
// ============================================
// Called by each HAL implementation's main()/setup() to hand off
// control to the shared application code.
void app_setup();
void app_loop();

// Simulator-only: pump events. On ESP32 this is a no-op.
// Must be called frequently (inside delay / waitForRelease) to keep
// the window responsive.
void hal_pumpEvents();

// Returns true when the application should exit (window closed, etc.).
// On ESP32 this always returns false.
bool hal_shouldQuit();

#endif // HAL_H
