import time
import json
import os
from datetime import datetime
from PIL import Image, ImageDraw, ImageFont

def log(msg):
    """Print a message with a timestamp prefix."""
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{ts}] {msg}")

# ============================================
# CONFIGURATION - SELECT MODE HERE
# ============================================
CONFIG = {
    "MODE": "SIMULATOR",  # "SIMULATOR" or "DEVICE"
    "DISPLAY_WIDTH": 240,
    "DISPLAY_HEIGHT": 320,
    "TOUCH_CALIBRATION": {
        "x_min": 10, "x_max": 4000,
        "y_min": 10, "y_max": 4000
    }
}

# Conditional imports based on mode
if CONFIG["MODE"] == "DEVICE":
    import board
    import busio
    import digitalio
    import adafruit_rgb_display.ili9341 as ili9341
    import xpt2046_circuitpython
else:
    try:
        import pygame
    except ImportError:
        print("⚠️  pygame not found. Install with: pip install pygame")
        print("Falling back to keyboard input only.")
        pygame = None

# ============================================
# SIMULATOR CLASSES (for Linux PC development)
# ============================================

class SimulatorDisplay:
    """Mock display for development on Linux PC"""
    def __init__(self, width=240, height=320):
        self.width = width
        self.height = height
        self.current_image = None
        self.pygame = pygame
        self.screen = None
        
        if pygame:
            try:
                pygame.init()
                # Scale up display for better visibility on PC
                self.scale = 2
                # Rotated 90° CW: window is landscape (height x width)
                self.screen = pygame.display.set_mode(
                    (self.height * self.scale, self.width * self.scale)
                )
                pygame.display.set_caption("Syrup Mixer Simulator - Touch to interact")
                self.clock = pygame.time.Clock()
            except Exception as e:
                print(f"⚠️  Pygame initialization failed: {e}")
                print("   Running in headless mode (no visual display)")
                self.pygame = None
        
    def image(self, img):
        """Display an image (same interface as real display)"""
        self.current_image = img
        
        if pygame and self.screen:
            # Rotate 90° CW for landscape simulation
            rotated = img.rotate(-90, expand=True)
            mode = rotated.mode
            size = rotated.size
            data = rotated.tobytes()
            py_image = pygame.image.fromstring(data, size, mode)
            
            # Scale up for visibility
            scaled = pygame.transform.scale(
                py_image, 
                (self.height * self.scale, self.width * self.scale)
            )
            self.screen.blit(scaled, (0, 0))
            pygame.display.flip()

class SimulatorTouch:
    """Mock touch input for development on Linux PC"""
    def __init__(self):
        self.pygame = pygame
        self.last_click = None
        self.display_width = CONFIG["DISPLAY_WIDTH"]
        self.display_height = CONFIG["DISPLAY_HEIGHT"]
        self.scale = 2 if pygame else 1
        
    def get_coordinates(self):
        """
        Returns touch coordinates matching device pixel range.
        Maps mouse clicks on the rotated landscape window back to
        the original portrait coordinate system.
        """
        if pygame:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    exit(0)
                elif event.type == pygame.MOUSEBUTTONDOWN:
                    mouse_x, mouse_y = pygame.mouse.get_pos()
                    
                    # Window is rotated 90° CW, so swap axes:
                    # touch_x (original X) = mouse_y mapped to display width
                    # touch_y (original Y) = mouse_x mapped to display height
                    touch_x = int(mouse_y / self.scale)
                    touch_y = int(mouse_x / self.scale)
                    
                    self.last_click = (touch_x, touch_y)
                    return (touch_x, touch_y)
        
        return None
    
    def wait_for_release_sim(self):
        """Simulate button release for PC testing"""
        if pygame:
            waiting = True
            while waiting:
                for event in pygame.event.get():
                    if event.type == pygame.QUIT:
                        exit(0)
                    elif event.type == pygame.MOUSEBUTTONUP:
                        waiting = False
                time.sleep(0.02)
        else:
            # Fallback for non-pygame mode
            time.sleep(0.3)

# ============================================
# HARDWARE INITIALIZATION
# ============================================

