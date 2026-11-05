/*
  ============================================================
   WiFiProgrammer - SENDER example  (ESP32-S3)
  ============================================================
   This example is for the SENDER board (ESP32-S3 with built-in RGB LED).
   The RECEIVER board uses the library:  WiFiProgrammer.begin();
   (see the examples Basic, CustomSettings, ...)

   What the sender does:
     - Connects to the receiver's WiFi (default: ESP32-Receiver)
     - Creates its own WiFi (ESP32-Programmer) -> web page http://192.168.5.1
     - You upload a .bin file on that web page, then press "Program"
       (or press the BOOT button on the board)

   RGB LED:
     RED     = receiver NOT connected
     BLUE    = receiver connected
     ORANGE  = programming
     GREEN   = finished (5 s) -> then connection is re-checked (BLUE / RED)
     PURPLE (blinking) = error (3 s) -> then connection is re-checked

   Arduino IDE settings for the SENDER:
     Board            : ESP32S3 Dev Module
     USB CDC On Boot  : Enabled
     Partition Scheme : one that has SPIFFS (e.g. Default 4MB with spiffs)

   IMPORTANT: RX_SSID, RX_PASS and OTA_KEY below must be the SAME as the
   values used in WiFiProgrammer.begin(ssid, pass, key) on the receiver.
   (If the receiver uses WiFiProgrammer.begin() with no arguments,
    leave the defaults here.)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

// ================= Settings =================
const char* RX_SSID = "ESP32-Receiver";   // receiver WiFi name
const char* RX_PASS = "12345678";         // receiver WiFi password
const char* OTA_KEY = "myKey123";         // secret key (same as receiver)

const char* AP_SSID = "ESP32-Programmer"; // this device WiFi (for the web page)
const char* AP_PASS = "12345678";         // at least 8 characters

IPAddress RX_IP(192, 168, 4, 1);          // receiver address
IPAddress AP_IP(192, 168, 5, 1);          // web page address of the sender
IPAddress AP_MASK(255, 255, 255, 0);

#define BOOT_BTN    0                     // BOOT button
#define LED_BRIGHT  40                    // 0..255
#define FW_PATH     "/firmware.bin"

#ifdef RGB_BUILTIN
  #define LED_PIN RGB_BUILTIN
#else
  #define LED_PIN 48                      // use 38 for DevKitC v1.1
#endif
// ============================================

enum State { ST_DISCONNECTED, ST_CONNECTED, ST_PROGRAMMING, ST_DONE, ST_ERROR };

State    state = ST_DISCONNECTED;
uint32_t stateAt = 0, lastCheck = 0, lastWifiTry = 0;
uint8_t  progress = 0;
bool     wantProgram = false;
String   lastMsg = "";
File     upFile;
WebServer server(80);

// ---------- Web page ----------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="fa" dir="rtl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Programmer</title>
<style>
body{font-family:Tahoma,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:16px}
.c{max-width:480px;margin:auto}
.b{background:#1e293b;border-radius:16px;padding:16px;margin-bottom:14px}
h1{font-size:20px;margin:0 0 12px}
#led{width:70px;height:70px;border-radius:50%;margin:8px auto;background:#ef4444;box-shadow:0 0 25px #ef4444}
button{width:100%;padding:12px;border:0;border-radius:12px;font-size:16px;margin-top:8px;cursor:pointer}
.p{background:#f97316;color:#111}.d{background:#475569;color:#fff}
button:disabled{opacity:.4}
.bar{height:12px;background:#334155;border-radius:8px;overflow:hidden;margin-top:10px}
#bar{height:100%;width:0;background:#38bdf8;transition:width .3s}
input{width:100%;color:#e2e8f0}
small{color:#94a3b8}
</style></head><body><div class="c">
<div class="b" style="text-align:center"><h1>برنامه‌ریز بی‌سیم ESP32</h1>
<div id="led"></div><div id="st" style="font-weight:bold">...</div>
<div class="bar"><div id="bar"></div></div>
<small id="msg"></small></div>
<div class="b"><b>۱) فایل bin</b><br><small id="fw">...</small>
<input type="file" id="f" accept=".bin">
<button class="d" onclick="up()">آپلود فایل</button>
<div class="bar"><div id="ub" style="height:100%;width:0;background:#22c55e"></div></div></div>
<div class="b"><b>۲) پروگرام</b>
<button class="p" id="pg" onclick="pr()">پروگرام گیرنده</button>
<button class="d" onclick="dl()">حذف فایل</button></div>
</div>
<script>
var $=function(i){return document.getElementById(i)};
var N={disconnected:['قطع - گیرنده وصل نیست','#ef4444'],connected:['متصل به گیرنده','#3b82f6'],programming:['در حال پروگرام...','#f97316'],done:['پروگرام تمام شد','#22c55e'],error:['خطا','#a855f7']};
function poll(){fetch('/status').then(function(r){return r.json()}).then(function(s){
var n=N[s.state];$('st').textContent=n[0];$('led').style.background=n[1];$('led').style.boxShadow='0 0 25px '+n[1];
$('bar').style.width=s.progress+'%';$('msg').textContent=s.msg;
$('fw').textContent=s.fw?('فایل ذخیره شده: '+s.size+' بایت'):'فایلی ذخیره نشده';
$('pg').disabled=!(s.state=='connected'&&s.fw);
}).catch(function(){})}
setInterval(poll,700);poll();
function up(){var f=$('f').files[0];if(!f){alert('فایل را انتخاب کنید');return}
var x=new XMLHttpRequest(),d=new FormData();d.append('fw',f,'fw.bin');
x.upload.onprogress=function(e){if(e.lengthComputable)$('ub').style.width=(e.loaded*100/e.total)+'%'};
x.onload=function(){if(x.status!=200)alert('خطا: '+x.responseText);poll()};
x.open('POST','/upload');x.send(d)}
function pr(){fetch('/program',{method:'POST'})}
function dl(){fetch('/delete',{method:'POST'}).then(poll)}
</script></body></html>
)rawliteral";

// ---------- LED ----------
void setLed(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(LED_PIN, r * LED_BRIGHT / 255, g * LED_BRIGHT / 255, b * LED_BRIGHT / 255);
}

void updateLed() {
  static int lastKey = -1;
  int key = (int)state;
  if (state == ST_ERROR) key = 10 + ((millis() / 200) % 2);   // blink
  if (key == lastKey) return;
  lastKey = key;
  switch (key) {
    case ST_DISCONNECTED: setLed(255, 0, 0);   break; // RED
    case ST_CONNECTED:    setLed(0, 0, 255);   break; // BLUE
    case ST_PROGRAMMING:  setLed(255, 90, 0);  break; // ORANGE
    case ST_DONE:         setLed(0, 255, 0);   break; // GREEN
    case 10:              setLed(180, 0, 255); break; // PURPLE
    case 11:              setLed(0, 0, 0);     break; // off (blink)
  }
}

void setState(State s) {
  state = s;
  stateAt = millis();
  updateLed();
}

// ---------- Connection check ----------
bool rxAlive() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient c;
  if (!c.connect(RX_IP, 80, 700)) return false;
  c.print("GET /ping HTTP/1.1\r\nHost: rx\r\nConnection: close\r\n\r\n");
  uint32_t t = millis();
  while (!c.available() && millis() - t < 700) delay(5);
  bool ok = c.available() && c.find("PONG");
  c.stop();
  return ok;
}

void refreshConnection() {
  lastCheck = millis();
  State n = rxAlive() ? ST_CONNECTED : ST_DISCONNECTED;
  if (n != state) setState(n);
}

// ---------- Send firmware to receiver ----------
bool writeAll(WiFiClient &c, const uint8_t *d, size_t n) {
  size_t w = 0;
  uint32_t t0 = millis();
  while (w < n) {
    int r = c.write(d + w, n - w);
    if (r > 0) { w += r; t0 = millis(); }
    else {
      if (!c.connected() || millis() - t0 > 5000) return false;
      delay(1);
    }
  }
  return true;
}

bool sendFirmware() {
  File f = LittleFS.open(FW_PATH, "r");
  if (!f) { lastMsg = "خطا در خواندن فایل"; return false; }

  const String boundary = "----ESP32FWBoundary7MA4YWxk";
  String head = "--" + boundary +
                "\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"firmware.bin\"\r\n"
                "Content-Type: application/octet-stream\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";
  size_t total = f.size();

  WiFiClient c;
  if (!c.connect(RX_IP, 80, 3000)) { f.close(); lastMsg = "اتصال به گیرنده برقرار نشد"; return false; }
  c.setNoDelay(true);

  String req = "POST /update HTTP/1.1\r\nHost: " + RX_IP.toString() +
               "\r\nX-Auth-Key: " + String(OTA_KEY) +
               "\r\nContent-Type: multipart/form-data; boundary=" + boundary +
               "\r\nContent-Length: " + String((unsigned long)(head.length() + total + tail.length())) +
               "\r\nConnection: close\r\n\r\n";
  c.print(req);
  c.print(head);

  uint8_t buf[1460];
  size_t sent = 0;
  bool ok = true;
  while (sent < total) {
    size_t want = total - sent;
    if (want > sizeof(buf)) want = sizeof(buf);
    size_t n = f.read(buf, want);
    if (n == 0) { ok = false; lastMsg = "خطا در خواندن فایل"; break; }
    if (!writeAll(c, buf, n)) { ok = false; lastMsg = "ارتباط با گیرنده قطع شد"; break; }
    sent += n;
    progress = (uint8_t)((uint64_t)sent * 100 / total);
    server.handleClient();   // keep the web page alive
  }
  f.close();

  if (ok) {
    c.print(tail);
    String resp;
    uint32_t t = millis();
    while (millis() - t < 15000) {
      while (c.available()) { resp += (char)c.read(); t = millis(); }
      if (resp.indexOf("UPDATE_OK") >= 0) break;
      if (!c.connected() && !c.available()) break;
      server.handleClient();
      delay(10);
    }
    if (resp.indexOf("UPDATE_OK") < 0) {
      ok = false;
      if (resp.indexOf("401") >= 0) lastMsg = "کلید OTA_KEY اشتباه است";
      else lastMsg = "گیرنده فایل را نپذیرفت (پارتیشن OTA / فایل نامعتبر)";
    }
  }
  c.stop();
  return ok;
}

void runProgram() {
  if (!LittleFS.exists(FW_PATH)) { lastMsg = "ابتدا فایل bin را آپلود کنید"; return; }
  if (!rxAlive()) { lastMsg = "گیرنده متصل نیست"; setState(ST_DISCONNECTED); return; }

  progress = 0;
  lastMsg = "در حال ارسال...";
  setState(ST_PROGRAMMING);                 // ORANGE
  Serial.println("Programming started");

  if (sendFirmware()) {
    progress = 100;
    lastMsg = "پروگرام با موفقیت انجام شد";
    Serial.println("Programming OK");
    setState(ST_DONE);                      // GREEN
  } else {
    Serial.println("Programming FAILED: " + lastMsg);
    setState(ST_ERROR);                     // PURPLE blink
  }
}

// ---------- Web handlers ----------
String stateName() {
  switch (state) {
    case ST_CONNECTED:   return "connected";
    case ST_PROGRAMMING: return "programming";
    case ST_DONE:        return "done";
    case ST_ERROR:       return "error";
    default:             return "disconnected";
  }
}

void handleStatus() {
  bool has = LittleFS.exists(FW_PATH);
  size_t sz = 0;
  if (has) { File f = LittleFS.open(FW_PATH, "r"); sz = f.size(); f.close(); }
  String j = "{\"state\":\"" + stateName() + "\",\"progress\":" + String(progress) +
             ",\"fw\":" + (has ? "true" : "false") + ",\"size\":" + String((unsigned long)sz) +
             ",\"msg\":\"" + lastMsg + "\"}";
  server.send(200, "application/json", j);
}

void handleUploadData() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    if (state == ST_PROGRAMMING) return;
    if (LittleFS.exists(FW_PATH)) LittleFS.remove(FW_PATH);
    upFile = LittleFS.open(FW_PATH, "w");
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upFile) upFile.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    if (upFile) upFile.close();
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (upFile) upFile.close();
    LittleFS.remove(FW_PATH);
  }
}

void handleUploadDone() {
  if (state == ST_PROGRAMMING) { server.send(409, "text/plain", "busy"); return; }
  bool ok = false;
  File f = LittleFS.open(FW_PATH, "r");
  if (f) {
    if (f.size() > 1000) ok = (f.read() == 0xE9);   // ESP image magic byte
    f.close();
  }
  if (!ok) {
    LittleFS.remove(FW_PATH);
    server.send(400, "text/plain", "فایل bin معتبر نیست یا حافظه کافی نیست");
    return;
  }
  lastMsg = "فایل ذخیره شد";
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN, INPUT_PULLUP);
  setState(ST_DISCONNECTED);                // RED at start

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed!");

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP(AP_SSID, AP_PASS, 1);
  WiFi.setAutoReconnect(true);
  WiFi.begin(RX_SSID, RX_PASS);
  lastWifiTry = millis();

  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.on("/program", HTTP_POST, []() {
    if (state == ST_CONNECTED) wantProgram = true;
    server.send(200, "text/plain", "OK");
  });
  server.on("/delete", HTTP_POST, []() {
    if (state != ST_PROGRAMMING) LittleFS.remove(FW_PATH);
    server.send(200, "text/plain", "OK");
  });
  server.begin();

  Serial.println("Sender ready. Web page: http://192.168.5.1");
}

void loop() {
  server.handleClient();

  // BOOT button -> start programming
  static bool prevBtn = HIGH;
  bool cur = digitalRead(BOOT_BTN);
  if (prevBtn == HIGH && cur == LOW) {
    delay(30);
    if (digitalRead(BOOT_BTN) == LOW && state == ST_CONNECTED) wantProgram = true;
  }
  prevBtn = cur;

  if (wantProgram) { wantProgram = false; runProgram(); }

  switch (state) {
    case ST_DISCONNECTED:
    case ST_CONNECTED:
      if (millis() - lastCheck >= 2000) refreshConnection();
      break;
    case ST_DONE:                           // green for 5 s, then re-check
      if (millis() - stateAt >= 5000) refreshConnection();
      break;
    case ST_ERROR:                          // purple blink for 3 s, then re-check
      if (millis() - stateAt >= 3000) refreshConnection();
      break;
    default: break;
  }

  // WiFi reconnect to receiver
  if (state != ST_PROGRAMMING && WiFi.status() != WL_CONNECTED && millis() - lastWifiTry > 10000) {
    lastWifiTry = millis();
    WiFi.disconnect();
    WiFi.begin(RX_SSID, RX_PASS);
  }

  updateLed();
}
