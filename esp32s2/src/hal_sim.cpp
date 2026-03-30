// HAL implementation for PC Simulator — SDL2
// Renders to an SDL2 window, mouse = touch, motors = console, storage = filesystem

#ifndef ARDUINO   // only compiled for the native (simulator) target

#include "hal.h"
#include "config.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <chrono>
#include <string>

// ============================================
// CONSTANTS
// ============================================
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
// Window size: 1.5× gives ~2.8" on a 14" laptop
static const int WIN_W    = 480;
static const int WIN_H    = 360;

// ============================================
// SDL GLOBALS
// ============================================
static SDL_Window   *gWindow       = nullptr;
static SDL_Renderer *gRenderer     = nullptr;
static SDL_Texture  *gRenderTarget = nullptr;
static bool          gQuit         = false;

// Start time for millis()
static auto gStartTime = std::chrono::steady_clock::now();

// Motor state (for console display)
static bool gMotorStates[8] = {};

// Current text datum & color
static HalDatum gDatum   = HAL_DATUM_TL;
static HalColor gTextFg  = 0xFFFFFF;
static HalColor gTextBg  = 0x000000;

// Mouse state for touch simulation
static bool     gMouseDown = false;
static uint16_t gMouseX    = 0;
static uint16_t gMouseY    = 0;

// Data directory (relative to executable, points to ../data/)
static std::string gDataDir;

// ============================================
// HELPERS
// ============================================
static void colorToRGB(HalColor c, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = (c >> 16) & 0xFF;
    g = (c >> 8)  & 0xFF;
    b =  c        & 0xFF;
}

// Copy offscreen texture to screen
static void presentFrame() {
    SDL_SetRenderTarget(gRenderer, nullptr);
    SDL_RenderCopy(gRenderer, gRenderTarget, nullptr, nullptr);
    SDL_RenderPresent(gRenderer);
    SDL_SetRenderTarget(gRenderer, gRenderTarget);
}

static void setColor(HalColor c) {
    uint8_t r, g, b;
    colorToRGB(c, r, g, b);
    SDL_SetRenderDrawColor(gRenderer, r, g, b, 255);
}

// ============================================
// TTF FONT RENDERING
// ============================================
// Font sizes matching TFT_eSPI font numbers (in LCD pixels):
//   font 1 → 8px   (small)
//   font 2 → 16px  (medium)
//   font 4 → 26px  (large)
static const int NUM_FONT_SLOTS = 3;
static TTF_Font *gFonts[NUM_FONT_SLOTS] = {};  // indices: 0=font1, 1=font2, 2=font4

static int fontSlot(int font) {
    if (font <= 1) return 0;
    if (font == 2) return 1;
    return 2;  // font 4
}

static int fontPtSize(int font) {
    // Render at native LCD-pixel sizes (no SCALE) to match TFT_eSPI bitmap fonts
    switch (font) {
    case 1:  return 8;
    case 2:  return 16;
    default: return 26; // font 4
    }
}

static bool initFonts() {
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return false;
    }
    // Try common font paths
    const char *fontPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        nullptr
    };
    const char *chosen = nullptr;
    for (int i = 0; fontPaths[i]; i++) {
        FILE *f = fopen(fontPaths[i], "r");
        if (f) { fclose(f); chosen = fontPaths[i]; break; }
    }
    if (!chosen) {
        fprintf(stderr, "No TTF font found!\n");
        return false;
    }
    for (int slot = 0; slot < NUM_FONT_SLOTS; slot++) {
        int fonts[] = {1, 2, 4};
        gFonts[slot] = TTF_OpenFont(chosen, fontPtSize(fonts[slot]));
        if (!gFonts[slot]) {
            fprintf(stderr, "TTF_OpenFont(%s, slot %d) failed: %s\n",
                    chosen, slot, TTF_GetError());
            return false;
        }
    }
    return true;
}

