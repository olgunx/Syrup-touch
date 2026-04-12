# Syrup Touch Mixer

ESP32-S2 firmware for a touchscreen syrup dispensing mixer with 8 pump outputs, editable mix recipes, WiFi-based configuration, OTA update support, and a native simulator target for UI testing.

The current firmware also supports a bilingual on-device UI (English and Turkish), translation overrides from LittleFS, persisted language and buzzer settings, mix import/export from the web UI, and OTA updates for both firmware and the filesystem image.

## Hardware

- Board: WEMOS LOLIN S2 Mini (ESP32-S2)
- Display: ILI9341 240x320 TFT
- Touch: XPT2046
- Motors: 8 pump channels through L9110S drivers
- Buzzer: passive buzzer on GPIO 40
- Filesystem: LittleFS

Motor pin mapping:

- Motor 1: GPIO 1, GPIO 2
- Motor 2: GPIO 3, GPIO 4
- Motor 3: GPIO 39, GPIO 6
- Motor 4: GPIO 7, GPIO 8
- Motor 5: GPIO 11, GPIO 12
- Motor 6: GPIO 9, GPIO 10
- Motor 7: GPIO 13, GPIO 14
- Motor 8: GPIO 17, GPIO 21

Pin mappings and timing constants are defined in [include/config.h](/home/oun/CODE/Syrup-touch/esp32s2/include/config.h).

## Build Targets

The project provides two PlatformIO environments:

- `lolin_s2_mini`: real ESP32-S2 firmware
- `simulator`: native SDL2 simulator for UI and interaction testing

PlatformIO configuration is in [platformio.ini](/home/oun/CODE/Syrup-touch/esp32s2/platformio.ini).

## Core Features

- 3x3 touch main menu for drink selection
- Passcode-protected configuration menu
- Mix viewer with pour and back actions
- Editable syrup assignments for 9 mixes and 8 motors
- Sequenced pour execution with progress tracking
- Bilingual on-device UI with English and Turkish labels
- UI translation strings loaded from `data/ui_translations.csv`
- Persistent language selection stored in LittleFS
- WiFi AP and web UI for mix import/export and WiFi settings
- OTA firmware and filesystem update page
- Native simulator build for desktop testing

## Config Menu UI

The config menu keeps the 3x3 mix grid and adds a dedicated header area above it:

- Top row shows build information on the left and the WiFi control on the right
- Build information now displays a `BUILD:` label above the build date/time
- Second row contains a quarter-width buzzer button and three reserved slots for future controls
- The config grid is slightly shorter to make room for these controls without removing any mix slots

Relevant UI logic lives in [src/main.cpp](/home/oun/CODE/Syrup-touch/esp32s2/src/main.cpp).

## Recent Functional Additions

### Buzzer Control

- Buzzer can be enabled or disabled directly from the config menu
- Buzzer state is persisted across reboots
- Touch and pour completion tones respect the saved buzzer setting

### Language and Translation Support

- The config menu includes a language toggle for English and Turkish
- The active language is persisted across reboots in `language_config.json`
- Built-in fallback strings are compiled into the firmware
- Runtime UI text can be overridden from `data/ui_translations.csv` without changing code
- Turkish-capable font headers are included so the device UI can render localized labels correctly

### WiFi State Persistence

- WiFi SSID and password are stored in LittleFS
- WiFi enabled/disabled state is also persisted
- If WiFi was on before reboot, the AP is restored automatically at next boot

WiFi persistence is handled in [src/hal_esp32.cpp](/home/oun/CODE/Syrup-touch/esp32s2/src/hal_esp32.cpp).

### Manual Motor Test From Config Menu

- In the config menu, pressing and holding buttons `1` through `8` manually runs the matching motor
- The motor stays active only while the touch remains held on the same button
- Releasing the touch, or sliding off the button, stops the motor immediately
- Short tap behavior is preserved, so a normal tap still opens mix editing

This is intended as a quick manual pump test without leaving the config screen.

### Hardware Wiring Correction

- Motor 5 and motor 6 mappings were swapped in firmware to match the installed hardware wiring
- All shared motor operations now use the corrected mapping, including pours and manual hold-to-run testing

## Data Files in LittleFS

These files are served from, or loaded from, the LittleFS image under `data/`:

- `index.html`: captive portal and mix/WiFi configuration UI
- `syrup_mixes.json`: persisted mix definitions and names
- `ui_translations.csv`: runtime translation table for the touchscreen UI
- `language_config.json`: last selected UI language

If you change files under `data/`, rebuild and upload the filesystem image so the device picks up the new content.

## Running

Build firmware:

```bash
platformio run
```

Build LittleFS image:

```bash
platformio run -t buildfs
```

Upload firmware to the ESP32-S2:

```bash
platformio run -t upload
```

Upload LittleFS data files:

```bash
platformio run -t uploadfs
```

Build simulator:

```bash
platformio run --environment simulator
```

Run simulator:

```bash
platformio run --environment simulator -t exec
```

Simulator prerequisite on Debian/Ubuntu:

```bash
sudo apt install libsdl2-dev libsdl2-ttf-dev
```

Upload simulator task output is available through the existing VS Code tasks configuration in the workspace.

## Web and OTA Endpoints

When WiFi AP mode is enabled, the ESP32-S2 hosts:

- `/`: web configuration UI
- `/api/mixes`: GET/POST mix definitions
- `/api/wifi`: GET/POST WiFi credentials
- `/api/export`: export mixes as JSON
- `/api/import`: import mixes as JSON
- `/update`: OTA page for firmware and LittleFS uploads

## Project Structure

- [src/main.cpp](/home/oun/CODE/Syrup-touch/esp32s2/src/main.cpp): shared application state machine and UI
- [src/hal_esp32.cpp](/home/oun/CODE/Syrup-touch/esp32s2/src/hal_esp32.cpp): ESP32 hardware implementation
- [src/hal_sim.cpp](/home/oun/CODE/Syrup-touch/esp32s2/src/hal_sim.cpp): simulator implementation
- [include/hal.h](/home/oun/CODE/Syrup-touch/esp32s2/include/hal.h): hardware abstraction interface
- [include/config.h](/home/oun/CODE/Syrup-touch/esp32s2/include/config.h): pins, timing, and defaults
- [data/index.html](/home/oun/CODE/Syrup-touch/esp32s2/data/index.html): web configuration UI
- [data/ui_translations.csv](/home/oun/CODE/Syrup-touch/esp32s2/data/ui_translations.csv): English/Turkish UI text overrides
- [data/language_config.json](/home/oun/CODE/Syrup-touch/esp32s2/data/language_config.json): persisted UI language selection
- [data/syrup_mixes.json](/home/oun/CODE/Syrup-touch/esp32s2/data/syrup_mixes.json): persisted mix definitions