// HAL implementation for ESP32-S2 — real hardware
// ILI9341 display, XPT2046 touch, L9110S motors, LittleFS storage

#ifdef ARDUINO   // only compiled for the ESP32 target

#include "hal.h"
#include "config.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>

// ============================================
// DISPLAY
// ============================================
static TFT_eSPI tft = TFT_eSPI();

// Touch calibration — update for your panel
static uint16_t touchCal[5] = {300, 3600, 300, 3600, 1};
static const uint16_t TOUCH_THRESHOLD = 20;

// Touch interrupt
static volatile bool touchPressed = false;
static unsigned long lastTouchISRTime = 0;
static const unsigned long TOUCH_ISR_DEBOUNCE_MS = 10;

static void IRAM_ATTR touchISR() {
    unsigned long now = millis();
    if (now - lastTouchISRTime < TOUCH_ISR_DEBOUNCE_MS) return;
    lastTouchISRTime = now;
    touchPressed = true;
}

// Convert HalColor (stores R8G8B8 packed) to RGB565
static inline uint16_t toRGB565(HalColor c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8)  & 0xFF;
    uint8_t b =  c        & 0xFF;
    return tft.color565(r, g, b);
}

HalColor hal_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void hal_fillScreen(HalColor c)                         { tft.fillScreen(toRGB565(c)); }
void hal_fillRect(int x, int y, int w, int h, HalColor c) { tft.fillRect(x, y, w, h, toRGB565(c)); }
void hal_drawRect(int x, int y, int w, int h, HalColor c) { tft.drawRect(x, y, w, h, toRGB565(c)); }
void hal_fillCircle(int cx, int cy, int r, HalColor c)  { tft.fillCircle(cx, cy, r, toRGB565(c)); }
void hal_drawCircle(int cx, int cy, int r, HalColor c)  { tft.drawCircle(cx, cy, r, toRGB565(c)); }
void hal_drawHLine(int x, int y, int w, HalColor c)     { tft.drawFastHLine(x, y, w, toRGB565(c)); }

// Datum mapping
static const uint8_t datumMap[] = {
    TL_DATUM, TC_DATUM, TR_DATUM,
    ML_DATUM, MC_DATUM, MR_DATUM,
    BL_DATUM, BC_DATUM, BR_DATUM,
};

void hal_setTextDatum(HalDatum d) {
    tft.setTextDatum(datumMap[d]);
}

static HalColor _textBg = 0;

void hal_setTextColor(HalColor fg, HalColor bg) {
    _textBg = bg;
    tft.setTextColor(toRGB565(fg), toRGB565(bg));
}

void hal_drawString(const char *str, int x, int y, int font) {
    tft.drawString(str, x, y, font);
}

void hal_drawNumber(int num, int x, int y, int font) {
    tft.drawNumber(num, x, y, font);
}

#include "FreeSansTurkish7pt.h"
#include "FreeSansTurkish8pt.h"
#include "FreeSansTurkish9pt.h"
#include "FreeSansTurkish10pt.h"
#include "FreeSansTurkish11pt.h"
#include "FreeSansTurkish12pt.h"

static const GFXfont *turkishFont(int size) {
    switch (size) {
        case  7: return &FreeSans7pt8b;
        case  8: return &FreeSans8pt8b;
        default:
        case  9: return &FreeSans9pt8b;
        case 10: return &FreeSans10pt8b;
        case 11: return &FreeSans11pt8b;
        case 12: return &FreeSans12pt8b;
    }
}

void hal_drawString_Turkish(const char *str, int x, int y, int size) {
    tft.setFreeFont(turkishFont(size));
    tft.drawString(str, x, y);
    tft.setFreeFont(nullptr);
}

// ============================================
// TOUCH
// ============================================