static void closeFonts() {
    for (int i = 0; i < NUM_FONT_SLOTS; i++) {
        if (gFonts[i]) { TTF_CloseFont(gFonts[i]); gFonts[i] = nullptr; }
    }
    TTF_Quit();
}

// Draw a string at LCD-pixel (x, y) with given font size and datum
static void drawText(const char *str, int x, int y, int font, HalDatum datum,
                     HalColor fg, HalColor bg) {
    if (!str || !str[0]) return;
    TTF_Font *f = gFonts[fontSlot(font)];
    if (!f) return;

    uint8_t fr, fgr, fb;
    colorToRGB(fg, fr, fgr, fb);
    SDL_Color sdlFg = {fr, fgr, fb, 255};

    // Render with light AA at native LCD resolution — readable but still pixelated when scaled
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, str, sdlFg);
    if (!surf) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(gRenderer, surf);
    // Text size in LCD pixels (native resolution)
    int lcdW = surf->w;
    int lcdH = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) return;

    // Apply datum alignment (using native LCD dimensions)
    int px = x;
    int py = y;
    switch (datum) {
    case HAL_DATUM_TL: break;
    case HAL_DATUM_TC: px -= lcdW / 2; break;
    case HAL_DATUM_TR: px -= lcdW; break;
    case HAL_DATUM_ML: py -= lcdH / 2; break;
    case HAL_DATUM_MC: px -= lcdW / 2; py -= lcdH / 2; break;
    case HAL_DATUM_MR: px -= lcdW; py -= lcdH / 2; break;
    case HAL_DATUM_BL: py -= lcdH; break;
    case HAL_DATUM_BC: px -= lcdW / 2; py -= lcdH; break;
    case HAL_DATUM_BR: px -= lcdW; py -= lcdH; break;
    }

    // Draw background rect to clear area behind text
    uint8_t br, bg2, bb;
    colorToRGB(bg, br, bg2, bb);
    SDL_SetRenderDrawColor(gRenderer, br, bg2, bb, 255);
    SDL_Rect bgRect = {px, py, lcdW, lcdH};
    SDL_RenderFillRect(gRenderer, &bgRect);

    // Render text at native LCD resolution
    SDL_Rect dstRect = {px, py, lcdW, lcdH};
    SDL_RenderCopy(gRenderer, tex, nullptr, &dstRect);
    SDL_DestroyTexture(tex);
}

// ============================================
// HAL DISPLAY IMPLEMENTATION
// ============================================
// Simulate ILI9341 RGB565 (65K color) quantization:
// Truncate R to 5 bits, G to 6 bits, B to 5 bits, then expand back to 8-bit.
// This ensures the simulator shows the same color banding as the real display.
HalColor hal_color(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t r5 = (r >> 3) << 3;  // keep top 5 bits
    uint8_t g6 = (g >> 2) << 2;  // keep top 6 bits
    uint8_t b5 = (b >> 3) << 3;  // keep top 5 bits
    return ((uint32_t)r5 << 16) | ((uint32_t)g6 << 8) | b5;
}

void hal_fillScreen(HalColor c) {
    setColor(c);
    SDL_Rect rc = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(gRenderer, &rc);
}

void hal_fillRect(int x, int y, int w, int h, HalColor c) {
    setColor(c);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(gRenderer, &rc);
}

void hal_drawRect(int x, int y, int w, int h, HalColor c) {
    setColor(c);
    SDL_Rect top    = {x, y, w, 1};
    SDL_Rect bottom = {x, y + h - 1, w, 1};
    SDL_Rect left   = {x, y, 1, h};
    SDL_Rect right  = {x + w - 1, y, 1, h};
    SDL_RenderFillRect(gRenderer, &top);
    SDL_RenderFillRect(gRenderer, &bottom);
    SDL_RenderFillRect(gRenderer, &left);
    SDL_RenderFillRect(gRenderer, &right);
}

