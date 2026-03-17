import time
import board
import busio
import digitalio
import json
import os
from PIL import Image, ImageDraw, ImageFont
import adafruit_rgb_display.ili9341 as ili9341
import xpt2046_circuitpython

# ---------------- PERMANENT STORAGE SETUP ----------------
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

# ---------------- HARDWARE SETUP ----------------
spi = busio.SPI(clock=board.SCK, MOSI=board.MOSI, MISO=board.MISO)

display = ili9341.ILI9341(
    spi,
    cs=digitalio.DigitalInOut(board.D8),
    dc=digitalio.DigitalInOut(board.D24),
    rst=digitalio.DigitalInOut(board.D25),
    width=240, height=320, baudrate=10000000
)

touch_cs = digitalio.DigitalInOut(board.D7)
touch_irq = digitalio.DigitalInOut(board.D17)
touch = xpt2046_circuitpython.Touch(spi, cs=touch_cs, interrupt=touch_irq)

width, height = display.width, display.height
image = Image.new("RGB", (width, height), (0, 0, 0))
draw = ImageDraw.Draw(image)

# ---------------- FONTS ----------------
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
        color_ok   = (50, 255, 50) if active_button == "OK"   else (0, 150, 0)
        
        c_draw.rectangle((10, 180, 150, 225), fill=color_back, outline=(255,255,255))
        c_draw.text((80, 202), "BACK", font=font_config, fill=(255, 255, 255), anchor="mm")
        
        c_draw.rectangle((170, 180, 310, 225), fill=color_ok, outline=(255,255,255))
        c_draw.text((240, 202), "OK", font=font_config, fill=(255, 255, 255), anchor="mm")

    except TypeError:
        # Fallback for older systems
        c_draw.text((20, 10), f"MIX {mix_id} CONTENTS", font=font_config, fill=(255, 255, 0))
        c_draw.rectangle((10, 180, 150, 225), fill=(150, 0, 0), outline=(255,255,255))
        c_draw.text((50, 195), "BACK", font=font_config, fill=(255, 255, 255))
        c_draw.rectangle((170, 180, 310, 225), fill=(0, 150, 0), outline=(255,255,255))
        c_draw.text((210, 195), "OK", font=font_config, fill=(255, 255, 255))

    rotated = canvas.rotate(90, expand=True)
    image.paste(rotated, (0, 0), rotated)
    display.image(image)

# Boot up
reset_grid_colors()
draw_main_menu()
print("System Ready. Waiting for input...")

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
                    # Visual Right side (OK button)
                    draw_view_mix(current_view_mix, active_button="OK")
                    wait_for_release()
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
                APP_STATE = "MAIN_MENU"
                reset_grid_colors()
                draw_main_menu()
            else:
                val = syrup_data[str(current_edit_mix)][f"syrup_{hit_number}"]
                val += 1
                if val > 10: val = 0
                syrup_data[str(current_edit_mix)][f"syrup_{hit_number}"] = val
                
                x1, y1 = col * box_w, row * box_h
                draw.rectangle((x1, y1, x1 + box_w, y1 + box_h), fill=(0, 255, 255))
                display.image(image)
                time.sleep(0.05)
                draw_edit_dashboard()

    except Exception:
        pass
    time.sleep(0.02)