// Take multiple SPI readings and accept only if they agree within ±tolerance.
// Rejects phantom coordinates caused by WiFi radio noise on the shared SPI bus.
static bool getValidatedTouch(uint16_t &x, uint16_t &y) {
    const int SAMPLES = 3;
    const int TOLERANCE = 10;
    uint16_t xs[SAMPLES], ys[SAMPLES];
    for (int i = 0; i < SAMPLES; i++) {
        if (!tft.getTouch(&xs[i], &ys[i], TOUCH_THRESHOLD)) return false;
    }
    // Check all samples are within tolerance of the first
    for (int i = 1; i < SAMPLES; i++) {
        if (abs((int)xs[i] - (int)xs[0]) > TOLERANCE ||
            abs((int)ys[i] - (int)ys[0]) > TOLERANCE) return false;
    }
    x = xs[0];
    y = ys[0];
    return true;
}

bool hal_getTouch(uint16_t &x, uint16_t &y) {
    // Only poll the XPT2046 when the IRQ pin actually fired (touch detected
    // by hardware). This prevents phantom reads from WiFi SPI noise.
    if (!touchPressed) return false;
    touchPressed = false;

    // Double-check the IRQ pin is still LOW (finger still on screen)
    if (digitalRead(XPT_IRQ) == HIGH) return false;

    return getValidatedTouch(x, y);
}

void hal_waitForRelease() {
    uint16_t x, y;
    while (tft.getTouch(&x, &y, TOUCH_THRESHOLD)) {
        delay(20);
    }
    touchPressed = false;   // clear any ISR that fired during release
    delay(50);
}

// ============================================
// MOTORS
// ============================================
void hal_initMotors() {
    for (int i = 0; i < 8; i++) {
        pinMode(MOTOR_PINS[i].in1, OUTPUT);
        pinMode(MOTOR_PINS[i].in2, OUTPUT);
        digitalWrite(MOTOR_PINS[i].in1, LOW);
        digitalWrite(MOTOR_PINS[i].in2, LOW);
    }
}

void hal_motorOn(int id) {
    digitalWrite(MOTOR_PINS[id - 1].in1, HIGH);
    digitalWrite(MOTOR_PINS[id - 1].in2, LOW);
}

void hal_motorOff(int id) {
    digitalWrite(MOTOR_PINS[id - 1].in1, LOW);
    digitalWrite(MOTOR_PINS[id - 1].in2, LOW);
}

void hal_allMotorsOff() {
    for (int i = 1; i <= 8; i++) hal_motorOff(i);
}

// ============================================
// STORAGE
// ============================================
int hal_readFile(const char *path, char *buf, size_t bufSize) {
    File f = LittleFS.open(path, "r");
    if (!f) return -1;
    int n = f.readBytes(buf, bufSize - 1);
    buf[n] = '\0';
    f.close();
    return n;
}

bool hal_writeFile(const char *path, const char *buf, size_t len) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.write((const uint8_t *)buf, len);
    f.close();
    return true;
}

// ============================================
// SYSTEM
// ============================================
unsigned long hal_millis()              { return millis(); }
void          hal_delay(unsigned long ms) { delay(ms); }

void hal_log(const char *msg) {
    Serial.printf("[%lu] %s\n", millis(), msg);
}

static const int LED_PIN = 15;

void hal_ledOn()  { digitalWrite(LED_PIN, HIGH); }
void hal_ledOff() { digitalWrite(LED_PIN, LOW); }

// ============================================
// BUZZER (passive buzzer on BUZZER_PIN via LEDC)
// ============================================
static const int BUZZER_LEDC_CH = 0;

void hal_buzzerInit() {
    ledcSetup(BUZZER_LEDC_CH, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CH);
    ledcWriteTone(BUZZER_LEDC_CH, 0);
}

void hal_buzzerTone(int freqHz, int durationMs) {
    ledcWriteTone(BUZZER_LEDC_CH, freqHz);
    delay(durationMs);
    ledcWriteTone(BUZZER_LEDC_CH, 0);
}

void hal_buzzerOff() {
    ledcWriteTone(BUZZER_LEDC_CH, 0);
}

void hal_pumpEvents() { /* no-op on ESP32 */ }
bool hal_shouldQuit() { return false; }