void hal_fillCircle(int cx, int cy, int r, HalColor c) {
    setColor(c);
    // Midpoint circle fill
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt((double)(r * r - dy * dy));
        SDL_Rect rc = {cx - dx, cy + dy, 2 * dx + 1, 1};
        SDL_RenderFillRect(gRenderer, &rc);
    }
}

void hal_drawCircle(int cx, int cy, int r, HalColor c) {
    setColor(c);
    // Bresenham circle outline
    int x0 = r, y0 = 0, err = 0;
    while (x0 >= y0) {
        auto plot = [&](int px, int py) {
            SDL_Rect rc = {px, py, 1, 1};
            SDL_RenderFillRect(gRenderer, &rc);
        };
        plot(cx + x0, cy + y0); plot(cx + y0, cy + x0);
        plot(cx - y0, cy + x0); plot(cx - x0, cy + y0);
        plot(cx - x0, cy - y0); plot(cx - y0, cy - x0);
        plot(cx + y0, cy - x0); plot(cx + x0, cy - y0);
        if (err <= 0) { y0++; err += 2 * y0 + 1; }
        if (err > 0)  { x0--; err -= 2 * x0 + 1; }
    }
}

void hal_drawHLine(int x, int y, int w, HalColor c) {
    setColor(c);
    SDL_Rect rc = {x, y, w, 1};
    SDL_RenderFillRect(gRenderer, &rc);
}

void hal_setTextDatum(HalDatum d) { gDatum = d; }

void hal_setTextColor(HalColor fg, HalColor bg) {
    gTextFg = fg;
    gTextBg = bg;
}

void hal_drawString(const char *str, int x, int y, int font) {
    drawText(str, x, y, font, gDatum, gTextFg, gTextBg);
}

void hal_drawNumber(int num, int x, int y, int font) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", num);
    drawText(buf, x, y, font, gDatum, gTextFg, gTextBg);
}

// ============================================
// TOUCH (mouse simulation)
// ============================================
bool hal_getTouch(uint16_t &x, uint16_t &y) {
    hal_pumpEvents();
    if (gMouseDown) {
        x = gMouseX;
        y = gMouseY;
        return true;
    }
    return false;
}

void hal_waitForRelease() {
    presentFrame();
    while (gMouseDown && !gQuit) {
        hal_pumpEvents();
        SDL_Delay(20);
    }
    SDL_Delay(50);
}

// ============================================
// MOTORS (console output)
// ============================================
void hal_initMotors() {
    memset(gMotorStates, 0, sizeof(gMotorStates));
}

void hal_motorOn(int id) {
    gMotorStates[id - 1] = true;
    char buf[64];
    snprintf(buf, sizeof(buf), "  >>> MOTOR %d: ON", id);
    hal_log(buf);
}

void hal_motorOff(int id) {
    if (gMotorStates[id - 1]) {
        gMotorStates[id - 1] = false;
        char buf[64];
        snprintf(buf, sizeof(buf), "  >>> MOTOR %d: OFF", id);
        hal_log(buf);
    }
}

void hal_allMotorsOff() {
    for (int i = 1; i <= 8; i++) hal_motorOff(i);
}

// ============================================
// STORAGE (filesystem — reads/writes ../data/ relative to executable)
// ============================================
int hal_readFile(const char *path, char *buf, size_t bufSize) {
    // path comes as "/syrup_mixes.json" — map to data dir
    std::string fullPath = gDataDir + path;
    FILE *f = fopen(fullPath.c_str(), "r");
    if (!f) return -1;
    int n = (int)fread(buf, 1, bufSize - 1, f);
    buf[n] = '\0';
    fclose(f);
    return n;
}

bool hal_writeFile(const char *path, const char *buf, size_t len) {
    std::string fullPath = gDataDir + path;
    FILE *f = fopen(fullPath.c_str(), "w");
    if (!f) return false;
    fwrite(buf, 1, len, f);
    fclose(f);
    return true;
}

