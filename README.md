# Syrup-touch

A touchscreen-controlled syrup dispensing machine with 8 independent pump/motor channels and customizable mix recipes. Runs on a Raspberry Pi with ILI9341 LCD + XPT2046 touch, or on a Linux PC in simulator mode for development.

## Hardware

### Components

| Component | Model | Interface |
|-----------|-------|-----------|
| LCD Display | ILI9341 (240×320, 2.8") | SPI, 10 MHz |
| Touch Controller | XPT2046 (resistive) | SPI (shared bus) |
| Motor/Pump Drivers | 8 channels (peristaltic pumps) | GPIO |
| Controller | Raspberry Pi (any model with SPI + GPIO) | — |

### GPIO Pin Mapping

| Function | Board Pin | BCM Pin | Description |
|----------|-----------|---------|-------------|
| SPI CLK | `board.SCK` | GPIO 11 | SPI clock (shared) |
| SPI MOSI | `board.MOSI` | GPIO 10 | SPI data out (shared) |
| SPI MISO | `board.MISO` | GPIO 9 | SPI data in (shared) |
| LCD CS | `board.D8` | GPIO 8 (CE0) | LCD chip select |
| LCD DC | `board.D24` | GPIO 24 | LCD data/command |
| LCD RST | `board.D25` | GPIO 25 | LCD reset |
| Touch CS | `board.D7` | GPIO 7 (CE1) | Touch chip select |
| Touch IRQ | `board.D17` | GPIO 17 | Touch interrupt |

### Motor Control

- **8 independent motor/pump channels** (syrup_1 through syrup_8)
- Max **2 motors** run concurrently to limit inrush current
- **500 ms stagger delay** between starting motors in a pair
- Pour rate: configurable (default 0.5 s per unit)

## Development Environment Setup (Linux)

### Prerequisites

- Python 3.7+
- System fonts (DejaVu or FreeSans) for display text rendering

```bash
# Debian/Ubuntu
sudo apt update
sudo apt install python3 python3-venv python3-pip fonts-dejavu-core
```

### Clone & Setup

```bash
git clone https://github.com/<your-username>/Syrup-touch.git
cd Syrup-touch

# Create virtual environment
python3 -m venv venv
source venv/bin/activate

# Install simulator dependencies
pip install Pillow pygame
```

### Run Simulator

```bash
source venv/bin/activate
python mix.py
```

The simulator opens a 640×480 pygame window (2× scaled, rotated 90° CW to landscape). Click with the mouse to simulate touch input.

### Device Mode (Raspberry Pi)

On the Raspberry Pi, install the hardware libraries instead:

```bash
source venv/bin/activate
pip install Pillow
pip install adafruit-circuitpython-rgb-display
pip install xpt2046-circuitpython
```

Then change the mode in `mix.py`:

```python
CONFIG = {
    "MODE": "DEVICE",  # Change from "SIMULATOR" to "DEVICE"
    ...
}
```

Enable SPI on the Pi:

```bash
sudo raspi-config
# Interface Options → SPI → Enable
```

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
- **OK** — start dispensing (pours the recipe)

### Editing Recipes

After entering the passcode, select a mix (1–9) to edit. Tap a syrup channel to increment its value (0–10, wraps around). Tap **SAVE** (button 9) to persist changes.

### Dispensing

When OK is pressed on a mix view, the machine dispenses all configured syrups:

- Motors start in pairs (max 2 concurrent) with a 500 ms inrush stagger
- A progress screen shows overall completion and individual motor status
- All events are logged to the console with timestamps

## Configuration

Key parameters in `mix.py`:

```python
CONFIG = {
    "MODE": "SIMULATOR",       # "SIMULATOR" or "DEVICE"
    "DISPLAY_WIDTH": 240,
    "DISPLAY_HEIGHT": 320,
    "TOUCH_CALIBRATION": {
        "x_min": 10, "x_max": 4000,
        "y_min": 10, "y_max": 4000
    }
}

SECONDS_PER_UNIT = 0.5   # pour time per unit (seconds)
INRUSH_DELAY     = 0.5   # stagger between motor starts (seconds)
MAX_CONCURRENT   = 2     # max simultaneous motors
```

Recipe data is stored in `syrup_mixes.json` (9 mixes × 8 channels, auto-created on first run).

## Project Structure

```
Syrup-touch/
├── mix.py              # Main application (UI, touch, motor control)
├── syrup_mixes.json    # Persistent recipe storage (auto-generated)
├── venv/               # Python virtual environment (not committed)
└── README.md
```
