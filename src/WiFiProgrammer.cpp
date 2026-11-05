#include "WiFiProgrammer.h"

WiFiProgrammerClass WiFiProgrammer;

// ============================================================
//  Helpers
// ============================================================
void WiFiProgrammerClass::_log(const String &msg) {
  if (!_debug) return;
  Serial.print("[WiFiProgrammer] ");
  Serial.println(msg);
}

void WiFiProgrammerClass::_led(bool on) {
  if (_ledPin < 0) return;
  _ledState = on;
  digitalWrite(_ledPin, (on == _ledActiveHigh) ? HIGH : LOW);
}

void WiFiProgrammerClass::setStatusLed(int pin, bool activeHigh) {
  _ledPin = pin;
  _ledActiveHigh = activeHigh;
  if (pin >= 0) {
    pinMode(pin, OUTPUT);
    _led(false);
  }
}

void WiFiProgrammerClass::_fail(const String &reason) {
  _updOk = false;
  _updating = false;
  _error = reason;
  _log("ERROR: " + reason);
  if (_onError) _onError(_error.c_str());
}

// ============================================================
//  begin / handle / stop
// ============================================================
bool WiFiProgrammerClass::begin(const char *ssid, const char *password,
                                const char *key, bool background) {
  if (_started) return true;

  if (strlen(password) < 8) {
    _log("ERROR: WiFi password must be at least 8 characters");
    return false;
  }

  _ssid = ssid;
  _key = key;
  _background = background;
  _stopReq = false;

  // Keep STA mode if the user already uses it, just add the Access Point.
  WiFi.mode((wifi_mode_t)(WiFi.getMode() | WIFI_MODE_AP));
  if (!WiFi.softAP(ssid, password, 1, 0, 4)) {   // channel 1, visible, max 4 clients
    _log("ERROR: could not start the Access Point");
    return false;
  }
  WiFi.setSleep(false);   // faster and more stable transfer

  _server = new WebServer(80);
  _setupRoutes();
  _server->begin();
  _started = true;

  _log(String("Ready. WiFi: ") + ssid + "  IP: " + WiFi.softAPIP().toString());

  if (_background) {
    xTaskCreate(_taskFn, "WiFiProg", 8192, this, 1, &_task);
  }
  return true;
}

void WiFiProgrammerClass::handle() {
  if (_started && !_background) _server->handleClient();
}

void WiFiProgrammerClass::stop() {
  if (!_started) return;
  _stopReq = true;
  if (_background) {
    uint32_t t = millis();
    while (_task != NULL && millis() - t < 500) delay(5);
  }
  _server->stop();
  delete _server;
  _server = nullptr;
  WiFi.softAPdisconnect(true);
  _started = false;
  _log("Stopped");
}

void WiFiProgrammerClass::_taskFn(void *arg) {
  WiFiProgrammerClass *self = (WiFiProgrammerClass *)arg;
  while (!self->_stopReq) {
    self->_server->handleClient();
    delay(2);
  }
  self->_task = NULL;
  vTaskDelete(NULL);
}

// ============================================================
//  Web routes
// ============================================================
void WiFiProgrammerClass::_setupRoutes() {
  static const char *headers[] = {"X-Auth-Key", "Content-Length"};
  _server->collectHeaders(headers, 2);

  // Used by the sender to check that this board is alive
  _server->on("/ping", HTTP_GET, [this]() {
    _server->send(200, "text/plain", "PONG");
  });

  // Small info page (open http://192.168.4.1 from a phone connected to this WiFi)
  _server->on("/", HTTP_GET, [this]() {
    String h = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                 "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                 "<title>WiFiProgrammer</title></head>"
                 "<body style='font-family:sans-serif;background:#0f172a;color:#e2e8f0;padding:24px'>"
                 "<h2>WiFiProgrammer is ready</h2><ul>");
    h += "<li>Library version: " + String(WIFIPROGRAMMER_VERSION) + "</li>";
    h += "<li>Chip: " + String(ESP.getChipModel()) + "</li>";
    h += "<li>WiFi: " + _ssid + "</li>";
    h += "<li>Free heap: " + String(ESP.getFreeHeap()) + " bytes</li>";
    h += "<li>Uptime: " + String(millis() / 1000) + " s</li>";
    h += "</ul><p>Send the firmware with the sender board.</p></body></html>";
    _server->send(200, "text/html", h);
  });

  // Firmware upload from the sender
  _server->on("/update", HTTP_POST,
              [this]() { _onFinish(); },
              [this]() { _onUpload(); });
}

// ============================================================
//  Firmware upload
// ============================================================
void WiFiProgrammerClass::_onUpload() {
  HTTPUpload &up = _server->upload();

  if (up.status == UPLOAD_FILE_START) {
    _updOk = false;
    _updating = false;
    _error = "";
    _progress = 0;
    _received = 0;
    _firstChunk = true;
    _contentLen = (size_t)_server->header("Content-Length").toInt();
    _authOk = (_server->header("X-Auth-Key") == _key);

    if (!_authOk) {
      _fail("Wrong key (the key must be the same in sender and receiver)");
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      _fail("Cannot start update (no OTA partition? choose a default Partition Scheme)");
      return;
    }
    _updOk = true;
    _updating = true;
    _log("Update started");
    if (_onStart) _onStart();
  }

  else if (up.status == UPLOAD_FILE_WRITE) {
    if (!_authOk || !_updOk) return;

    if (_firstChunk) {
      _firstChunk = false;
      if (up.currentSize == 0 || up.buf[0] != 0xE9) {   // ESP32 image magic byte
        Update.abort();
        _fail("File is not a valid ESP32 .bin (use the .ino.bin file)");
        return;
      }
    }

    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      Update.abort();
      _fail(String("Write error: ") + Update.errorString());
      return;
    }
    _received += up.currentSize;

    // progress (approximate, based on the request size)
    if (_contentLen > 0) {
      uint32_t p = (uint32_t)((uint64_t)_received * 100 / _contentLen);
      if (p > 99) p = 99;
      if (p != _progress) {
        _progress = (uint8_t)p;
        if (_onProgress) _onProgress(_progress);
      }
    }

    // fast blinking status LED
    if (_ledPin >= 0 && millis() - _ledTime >= 80) {
      _ledTime = millis();
      _led(!_ledState);
    }
  }

  else if (up.status == UPLOAD_FILE_END) {
    if (!_authOk || !_updOk) return;
    if (Update.end(true)) {
      _progress = 100;
      if (_onProgress) _onProgress(100);
      _log("Update finished: " + String((unsigned long)up.totalSize) + " bytes");
    } else {
      _fail(String("Finish error: ") + Update.errorString());
    }
  }

  else if (up.status == UPLOAD_FILE_ABORTED) {
    if (_updating) Update.abort();
    _fail("Upload aborted (connection lost)");
  }
}

void WiFiProgrammerClass::_onFinish() {
  if (!_authOk) {
    _server->send(401, "text/plain", "UNAUTHORIZED");
  } else if (_updOk) {
    _server->send(200, "text/plain", "UPDATE_OK");
    _updating = false;
    if (_onSuccess) _onSuccess();
    _log("Restarting...");
    delay(500);
    ESP.restart();
  } else {
    _server->send(500, "text/plain", "UPDATE_FAIL: " + _error);
  }
  _authOk = false;
  _updOk = false;
  _updating = false;
  _led(false);
}