// ============================================
// WIFI AP + WEB SERVER
// ============================================
static WebServer *webServer = nullptr;
static DNSServer *dnsServer = nullptr;
static bool wifiActive = false;
static bool mixesUpdatedFlag = false;

// Runtime WiFi config (loaded from LittleFS, falls back to config.h defaults)
static char wifiSSID[33] = {};
static char wifiPass[65] = {};

static void loadWifiConfig() {
    char buf[256];
    int n = hal_readFile("/wifi_config.json", buf, sizeof(buf));
    if (n <= 0) {
        strncpy(wifiSSID, WIFI_AP_SSID, sizeof(wifiSSID) - 1);
        strncpy(wifiPass, WIFI_AP_PASS, sizeof(wifiPass) - 1);
        return;
    }
    // Minimal JSON parse for {"ssid":"...","pass":"..."}
    auto extract = [&](const char *key, char *dst, size_t dstSz) {
        char needle[40];
        snprintf(needle, sizeof(needle), "\"%s\":\"", key);
        const char *p = strstr(buf, needle);
        if (!p) return;
        p += strlen(needle);
        const char *end = strchr(p, '"');
        if (!end) return;
        size_t len = end - p;
        if (len >= dstSz) len = dstSz - 1;
        memcpy(dst, p, len);
        dst[len] = '\0';
    };
    wifiSSID[0] = '\0';
    wifiPass[0] = '\0';
    extract("ssid", wifiSSID, sizeof(wifiSSID));
    extract("pass", wifiPass, sizeof(wifiPass));
    if (wifiSSID[0] == '\0') strncpy(wifiSSID, WIFI_AP_SSID, sizeof(wifiSSID) - 1);
    if (wifiPass[0] == '\0') strncpy(wifiPass, WIFI_AP_PASS, sizeof(wifiPass) - 1);
}

static bool saveWifiConfig() {
    char buf[160];
    int len = snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\",\"pass\":\"%s\"}", wifiSSID, wifiPass);
    return hal_writeFile("/wifi_config.json", buf, len);
}

const char* hal_wifiGetSSID() { return wifiSSID; }
const char* hal_wifiGetPass() { return wifiPass; }

static void handleGetMixes() {
    char buf[4096];
    int n = hal_readFile("/syrup_mixes.json", buf, sizeof(buf));
    if (n <= 0) {
        webServer->send(200, "application/json", "{}");
    } else {
        webServer->send(200, "application/json", buf);
    }
}