if CONFIG["MODE"] == "DEVICE":
    print("🔌 Device mode - Initializing Raspberry Pi hardware...")
    
    spi = busio.SPI(clock=board.SCK, MOSI=board.MOSI, MISO=board.MISO)
    
    display = ili9341.ILI9341(
        spi,
        cs=digitalio.DigitalInOut(board.D8),
        dc=digitalio.DigitalInOut(board.D24),
        rst=digitalio.DigitalInOut(board.D25),
        width=CONFIG["DISPLAY_WIDTH"],
        height=CONFIG["DISPLAY_HEIGHT"],
        baudrate=10000000
    )
    
    touch_cs = digitalio.DigitalInOut(board.D7)
    touch_irq = digitalio.DigitalInOut(board.D17)
    touch = xpt2046_circuitpython.Touch(spi, cs=touch_cs, interrupt=touch_irq)

    # ==========================================
    # DC MOTORS (L9110S dual H-bridge modules)
    # ==========================================
    MOTOR_PIN_MAP = {
        1: (board.D5, board.D6),    # M1: Pin 29 (GPIO 5),  Pin 31 (GPIO 6)
        2: (board.D19, board.D13),  # M2: Pin 35 (GPIO 19), Pin 33 (GPIO 13)
        3: (board.D12, board.D20),  # M3: Pin 32 (GPIO 12), Pin 38 (GPIO 20)
        4: (board.D16, board.D21),  # M4: Pin 36 (GPIO 16), Pin 40 (GPIO 21)
        5: (board.D0, board.D1),    # M5: Pin 27 (GPIO 0),  Pin 28 (GPIO 1)
        6: (board.D26, board.D23),  # M6: Pin 37 (GPIO 26), Pin 16 (GPIO 23)
        7: (board.D22, board.D27),  # M7: Pin 15 (GPIO 22), Pin 13 (GPIO 27)
        8: (board.D4, board.D3),    # M8: Pin 7  (GPIO 4),  Pin 5  (GPIO 3)
    }

    motors = {}
    for m_id, (pin1, pin2) in MOTOR_PIN_MAP.items():
        in1 = digitalio.DigitalInOut(pin1)
        in1.direction = digitalio.Direction.OUTPUT
        in1.value = False
        in2 = digitalio.DigitalInOut(pin2)
        in2.direction = digitalio.Direction.OUTPUT
        in2.value = False
        motors[m_id] = {'IN1': in1, 'IN2': in2}

else:
    print("💻 Simulator mode - Using mock display and touch input")
    print("   Click in the window to simulate touch input")
    display = SimulatorDisplay(
        width=CONFIG["DISPLAY_WIDTH"],
        height=CONFIG["DISPLAY_HEIGHT"]
    )
    touch = SimulatorTouch()

width, height = display.width, display.height
image = Image.new("RGB", (width, height), (0, 0, 0))
draw = ImageDraw.Draw(image)

# ============================================
# PERMANENT STORAGE SETUP ================
DATA_FILE = "syrup_mixes.json"

def load_mixes():
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, "r") as f:
            return json.load(f)
    else:
        default_data = {str(mix_id): {f"syrup_{s}": 0 for s in range(1, 9)} for mix_id in range(1, 10)}
        with open(DATA_FILE, "w") as f:
            json.dump(default_data, f)
        return default_data

def save_mixes(data):
    with open(DATA_FILE, "w") as f:
        json.dump(data, f)

syrup_data = load_mixes()
current_edit_mix = 1 
current_view_mix = 1 

# ============================================
# FONTS ============================================
try:
    font_large = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 36)
    font_config = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
    font_small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16)
except IOError:
    try:
        font_large = ImageFont.truetype("/usr/share/fonts/truetype/freefont/FreeSansBold.ttf", 36)
        font_config = ImageFont.truetype("/usr/share/fonts/truetype/freefont/FreeSansBold.ttf", 22)
        font_small = ImageFont.truetype("/usr/share/fonts/truetype/freefont/FreeSansBold.ttf", 16)
    except IOError:
        font_large = font_config = font_small = ImageFont.load_default()

# ---------------- APP STATE & UI DATA ----------------
APP_STATE = "MAIN_MENU"
passcode_buffer = []
SECRET_CODE = [4, 7, 2, 5]

box_w = width // 3
box_h = height // 3

DEFAULT_COLOR = (40, 40, 40)
HIGHLIGHT_COLOR = (255, 255, 0)
grid_colors = [[DEFAULT_COLOR for _ in range(3)] for _ in range(3)]

number_map = [
    [3, 6, 9],  
    [2, 5, 8],  
    [1, 4, 7]   
]

