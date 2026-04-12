#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>

// ============================================
// UI FONT SIZES (0-100 scale)
// ============================================
constexpr int FONT_SIZE_BOXNUMBER = 35; // Font size for numbers in grid cells (0-100)
constexpr int FONT_SIZE_BOXNAME = 9;    // Turkish font pt size for names in grid cells (7,8,9,10,11,12)

// ============================================
// MOTOR GPIO PIN MAPPING
// ============================================
// Each L9110S channel uses two GPIO pins (IN1=forward, IN2=reverse).
// For pumps, only IN1 is driven HIGH to pour; IN2 stays LOW.
//
// ESP32-S2 Mini pinout:
//   Motor 1: GPIO  1, GPIO  2
//   Motor 2: GPIO  3, GPIO  4
//   Motor 3: GPIO 39, GPIO  6
//   Motor 4: GPIO  7, GPIO  8
//   Motor 5: GPIO 11, GPIO 12
//   Motor 6: GPIO  9, GPIO 10
//   Motor 7: GPIO 13, GPIO 14
//   Motor 8: GPIO 17, GPIO 21
//
// SPI (FSPI) for display & touch:
//   SCK=36, MOSI=35, MISO=37, LCD_CS=34, LCD_DC=33, LCD_RST=18, TOUCH_CS=16, TOUCH_IRQ=38
//
// Spare GPIOs: 15 (onboard LED), 5 (strapping pin — avoid for motors)
//
// Buzzer: GPIO 40

struct MotorPins {
    uint8_t in1;
    uint8_t in2;
};

constexpr MotorPins MOTOR_PINS[8] = {
    { 1,  2},  // Motor 1 → syrup_1
    { 3,  4},  // Motor 2 → syrup_2
    {39,  6},  // Motor 3 → syrup_3
    { 7,  8},  // Motor 4 → syrup_4
    {11, 12},  // Motor 5 → syrup_5
    { 9, 10},  // Motor 6 → syrup_6
    {13, 14},  // Motor 7 → syrup_7
    {17, 21},  // Motor 8 → syrup_8
};

// ============================================
// POUR TIMING
// ============================================
constexpr float SECONDS_PER_UNIT = 5.5556f;  // pour time per 10ml unit (measured: 18ml in 10s)
constexpr int   STAGGER_DELAY_MS = 500;    // inrush-current stagger between motor starts
constexpr int   MAX_CONCURRENT   = 2;      // max motors running at once

// ============================================
// TOUCH PIN MAPPING
// ============================================
constexpr uint8_t XPT_CS  = 16;      // SPI chip select for touch controller
constexpr uint8_t XPT_IRQ = 38;      // Touch interrupt pin (active LOW)

// ============================================
// PASSCODE (tap sequence to enter edit mode)
// ============================================
constexpr int SECRET_CODE[4] = {4, 7, 2, 5};

// ============================================
// BUZZER
// ============================================
constexpr uint8_t BUZZER_PIN = 40;

// ============================================
// WIFI ACCESS POINT
// ============================================
constexpr const char *WIFI_AP_SSID = "SyrupTouch";
constexpr const char *WIFI_AP_PASS = "syrup1234";  // min 8 chars; set "" for open

#endif