static void handlePostMixes() {
    if (!webServer->hasArg("plain")) {
        webServer->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    String body = webServer->arg("plain");
    if (body.length() > 4000) {
        webServer->send(413, "application/json", "{\"error\":\"too large\"}");
        return;
    }
    if (hal_writeFile("/syrup_mixes.json", body.c_str(), body.length())) {
        mixesUpdatedFlag = true;
        webServer->send(200, "application/json", "{\"ok\":true}");
    } else {
        webServer->send(500, "application/json", "{\"error\":\"write failed\"}");
    }
}

// Export: download syrup_mixes.json as a file
static void handleExportMixes() {
    File f = LittleFS.open("/syrup_mixes.json", "r");
    if (!f) {
        webServer->send(404, "text/plain", "No mixes file");
        return;
    }
    webServer->sendHeader("Content-Disposition", "attachment; filename=syrup_mixes.json");
    webServer->streamFile(f, "application/json");
    f.close();
}

// Import: upload syrup_mixes.json file
static void handleImportMixes() {
    if (!webServer->hasArg("plain")) {
        webServer->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    String body = webServer->arg("plain");
    if (body.length() < 2 || body.length() > 4000) {
        webServer->send(400, "application/json", "{\"error\":\"invalid size\"}");
        return;
    }
    if (hal_writeFile("/syrup_mixes.json", body.c_str(), body.length())) {
        mixesUpdatedFlag = true;
        webServer->send(200, "application/json", "{\"ok\":true}");
    } else {
        webServer->send(500, "application/json", "{\"error\":\"write failed\"}");
    }
}

static void handleIndex() {
    File f = LittleFS.open("/index.html", "r");
    if (!f) {
        webServer->send(404, "text/plain", "index.html not found");
        return;
    }
    webServer->streamFile(f, "text/html");
    f.close();
}

// Captive portal: redirect all unknown requests to root
static void handleCaptivePortal() {
    webServer->sendHeader("Location", "http://192.168.4.1/", true);
    webServer->send(302, "text/plain", "");
}

static void handleGetWifi() {
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\",\"pass\":\"%s\"}", wifiSSID, wifiPass);
    webServer->send(200, "application/json", buf);
}

static void handlePostWifi() {
    if (!webServer->hasArg("plain")) {
        webServer->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    String body = webServer->arg("plain");
    if (body.length() > 200) {
        webServer->send(413, "application/json", "{\"error\":\"too large\"}");
        return;
    }
    // Parse ssid and pass from JSON
    char newSSID[33] = {}, newPass[65] = {};
    auto extract = [&](const char *src, const char *key, char *dst, size_t dstSz) {
        char needle[40];
        snprintf(needle, sizeof(needle), "\"%s\":\"", key);
        const char *p = strstr(src, needle);
        if (!p) return;
        p += strlen(needle);
        const char *end = strchr(p, '"');
        if (!end) return;
        size_t len = end - p;
        if (len >= dstSz) len = dstSz - 1;
        memcpy(dst, p, len);
        dst[len] = '\0';
    };
    extract(body.c_str(), "ssid", newSSID, sizeof(newSSID));
    extract(body.c_str(), "pass", newPass, sizeof(newPass));
    if (newSSID[0] == '\0') {
        webServer->send(400, "application/json", "{\"error\":\"ssid required\"}");
        return;
    }
    if (strlen(newPass) > 0 && strlen(newPass) < 8) {
        webServer->send(400, "application/json", "{\"error\":\"password must be 8+ chars or empty\"}");
        return;
    }
    strncpy(wifiSSID, newSSID, sizeof(wifiSSID) - 1);
    strncpy(wifiPass, newPass, sizeof(wifiPass) - 1);
    if (saveWifiConfig()) {
        webServer->send(200, "application/json", "{\"ok\":true,\"restart\":true}");
        hal_log("WiFi config saved — restart AP to apply");
    } else {
        webServer->send(500, "application/json", "{\"error\":\"write failed\"}");
    }
}

// ============================================
// OTA FIRMWARE UPDATE
// ============================================
static const char OTA_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Device Update</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:#1a1a2e;color:#e0e0e0;padding:20px;max-width:500px;margin:0 auto}
h1{text-align:center;color:#00d4aa;margin:20px 0;font-size:1.3em}
.card{background:#16213e;border-radius:10px;padding:20px;border:1px solid #0f3460;margin-bottom:16px}
.card h2{font-size:1em;color:#00d4aa;margin-bottom:10px}
label{display:block;margin-bottom:8px;color:#8892b0;font-size:0.9em}
input[type=file]{width:100%;padding:10px;background:#0f3460;border:1px solid #1a4080;border-radius:6px;color:#e0e0e0;margin-bottom:12px}
.btn{display:block;width:100%;padding:12px;border:none;border-radius:8px;font-size:1em;font-weight:600;cursor:pointer;background:#00d4aa;color:#1a1a2e}
.btn-fs{background:#e67e22;color:#fff}
.btn:disabled{opacity:.5;cursor:not-allowed}
.prog{width:100%;height:22px;border-radius:6px;overflow:hidden;background:#0f3460;margin:12px 0;display:none}
.prog-bar{height:100%;width:0%;transition:width .2s}
.pb-fw{background:#00d4aa}
.pb-fs{background:#e67e22}
.st{text-align:center;margin-top:12px;font-size:0.95em}
a{color:#00d4aa;text-decoration:none}
.note{font-size:0.78em;color:#8892b0;margin-top:8px}
</style></head><body>
<h1>&#128268; Device Update</h1>
<div class="card">
<h2>Firmware (.bin)</h2>
<form id="uf"><label>Select firmware.bin:</label>
<input type="file" id="fw" accept=".bin">
<div class="prog" id="prog"><div class="prog-bar pb-fw" id="pbar"></div></div>
<button class="btn" type="submit" id="ubtn">Upload Firmware</button></form>
<div class="st" id="status"></div>
<div class="note">.pio/build/lolin_s2_mini/firmware.bin</div>
</div>
<div class="card">
<h2>Filesystem (.bin)</h2>
<form id="uf2"><label>Select littlefs.bin:</label>
<input type="file" id="fs" accept=".bin">
<div class="prog" id="prog2"><div class="prog-bar pb-fs" id="pbar2"></div></div>
<button class="btn btn-fs" type="submit" id="ubtn2">Upload Filesystem</button></form>
<div class="st" id="status2"></div>
<div class="note">.pio/build/lolin_s2_mini/littlefs.bin</div>
</div>
<p style="text-align:center;margin-top:16px"><a href="/">&larr; Back to Mixer</a></p>
<script>
function doUpload(formId,fileId,progId,barId,stId,btnId,url){
  document.getElementById(formId).onsubmit=function(e){
    e.preventDefault();
    const f=document.getElementById(fileId).files[0];
    if(!f){document.getElementById(stId).textContent='Select a file first';return;}
    const xhr=new XMLHttpRequest();
    const prog=document.getElementById(progId),pbar=document.getElementById(barId),st=document.getElementById(stId),btn=document.getElementById(btnId);
    btn.disabled=true;prog.style.display='block';st.textContent='Uploading...';
    xhr.upload.onprogress=function(ev){if(ev.lengthComputable){const p=Math.round(ev.loaded/ev.total*100);pbar.style.width=p+'%';st.textContent='Uploading: '+p+'%';}};
    xhr.onload=function(){if(xhr.status===200){st.innerHTML='<b style="color:#00d4aa">Success! Rebooting...</b>';setTimeout(()=>{st.textContent='Reconnect to WiFi and refresh.'},5000);}else{st.textContent='Error: '+xhr.responseText;btn.disabled=false;}};
    xhr.onerror=function(){st.textContent='Upload failed \u2014 connection lost';btn.disabled=false;};
    const fd=new FormData();fd.append('firmware',f);
    xhr.open('POST',url);xhr.send(fd);
  };
}
doUpload('uf','fw','prog','pbar','status','ubtn','/api/ota');
doUpload('uf2','fs','prog2','pbar2','status2','ubtn2','/api/ota-fs');
</script></body></html>
)rawhtml";

static void handleOTAPage() {
    webServer->send_P(200, "text/html", OTA_PAGE);
}

static void handleOTAUpload() {
    HTTPUpload &upload = webServer->upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("OTA start: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            hal_log("OTA begin failed");
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            hal_log("OTA write failed");
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("OTA success: %u bytes\n", upload.totalSize);
        } else {
            hal_log("OTA end failed");
        }
    }
}

static void handleOTAResult() {
    if (Update.hasError()) {
        webServer->send(500, "text/plain", "Update failed");
    } else {
        webServer->send(200, "text/plain", "OK — rebooting...");
        delay(500);
        ESP.restart();
    }
}
// Filesystem (LittleFS) OTA upload
static void handleFSOTAUpload() {
    HTTPUpload &upload = webServer->upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("FS OTA start: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
            hal_log("FS OTA begin failed");
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            hal_log("FS OTA write failed");
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("FS OTA success: %u bytes\n", upload.totalSize);
        } else {
            hal_log("FS OTA end failed");
        }
    }
}
bool hal_wifiStart() {
    if (wifiActive) return true;

    WiFi.mode(WIFI_AP);
    bool ok;
    if (strlen(wifiPass) >= 8) {
        ok = WiFi.softAP(wifiSSID, wifiPass);
    } else {
        ok = WiFi.softAP(wifiSSID);
    }
    if (!ok) {
        hal_log("WiFi AP start failed");
        return false;
    }

    webServer = new WebServer(80);
    webServer->on("/", HTTP_GET, handleIndex);
    webServer->on("/api/mixes", HTTP_GET, handleGetMixes);
    webServer->on("/api/mixes", HTTP_POST, handlePostMixes);
    webServer->on("/api/wifi", HTTP_GET, handleGetWifi);
    webServer->on("/api/wifi", HTTP_POST, handlePostWifi);
    webServer->on("/api/export", HTTP_GET, handleExportMixes);
    webServer->on("/api/import", HTTP_POST, handleImportMixes);
    // OTA firmware + filesystem update
    webServer->on("/update", HTTP_GET, handleOTAPage);
    webServer->on("/api/ota", HTTP_POST, handleOTAResult, handleOTAUpload);
    webServer->on("/api/ota-fs", HTTP_POST, handleOTAResult, handleFSOTAUpload);
    // Captive portal detection endpoints
    webServer->on("/generate_204", HTTP_GET, handleCaptivePortal);     // Android
    webServer->on("/gen_204", HTTP_GET, handleCaptivePortal);           // Android
    webServer->on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal); // Apple
    webServer->on("/library/test/success.html", HTTP_GET, handleCaptivePortal); // Apple
    webServer->on("/connecttest.txt", HTTP_GET, handleCaptivePortal);   // Windows
    webServer->on("/redirect", HTTP_GET, handleCaptivePortal);          // Windows
    webServer->on("/fwlink", HTTP_GET, handleCaptivePortal);            // Windows
    webServer->onNotFound(handleCaptivePortal);  // catch-all redirect
    webServer->begin();

    // DNS server: resolve all domains to our IP (captive portal)
    dnsServer = new DNSServer();
    dnsServer->start(53, "*", WiFi.softAPIP());

    wifiActive = true;
    char msg[80];
    snprintf(msg, sizeof(msg), "WiFi AP started: %s @ %s",
             wifiSSID, WiFi.softAPIP().toString().c_str());
    hal_log(msg);
    return true;
}

void hal_wifiStop() {
    if (!wifiActive) return;
    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
    if (webServer) {
        webServer->stop();
        delete webServer;
        webServer = nullptr;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiActive = false;
    hal_log("WiFi AP stopped");
}

bool hal_wifiIsActive() { return wifiActive; }

void hal_wifiProcess() {
    if (wifiActive) {
        if (dnsServer) dnsServer->processNextRequest();
        if (webServer) webServer->handleClient();
    }
}

bool hal_wifiMixesUpdated() {
    if (mixesUpdatedFlag) {
        mixesUpdatedFlag = false;
        return true;
    }
    return false;
}

// ============================================
// ARDUINO ENTRY POINTS → app_setup / app_loop
// ============================================
void setup() {
    // Motors FIRST — prevent GPIO float from activating motors during boot
    hal_initMotors();

    Serial.begin(115200);
    unsigned long serialWait = millis();
    while (!Serial && (millis() - serialWait < 3000)) delay(10);

    // Display
    tft.init();
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);
    tft.setTouch(touchCal);

    // Touch interrupt
    pinMode(XPT_IRQ, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(XPT_IRQ), touchISR, FALLING);

    // Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed!");
    }

    // Load WiFi config from LittleFS (or use defaults from config.h)
    loadWifiConfig();

    // LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Buzzer
    hal_buzzerInit();

    // Hand off to shared app code
    app_setup();
}

void loop() {
    // On ESP32, only process touch when ISR fires
    // But we still call app_loop() every iteration for heartbeat etc.
    // The touch-press flag is checked inside app_loop via hal_getTouch.
    // We gate polling: only call hal_getTouch when ISR triggered.
    app_loop();
}

#endif // ARDUINO