# ---------------- DRAWING HELPERS ----------------
def draw_rotated_text(text, center_x, center_y, font, fill_color):
    txt_img = Image.new('RGBA', (200, 80), (0, 0, 0, 0))
    txt_draw = ImageDraw.Draw(txt_img)
    try:
        txt_draw.text((100, 40), text, font=font, fill=fill_color, anchor="mm", align="center")
    except TypeError:
        txt_draw.text((50, 10), text, font=font, fill=fill_color, align="center")
        
    rotated = txt_img.rotate(90, expand=True)
    paste_x = center_x - (rotated.width // 2)
    paste_y = center_y - (rotated.height // 2)
    image.paste(rotated, (paste_x, paste_y), rotated)

def reset_grid_colors():
    for r in range(3):
        for c in range(3):
            grid_colors[r][c] = DEFAULT_COLOR

def wait_for_release():
    """Wait for touch to be released (device or simulator)"""
    if CONFIG["MODE"] == "SIMULATOR":
        touch.wait_for_release_sim()
    else:
        consecutive_nones = 0
        while consecutive_nones < 4:
            try:
                if touch.get_coordinates() is None:
                    consecutive_nones += 1
                else:
                    consecutive_nones = 0
            except Exception:
                consecutive_nones += 1 
            time.sleep(0.02)


# ---------------- UI SCREENS ----------------
def draw_main_menu():
    draw.rectangle((0, 0, width, height), fill=(0, 0, 0))
    for r in range(3):
        for c in range(3):
            x1, y1 = c * box_w, r * box_h
            draw.rectangle((x1, y1, x1 + box_w, y1 + box_h), fill=grid_colors[r][c], outline=(255,255,255))
            cx, cy = x1 + (box_w // 2), y1 + (box_h // 2)
            draw.ellipse((cx - 24, cy - 24, cx + 24, cy + 24), fill=(0, 0, 0), outline=(255, 255, 255))
            draw_rotated_text(str(number_map[r][c]), cx, cy, font_large, (255, 255, 255, 255))
    display.image(image)

def draw_select_mix():
    draw.rectangle((0, 0, width, height), fill=(0, 0, 0))
    for r in range(3):
        for c in range(3):
            x1, y1 = c * box_w, r * box_h
            draw.rectangle((x1, y1, x1 + box_w, y1 + box_h), fill=(80, 0, 80), outline=(255,255,255))
            cx, cy = x1 + (box_w // 2), y1 + (box_h // 2)
            draw.ellipse((cx - 24, cy - 24, cx + 24, cy + 24), fill=(0, 0, 0), outline=(255, 255, 255))
            draw_rotated_text(str(number_map[r][c]), cx, cy, font_large, (255, 255, 255))
    display.image(image)

def draw_edit_dashboard():
    draw.rectangle((0, 0, width, height), fill=(0, 0, 0))
    for r in range(3):
        for c in range(3):
            x1, y1 = c * box_w, r * box_h
            cx, cy = x1 + (box_w // 2), y1 + (box_h // 2)
            box_num = number_map[r][c]

            if box_num == 9:
                draw.rectangle((x1, y1, x1 + box_w, y1 + box_h), fill=(200, 0, 0), outline=(255,255,255))
                draw_rotated_text("SAVE", cx, cy, font_config, (255, 255, 255))
            else:
                amount = syrup_data[str(current_edit_mix)][f"syrup_{box_num}"]
                draw.rectangle((x1, y1, x1 + box_w, y1 + box_h), fill=(0, 100, 150), outline=(255,255,255))
                text = f"S{box_num}\n{amount}"
                draw_rotated_text(text, cx, cy, font_config, (255, 255, 255))
    display.image(image)

def draw_view_mix(mix_id, active_button=None):
    draw.rectangle((0, 0, width, height), fill=(0, 0, 0))
    canvas = Image.new('RGBA', (320, 240), (0, 50, 50, 255)) 
    c_draw = ImageDraw.Draw(canvas)
    
    try:
        # Title
        c_draw.text((160, 20), f"--- MIX {mix_id} CONTENTS ---", font=font_config, fill=(255, 255, 0), anchor="mm")
        
        # Tightened Y-spacing for the syrups so they don't overlap the buttons
        mix = syrup_data[str(mix_id)]
        for i in range(1, 5):
            y_pos = 60 + ((i - 1) * 30) # Spaced at Y=60, 90, 120, 150
            val1 = mix[f"syrup_{i}"]
            val2 = mix[f"syrup_{i+4}"]
            c_draw.text((80, y_pos), f"Syrup {i}:  {val1}", font=font_config, fill=(255, 255, 255), anchor="mm")
            c_draw.text((240, y_pos), f"Syrup {i+4}:  {val2}", font=font_config, fill=(255, 255, 255), anchor="mm")
            
        # Draw explicit BACK and OK buttons pushed further down
        color_back = (255, 50, 50) if active_button == "BACK" else (150, 0, 0)
        color_pour = (50, 255, 50) if active_button == "POUR" else (0, 150, 0)
        
        c_draw.rectangle((10, 180, 150, 225), fill=color_back, outline=(255,255,255))
        c_draw.text((80, 202), "BACK", font=font_config, fill=(255, 255, 255), anchor="mm")
        
        c_draw.rectangle((170, 180, 310, 225), fill=color_pour, outline=(255,255,255))
        c_draw.text((240, 202), "POUR", font=font_config, fill=(255, 255, 255), anchor="mm")

    except TypeError:
        # Fallback for older systems
        c_draw.text((20, 10), f"MIX {mix_id} CONTENTS", font=font_config, fill=(255, 255, 0))
        c_draw.rectangle((10, 180, 150, 225), fill=(150, 0, 0), outline=(255,255,255))
        c_draw.text((50, 195), "BACK", font=font_config, fill=(255, 255, 255))
        c_draw.rectangle((170, 180, 310, 225), fill=(0, 150, 0), outline=(255,255,255))
        c_draw.text((210, 195), "POUR", font=font_config, fill=(255, 255, 255))

    rotated = canvas.rotate(90, expand=True)
    image.paste(rotated, (0, 0), rotated)
    display.image(image)

SECONDS_PER_UNIT = 10.0  # pour time per unit (seconds)
STAGGER_DELAY   = 0.5   # 500ms stagger between motor starts
MAX_CONCURRENT  = 2     # max motors running at once

def draw_pouring_screen(mix_id, running_motors, total_motors, overall_done, overall_total,
                        elapsed_sec, paused=False):
    """Draw pouring progress with overall bar, active motor bars, and STOP/PAUSE buttons."""
    draw.rectangle((0, 0, width, height), fill=(0, 0, 0))
    canvas = Image.new('RGBA', (320, 240), (0, 20, 40, 255))
    c_draw = ImageDraw.Draw(canvas)

    overall_frac = overall_done / overall_total if overall_total > 0 else 0

    try:
        # --- Title ---
        title = f"PAUSED MIX {mix_id}" if paused else f"POURING MIX {mix_id}"
        title_color = (255, 160, 0) if paused else (255, 200, 0)
        c_draw.text((160, 16), title, font=font_config, fill=title_color, anchor="mm")

        # --- Overall progress bar (large, top) ---
        pct = int(overall_frac * 100)
        c_draw.text((160, 42), f"Overall  {pct}%", font=font_config, fill=(255, 255, 255), anchor="mm")
        ob_x, ob_y, ob_w, ob_h = 20, 58, 280, 28
        c_draw.rectangle((ob_x, ob_y, ob_x + ob_w, ob_y + ob_h), outline=(255, 255, 255))
        of_w = int(ob_w * overall_frac)
        c_draw.rectangle((ob_x, ob_y, ob_x + of_w, ob_y + ob_h), fill=(0, 180, 255))
        c_draw.text((160, ob_y + ob_h // 2), f"{overall_done:.1f}/{overall_total} ml",
                    font=font_small, fill=(255, 255, 255), anchor="mm")

        # --- Time info ---
        remaining = max(0, elapsed_sec * ((overall_total - overall_done) / max(overall_done, 1)))
        c_draw.text((160, 100), f"Elapsed: {elapsed_sec:.1f}s    Remaining: ~{remaining:.1f}s",
                    font=font_small, fill=(180, 180, 180), anchor="mm")

        # --- Separator ---
        c_draw.line((20, 118, 300, 118), fill=(80, 80, 80), width=1)

        # --- Active motor bars (smaller, bottom) ---
        c_draw.text((160, 130), f"Active motors ({len(running_motors)}/{total_motors} total)",
                    font=font_small, fill=(200, 200, 200), anchor="mm")

        for i, (motor_id, amount, frac) in enumerate(running_motors):
            y_base = 150 + i * 30
            pct = int(frac * 100)
            label = f"M{motor_id}: {pct}%"
            c_draw.text((30, y_base + 2), label, font=font_small, fill=(200, 200, 200), anchor="lm")
            mb_x, mb_w, mb_h = 100, 200, 8
            c_draw.rectangle((mb_x, y_base - 3, mb_x + mb_w, y_base - 3 + mb_h), outline=(180, 180, 180))
            mf_w = int(mb_w * frac)
            c_draw.rectangle((mb_x, y_base - 3, mb_x + mf_w, y_base - 3 + mb_h), fill=(0, 200, 80))

        # --- STOP and PAUSE/RESUME buttons at bottom ---
        btn_y, btn_h = 200, 36
        gap = 8
        # STOP button (left half) — red
        sb_x, sb_w = 20, 145
        c_draw.rectangle((sb_x, btn_y, sb_x + sb_w, btn_y + btn_h), fill=(200, 30, 30))
        c_draw.rectangle((sb_x, btn_y, sb_x + sb_w, btn_y + btn_h), outline=(255, 100, 100))
        c_draw.text((sb_x + sb_w // 2, btn_y + btn_h // 2), "\u25A0  STOP", font=font_config, fill=(255, 255, 255), anchor="mm")
        # PAUSE/RESUME button (right half) — orange/green
        pb_x, pb_w = sb_x + sb_w + gap, 145
        if paused:
            pb_fill = (30, 160, 60)
            pb_outline = (100, 220, 100)
            pb_label = "\u25B6  RESUME"
        else:
            pb_fill = (200, 140, 0)
            pb_outline = (255, 200, 80)
            pb_label = "\u275A\u275A  PAUSE"
        c_draw.rectangle((pb_x, btn_y, pb_x + pb_w, btn_y + btn_h), fill=pb_fill)
        c_draw.rectangle((pb_x, btn_y, pb_x + pb_w, btn_y + btn_h), outline=pb_outline)
        c_draw.text((pb_x + pb_w // 2, btn_y + btn_h // 2), pb_label, font=font_config, fill=(255, 255, 255), anchor="mm")

    except TypeError:
        c_draw.text((20, 20), f"Pouring Mix {mix_id}  {overall_done}/{overall_total}",
                    font=font_config, fill=(255, 255, 255))

    rotated = canvas.rotate(90, expand=True)
    image.paste(rotated, (0, 0), rotated)
    display.image(image)

def draw_summary_screen(mix_id, status, elapsed, motors_used, total_units):
    """Show a summary after pouring finishes or is stopped."""
    draw.rectangle((0, 0, width, height), fill=(0, 0, 0))
    canvas = Image.new('RGBA', (320, 240), (0, 20, 40, 255))
    c_draw = ImageDraw.Draw(canvas)

    try:
        if status == "complete":
            c_draw.text((160, 30), "\u2714  POUR COMPLETE", font=font_config, fill=(0, 220, 80), anchor="mm")
        else:
            c_draw.text((160, 30), "\u2716  POUR STOPPED", font=font_config, fill=(220, 60, 60), anchor="mm")

        c_draw.line((40, 55, 280, 55), fill=(80, 80, 80), width=1)

        c_draw.text((160, 80), f"Mix {mix_id}", font=font_config, fill=(255, 255, 255), anchor="mm")
        c_draw.text((160, 110), f"Time: {elapsed:.1f}s", font=font_config, fill=(200, 200, 200), anchor="mm")
        c_draw.text((160, 140), f"Motors: {motors_used}", font=font_config, fill=(200, 200, 200), anchor="mm")
        c_draw.text((160, 170), f"Volume: {total_units} ml", font=font_config, fill=(200, 200, 200), anchor="mm")

        # OK button
        c_draw.rectangle((110, 198, 210, 234), fill=(0, 120, 200), outline=(100, 180, 255))
        c_draw.text((160, 216), "OK", font=font_config, fill=(255, 255, 255), anchor="mm")
    except TypeError:
        c_draw.text((20, 20), f"Pour done - Mix {mix_id}", font=font_config, fill=(255, 255, 255))

    rotated = canvas.rotate(90, expand=True)
    image.paste(rotated, (0, 0), rotated)
    display.image(image)

def check_buttons():
    """Check if STOP or PAUSE/RESUME button was pressed. Returns 'stop', 'pause', or None."""
    raw = touch.get_coordinates()
    if raw is None:
        return None
    raw_x, raw_y = raw
    # Buttons are at canvas y=200..236 -> rotated: raw_x ~ 200+
    # STOP  canvas x=20..165  -> rotated: raw_y ~ 20..165
    # PAUSE canvas x=173..318 -> rotated: raw_y ~ 173..318
    if raw_x > 185:
        if 10 < raw_y < 170:
            return 'stop'
        elif 165 < raw_y < 320:
            return 'pause'
    return None

def simulate_pour(mix_id):
    """Run pouring: max 2 motors concurrently, time-based progress."""
    mix = syrup_data[str(mix_id)]
    active_motors = [(s, mix[f"syrup_{s}"]) for s in range(1, 9) if mix[f"syrup_{s}"] > 0]

    if not active_motors:
        log(f"⚠️  [SIM] Mix {mix_id} is empty — no motors to drive.")
        return {"status": "empty", "elapsed": 0, "motors": 0, "units": 0}

    total_motors = len(active_motors)
    total_units = sum(a for _, a in active_motors)
    log(f"🚀 [SIM] DISPENSING Mix {mix_id} — {total_motors} motor(s), {total_units} ml, max {MAX_CONCURRENT} concurrent")

    TICK = 0.1  # 100ms poll for smooth progress
    pending = list(active_motors)       # motors waiting to start
    running = []                        # active: [motor_id, amount, start_active_time]
    finished_units = 0
    t0 = time.time()

    cancelled = False
    paused = False
    pause_start = 0
    total_pause_time = 0

    while pending or running:
        # --- Check for stop / pause ---
        btn = check_buttons()
        if btn == 'stop':
            log(f"   ❌ [SIM] STOPPED by user during Mix {mix_id} pour!")
            cancelled = True
            break
        elif btn == 'pause':
            paused = not paused
            if paused:
                pause_start = time.time()
                log(f"   ⏸️  [SIM] PAUSED by user during Mix {mix_id} pour.")
            else:
                total_pause_time += time.time() - pause_start
                log(f"   ▶️  [SIM] RESUMED Mix {mix_id} pour.")
            time.sleep(0.3)  # debounce

        active_time = time.time() - t0 - total_pause_time

        if paused:
            # Build display with frozen fractions
            motor_display = []
            for m_id, amount, m_start in running:
                duration = amount * SECONDS_PER_UNIT
                frac = min(1.0, (active_time - m_start) / duration)
                motor_display.append((m_id, amount, frac))
            running_frac = sum(a * min(1.0, (active_time - ms) / (a * SECONDS_PER_UNIT)) for _, a, ms in running)
            draw_pouring_screen(mix_id, motor_display, total_motors,
                                finished_units + running_frac, total_units, active_time, paused=True)
            time.sleep(0.05)
            continue

        # --- Start new motors up to MAX_CONCURRENT, with inrush stagger ---
        while pending and len(running) < MAX_CONCURRENT:
            motor_id, amount = pending.pop(0)
            active_time = time.time() - t0 - total_pause_time
            log(f"   ⚙️  Motor {motor_id}: START — {amount} ml ({amount * SECONDS_PER_UNIT:.1f}s)")
            running.append([motor_id, amount, active_time])
            if pending and len(running) < MAX_CONCURRENT:
                log(f"   ⏳ Inrush delay {int(STAGGER_DELAY*1000)}ms before next motor")
                time.sleep(STAGGER_DELAY)

        active_time = time.time() - t0 - total_pause_time

        # --- Check for completed motors ---
        still_running = []
        for m_id, amount, m_start in running:
            duration = amount * SECONDS_PER_UNIT
            if active_time - m_start >= duration:
                finished_units += amount
                log(f"   ✅ Motor {m_id}: DONE — {amount} ml dispensed")
            else:
                still_running.append([m_id, amount, m_start])
        running = still_running

        # --- Build display data with fractional progress ---
        motor_display = []
        for m_id, amount, m_start in running:
            duration = amount * SECONDS_PER_UNIT
            frac = min(1.0, (active_time - m_start) / duration)
            motor_display.append((m_id, amount, frac))
        running_frac = sum(a * min(1.0, (active_time - ms) / (a * SECONDS_PER_UNIT)) for _, a, ms in running)
        draw_pouring_screen(mix_id, motor_display, total_motors,
                            finished_units + running_frac, total_units, active_time)

        time.sleep(TICK)

    elapsed = time.time() - t0 - total_pause_time
    status = "stopped" if cancelled else "complete"
    if cancelled:
        log(f"   🛑 Mix {mix_id} pour STOPPED after {elapsed:.1f}s.")
    else:
        log(f"   🏁 Mix {mix_id} dispensing complete ({elapsed:.1f}s total).")
    return {"status": status, "elapsed": elapsed, "motors": total_motors, "units": total_units}

def execute_pour(mix_id):
    """Device mode: builds a staggered timeline and drives real motors."""
    mix = syrup_data[str(mix_id)]
    schedule = {}
    current_start_time = 0.0

    for i in range(1, 9):
        units = mix[f"syrup_{i}"]
        if units > 0:
            duration = units * SECONDS_PER_UNIT
            schedule[i] = {
                'start_time': current_start_time,
                'stop_time': current_start_time + duration,
                'is_running': False,
                'is_finished': False
            }
            current_start_time += STAGGER_DELAY

    if not schedule:
        log("Mix is empty. Nothing to pour!")
        return

    log(f"🚀 Starting staggered pour for Mix {mix_id}...")
    start_time = time.time()

    cancelled = False
    paused = False
    pause_start = 0
    total_pause_time = 0
    while True:
        # --- Check for stop / pause ---
        btn = check_buttons()
        if btn == 'stop':
            log(f"   ❌ STOPPED by user during Mix {mix_id} pour!")
            cancelled = True
            break
        elif btn == 'pause':
            paused = not paused
            if paused:
                pause_start = time.time()
                # Stop all running motors while paused
                for m_id, timing in schedule.items():
                    if timing['is_running']:
                        motors[m_id]['IN1'].value = False
                        motors[m_id]['IN2'].value = False
                log(f"   ⏸️  PAUSED by user during Mix {mix_id} pour.")
            else:
                total_pause_time += time.time() - pause_start
                # Restart motors that were running
                for m_id, timing in schedule.items():
                    if timing['is_running']:
                        motors[m_id]['IN1'].value = True
                        motors[m_id]['IN2'].value = False
                log(f"   ▶️  RESUMED Mix {mix_id} pour.")
            time.sleep(0.3)  # debounce

        if paused:
            time.sleep(0.05)
            continue

        elapsed = time.time() - start_time - total_pause_time
        all_done = True

        for m_id, timing in schedule.items():
            if not timing['is_finished']:
                all_done = False

                if elapsed >= timing['start_time'] and not timing['is_running']:
                    motors[m_id]['IN1'].value = True
                    motors[m_id]['IN2'].value = False
                    timing['is_running'] = True
                    log(f"   [{elapsed:.2f}s] -> Motor {m_id} ON")

                elif elapsed >= timing['stop_time'] and timing['is_running']:
                    motors[m_id]['IN1'].value = False
                    motors[m_id]['IN2'].value = False
                    timing['is_running'] = False
                    timing['is_finished'] = True
                    log(f"   [{elapsed:.2f}s] -> Motor {m_id} OFF")

        if all_done:
            break

        time.sleep(0.02)

    # Safety shutoff — always stop all motors
    for m_id in schedule.keys():
        motors[m_id]['IN1'].value = False
        motors[m_id]['IN2'].value = False

    elapsed = time.time() - start_time - total_pause_time
    status = "stopped" if cancelled else "complete"
    total_units = sum(s['stop_time'] - s['start_time'] for s in schedule.values()) / SECONDS_PER_UNIT
    if cancelled:
        log(f"🛑 Pour STOPPED! Motors stopped after {elapsed:.2f} seconds.")
    else:
        log(f"🏁 Pour complete! Total time: {elapsed:.2f} seconds.")
    return {"status": status, "elapsed": elapsed, "motors": len(schedule), "units": int(total_units)}

# Boot up
reset_grid_colors()
draw_main_menu()
print("System Ready. Waiting for input...")
if CONFIG["MODE"] == "SIMULATOR":
    print("📱 Tips: Click buttons to interact | Close window to exit")

# ---------------- MAIN LOOP ----------------
while True:
    try:
        raw = touch.get_coordinates()
        if raw is None:
            time.sleep(0.02)
            continue
            
        raw_x, raw_y = raw
        if raw_x < 10 or raw_x > 4000 or raw_y < 10 or raw_y > 4000:
            continue

        # --- WARP-CORRECTED BOUNDARY LOGIC ---
        if raw_y > 230:
            row = 0
            if raw_x < 85: col = 0
            elif raw_x < 160: col = 1
            else: col = 2
        elif raw_y > 110:
            row = 1
            if raw_x < 85: col = 0
            elif raw_x < 160: col = 1
            else: col = 2
        else:
            row = 2
            if raw_x < 85: col = 0
            elif raw_x < 160: col = 1
            else: col = 2

        hit_number = number_map[row][col]

        # ==========================================
        # STATE MACHINE
        # ==========================================
        
        if APP_STATE == "MAIN_MENU":
            reset_grid_colors() 
            grid_colors[row][col] = HIGHLIGHT_COLOR
            draw_main_menu()

            start_time = time.time()
            wait_for_release()
            duration = time.time() - start_time

            reset_grid_colors()
            draw_main_menu()

            if duration >= 0.5:
                # LONG PRESS
                passcode_buffer.clear()
                current_view_mix = hit_number
                log(f"🔍 [SIM] Viewing Mix {hit_number} recipe...")
                mix = syrup_data[str(hit_number)]
                for s in range(1, 9):
                    amt = mix[f"syrup_{s}"]
                    if amt > 0:
                        log(f"   Motor {s}: {amt} ml queued")
                APP_STATE = "VIEW_MIX"
                draw_view_mix(current_view_mix)
            else:
                # SHORT TAP
                passcode_buffer.append(hit_number)
                if len(passcode_buffer) > 4: 
                    passcode_buffer.pop(0)
                    
                if passcode_buffer == SECRET_CODE:
                    print("PASSCODE ACCEPTED!")
                    APP_STATE = "SELECT_MIX"
                    passcode_buffer.clear()
                    draw_select_mix()

        elif APP_STATE == "VIEW_MIX":
            # col 2 visually maps to the bottom third of the screen in landscape
            if col == 2:
                # Swapped row logic to correctly match Visual Left and Visual Right
                if row == 2:
                    # Visual Left side (BACK button)
                    draw_view_mix(current_view_mix, active_button="BACK")
                    wait_for_release()
                    APP_STATE = "MAIN_MENU"
                    reset_grid_colors()
                    draw_main_menu()
                elif row == 0:
                    # Visual Right side (POUR button)
                    draw_view_mix(current_view_mix, active_button="POUR")
                    wait_for_release()
                    APP_STATE = "POURING"
                    if CONFIG["MODE"] == "DEVICE":
                        result = execute_pour(current_view_mix)
                    else:
                        result = simulate_pour(current_view_mix)
                    if result and result.get("status") != "empty":
                        draw_summary_screen(current_view_mix, result["status"],
                                            result["elapsed"], result["motors"], result["units"])
                        # Wait for OK tap
                        while True:
                            raw = touch.get_coordinates()
                            if raw is not None:
                                break
                            time.sleep(0.02)
                    APP_STATE = "MAIN_MENU"
                    reset_grid_colors()
                    draw_main_menu()

        elif APP_STATE == "SELECT_MIX":
            wait_for_release()
            current_edit_mix = hit_number
            APP_STATE = "EDIT_DASHBOARD"
            draw_edit_dashboard()

        elif APP_STATE == "EDIT_DASHBOARD":
            wait_for_release()
            if hit_number == 9:
                save_mixes(syrup_data)
                log(f"💾 [SIM] Mix {current_edit_mix} saved. Motor config:")
                mix = syrup_data[str(current_edit_mix)]
                for s in range(1, 9):
                    amt = mix[f"syrup_{s}"]
                    status = f"{amt} ml" if amt > 0 else "OFF"
                    log(f"   Motor {s}: {status}")
                APP_STATE = "MAIN_MENU"
                reset_grid_colors()
                draw_main_menu()
            else:
                val = syrup_data[str(current_edit_mix)][f"syrup_{hit_number}"]
                val += 1
                if val > 10: val = 0
                syrup_data[str(current_edit_mix)][f"syrup_{hit_number}"] = val
                log(f"   ⚙️  [SIM] Motor {hit_number} set to {val} units (Mix {current_edit_mix})")
                
                x1, y1 = col * box_w, row * box_h
                draw.rectangle((x1, y1, x1 + box_w, y1 + box_h), fill=(0, 255, 255))
                display.image(image)
                time.sleep(0.05)
                draw_edit_dashboard()

    except Exception as e:
        if str(e):
            log(f"❌ [SIM] Error: {e}")
    time.sleep(0.02)
