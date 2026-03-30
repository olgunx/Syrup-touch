# Syrup-touch

A touchscreen-controlled syrup dispensing machine with 8 independent pump/motor channels and customizable mix recipes. Runs on an ESP32-S2 (LOLIN S2 Mini) with ILI9341 LCD + XPT2046 touch, or on a Linux PC via a native SDL2 simulator for development.

## Hardware

### Components

| Component | Model | Interface |
|-----------|-------|-----------|
| Microcontroller | LOLIN S2 Mini (ESP32-S2, 4MB Flash, 2MB PSRAM) | USB-C |
| LCD Display | ILI9341 (240×320, 2.8") | SPI |
| Touch Controller | XPT2046 (resistive) | SPI (shared bus) |
| Motor/Pump Drivers | 4× L9110S dual H-bridge module (8 channels) | GPIO |

### SPI Pin Mapping

| Function | GPIO | Description |
|----------|------|-------------|
| SPI CLK  | 36   | FSPI clock (shared display + touch) |
| SPI MOSI | 35   | FSPI data out (shared) |
| SPI MISO | 37   | FSPI data in (shared) |
| LCD CS   | 34   | Display chip select |
| LCD DC   | 33   | Display data/command |
| LCD RST  | 18   | Display reset |
| Touch CS | 16   | XPT2046 chip select |
| Touch IRQ| 38   | XPT2046 interrupt (active LOW) |

### Motor Control

Each L9110S module drives 2 motors. 4 modules provide 8 independent pump channels.
Each motor uses a GPIO pair (IN1 = forward, IN2 = reverse). For pumps, only IN1 is driven HIGH to pour.

| Motor | Syrup | IN1 (GPIO) | IN2 (GPIO) |
|-------|-------|------------|------------|
| Motor 1 | syrup_1 | 1  | 2  |
| Motor 2 | syrup_2 | 3  | 4  |
| Motor 3 | syrup_3 | 5  | 6  |
| Motor 4 | syrup_4 | 7  | 8  |
| Motor 5 | syrup_5 | 9  | 10 |
| Motor 6 | syrup_6 | 11 | 12 |
| Motor 7 | syrup_7 | 13 | 14 |
| Motor 8 | syrup_8 | 17 | 21 |

- Max **2 motors** run concurrently to limit inrush current
- **500 ms stagger delay** between starting motors in a pair
- Pour rate: **10 s per unit** (configurable in `config.h`)

## Architecture — HAL Abstraction

The firmware uses a **Hardware Abstraction Layer (HAL)** so that the exact same application logic runs on both the real ESP32 device and a native PC simulator:

```
esp32s2/
├── include/
│   ├── hal.h              # Portable HAL interface (display, touch, motors, storage)
│   └── config.h           # Shared constants (pins, timing, secret code)
├── src/
│   ├── main.cpp           # All app logic — uses only hal_* calls
│   ├── hal_esp32.cpp      # ESP32 HAL: TFT_eSPI, GPIO, LittleFS
│   └── hal_sim.cpp        # Simulator HAL: SDL2 + SDL_ttf, mouse, filesystem
├── data/
│   └── syrup_mixes.json   # Mix recipe data (shared by both targets)
└── platformio.ini         # Build config for both environments
```

- **`main.cpp`** — all UI, state machine, and pour logic using portable `hal_*` calls
- **`hal_esp32.cpp`** — wraps TFT_eSPI, GPIO, and LittleFS (compiled only for ESP32)
- **`hal_sim.cpp`** — wraps SDL2 + SDL_ttf for rendering, mouse for touch, filesystem for storage (compiled only for simulator)
- **`config.h`** — uses `<cstdint>` (no Arduino dependency) for cross-platform portability

## Development Setup (Linux)

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- SDL2 + SDL2_ttf (for simulator only)
- System fonts (DejaVu Sans Bold — typically pre-installed)

```bash
# Debian/Ubuntu — simulator dependencies
sudo apt install libsdl2-dev libsdl2-ttf-dev
```

### Clone & Build

```bash
git clone https://github.com/olgunx/Syrup-touch.git
cd Syrup-touch/esp32s2

# Build ESP32 firmware
pio run -e lolin_s2_mini

# Build native simulator
pio run -e simulator
```

### Flash to Device

```bash
pio run -e lolin_s2_mini --target upload

# Monitor serial output
pio device monitor
```

### Run Simulator

```bash
# Build & run
pio run -e simulator && .pio/build/simulator/program
```

The simulator opens a **960×720 window** (3× scaled from 320×240 LCD). Click with the mouse to simulate touch input. Motor events are logged to the console with timestamps.

#### VS Code Shortcuts (with PlatformIO extension)

- Switch environment in the **status bar** (bottom) between `lolin_s2_mini` and `simulator`
- **Ctrl+Alt+B** — Build
- **Ctrl+Alt+U** — Upload / Run

## Usage

### Main Menu

A 3×3 grid numbered 1–9. Each button represents a saved mix recipe.

| Action | Gesture |
|--------|---------|
| View a mix recipe | **Long press** (≥0.5 s) on a button |
| Enter edit mode | **Short tap** the secret passcode sequence: **4 → 7 → 2 → 5** |

### Viewing a Mix

Shows all 8 syrup amounts for the selected mix. Two buttons at the bottom:

- **BACK** — return to main menu
- **POUR** — start dispensing the recipe

### Editing Recipes

After entering the passcode, select a mix (1–9) to edit. Tap a syrup channel to increment its value (0–10, wraps around). Tap **SAVE** (button 9) to persist changes to `syrup_mixes.json`.

### Dispensing

When POUR is pressed, the machine dispenses all configured syrups:

- Motors start in pairs (max 2 concurrent) with a 500 ms inrush stagger
- A progress screen shows overall completion %, elapsed/remaining time, and per-motor status
- **PAUSE/RESUME** and **STOP** buttons available during pour
- All events are logged with timestamps

## Configuration

Key parameters in `include/config.h`:

```cpp
constexpr float SECONDS_PER_UNIT = 10.0f;  // pour time per unit (seconds)
constexpr int   STAGGER_DELAY_MS = 500;    // stagger between motor starts (ms)
constexpr int   MAX_CONCURRENT   = 2;      // max simultaneous motors
constexpr int   SECRET_CODE[4]   = {4, 7, 2, 5};  // passcode to enter edit mode
```

Recipe data is stored in `data/syrup_mixes.json` (9 mixes × 8 channels, auto-created on first run).
