# Syrup Touch Mixer

ESP32-S2 firmware for a touchscreen syrup dispensing mixer with 8 pump outputs, editable mix recipes, WiFi-based configuration, OTA update support, and a native simulator target for UI testing.

## Hardware

- Board: WEMOS LOLIN S2 Mini (ESP32-S2)
- Display: ILI9341 240x320 TFT
- Touch: XPT2046
- Motors: 8 pump channels through L9110S drivers
- Buzzer: passive buzzer on GPIO 40
- Filesystem: LittleFS

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

## Running

Build firmware:

```bash
platformio run
```

Build simulator:

```bash
platformio run --environment simulator
```

Upload simulator task output is available through the existing VS Code tasks configuration in the workspace.

## Project Structure

- [src/main.cpp](/home/oun/CODE/Syrup-touch/esp32s2/src/main.cpp): shared application state machine and UI
- [src/hal_esp32.cpp](/home/oun/CODE/Syrup-touch/esp32s2/src/hal_esp32.cpp): ESP32 hardware implementation
- [src/hal_sim.cpp](/home/oun/CODE/Syrup-touch/esp32s2/src/hal_sim.cpp): simulator implementation
- [include/hal.h](/home/oun/CODE/Syrup-touch/esp32s2/include/hal.h): hardware abstraction interface
- [include/config.h](/home/oun/CODE/Syrup-touch/esp32s2/include/config.h): pins, timing, and defaults
- [data/index.html](/home/oun/CODE/Syrup-touch/esp32s2/data/index.html): web configuration UI
- [data/syrup_mixes.json](/home/oun/CODE/Syrup-touch/esp32s2/data/syrup_mixes.json): persisted mix definitions