// ============================================
// SYSTEM
// ============================================
unsigned long hal_millis() {
    auto now = std::chrono::steady_clock::now();
    return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(
        now - gStartTime).count();
}

void hal_delay(unsigned long ms) {
    presentFrame();
    unsigned long end = hal_millis() + ms;
    while (hal_millis() < end && !gQuit) {
        hal_pumpEvents();
        SDL_Delay(1);
    }
}

void hal_log(const char *msg) {
    printf("[%8lu] %s\n", hal_millis(), msg);
}

void hal_ledOn()  { /* no-op in simulator */ }
void hal_ledOff() { /* no-op in simulator */ }

void hal_buzzerInit() { /* no-op in simulator */ }
void hal_buzzerTone(int freqHz, int durationMs) { (void)freqHz; (void)durationMs; }
void hal_buzzerOff() { /* no-op in simulator */ }

// ============================================
// EVENT PUMP
// ============================================
void hal_pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            gQuit = true;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                gMouseDown = true;
                gMouseX = (uint16_t)(e.button.x * SCREEN_W / WIN_W);
                gMouseY = (uint16_t)(e.button.y * SCREEN_H / WIN_H);
                if (gMouseX >= SCREEN_W) gMouseX = SCREEN_W - 1;
                if (gMouseY >= SCREEN_H) gMouseY = SCREEN_H - 1;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                gMouseDown = false;
            }
            break;
        case SDL_MOUSEMOTION:
            if (gMouseDown) {
                gMouseX = (uint16_t)(e.motion.x * SCREEN_W / WIN_W);
                gMouseY = (uint16_t)(e.motion.y * SCREEN_H / WIN_H);
                if (gMouseX >= SCREEN_W) gMouseX = SCREEN_W - 1;
                if (gMouseY >= SCREEN_H) gMouseY = SCREEN_H - 1;
            }
            break;
        }
    }
}

bool hal_shouldQuit() { return gQuit; }

// ============================================
// MAIN — simulator entry point
// ============================================
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    // Resolve data directory: <executable_dir>/../../../data
    // Binary lives at .pio/build/simulator/program, project data at data/
    {
        std::string exePath(argv[0]);
        size_t sep = exePath.find_last_of("/\\");
        std::string exeDir = (sep != std::string::npos) ? exePath.substr(0, sep) : ".";
        gDataDir = exeDir + "/../../../data";
    }

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    gWindow = SDL_CreateWindow(
        "Syrup Mixer — ILI9341 320x240 RGB565 (1.5x)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!gWindow) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    gRenderer = SDL_CreateRenderer(gWindow, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!gRenderer) {
        // Fallback to software renderer
        gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!gRenderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(gWindow);
        SDL_Quit();
        return 1;
    }

    // Create offscreen render target to avoid double-buffer flickering
    gRenderTarget = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
    SDL_SetTextureScaleMode(gRenderTarget, SDL_ScaleModeNearest);
    SDL_SetRenderTarget(gRenderer, gRenderTarget);

    // Init TTF fonts
    if (!initFonts()) {
        SDL_DestroyTexture(gRenderTarget);
        SDL_DestroyRenderer(gRenderer);
        SDL_DestroyWindow(gWindow);
        SDL_Quit();
        return 1;
    }

    gStartTime = std::chrono::steady_clock::now();

    printf("\n");
    hal_log("=== Syrup Touch Mixer (SIMULATOR — SDL2) ===");
    hal_log("Click to interact. Close window to quit.");
    printf("\n");

    hal_initMotors();
    app_setup();

    while (!gQuit) {
        app_loop();
        presentFrame();
        hal_pumpEvents();
        SDL_Delay(10);
    }

    hal_allMotorsOff();
    closeFonts();
    SDL_DestroyTexture(gRenderTarget);
    SDL_DestroyRenderer(gRenderer);
    SDL_DestroyWindow(gWindow);
    SDL_Quit();
    return 0;
}

#endif // !ARDUINO
