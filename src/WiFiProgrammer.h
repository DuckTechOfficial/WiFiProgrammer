/*
  WiFiProgrammer  -  Receiver library for ESP32
  ------------------------------------------------------------
  Lets another ESP32 (the "sender", e.g. ESP32-S3) program this
  ESP32 over WiFi.

  Minimum usage:

      #include <WiFiProgrammer.h>

      void setup() {
        WiFiProgrammer.begin();      // <-- the only line you need
      }

      void loop() {
        // your code (delay() is allowed)
      }

  By default the library runs in the BACKGROUND (a small FreeRTOS
  task), so you do not need to call anything in loop().
*/
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <functional>

#define WIFIPROGRAMMER_VERSION "1.0.0"

// Defaults - they match the sender code, so it works without any setting.
#define WP_DEFAULT_SSID "ESP32-Receiver"
#define WP_DEFAULT_PASS "12345678"       // at least 8 characters
#define WP_DEFAULT_KEY  "myKey123"       // must be the same in the sender

class WiFiProgrammerClass {
public:
  typedef std::function<void()>            VoidCallback;
  typedef std::function<void(uint8_t)>     ProgressCallback;   // 0..100
  typedef std::function<void(const char*)> ErrorCallback;      // error text

  // ---------------- Main functions ----------------

  // Start the WiFi programmer.
  //   ssid, password : WiFi (Access Point) of this board. Password >= 8 chars.
  //   key            : secret key; sender must use the same key.
  //   background     : true  -> works automatically, nothing needed in loop()
  //                    false -> you must call WiFiProgrammer.handle() in loop()
  // Returns true if started.
  bool begin(const char* ssid     = WP_DEFAULT_SSID,
             const char* password = WP_DEFAULT_PASS,
             const char* key      = WP_DEFAULT_KEY,
             bool background      = true);

  // Only needed when begin(..., background = false).
  void handle();

  // Stop the programmer and turn the Access Point off.
  void stop();

  // ---------------- Optional extras ----------------

  void onStart(VoidCallback cb)        { _onStart = cb; }      // update begins
  void onProgress(ProgressCallback cb) { _onProgress = cb; }   // percent changed
  void onSuccess(VoidCallback cb)      { _onSuccess = cb; }    // finished OK (board restarts after)
  void onError(ErrorCallback cb)       { _onError = cb; }      // something failed

  // This LED blinks fast while programming. activeHigh=false for inverted LEDs.
  void setStatusLed(int pin, bool activeHigh = true);

  // Print messages on Serial (default: true).
  void setDebug(bool enable) { _debug = enable; }

  // ---------------- Information ----------------

  bool        isUpdating() const { return _updating; }
  uint8_t     progress()   const { return _progress; }
  const char* lastError()  const { return _error.c_str(); }
  IPAddress   ip()         const { return WiFi.softAPIP(); }
  const char* ssid()       const { return _ssid.c_str(); }
  static const char* version()   { return WIFIPROGRAMMER_VERSION; }

private:
  WebServer*   _server  = nullptr;
  TaskHandle_t _task    = NULL;
  String       _ssid, _key, _error;

  bool    _started = false, _background = true, _stopReq = false;
  bool    _authOk = false, _updOk = false, _updating = false;
  bool    _firstChunk = true, _debug = true;
  uint8_t _progress = 0;
  size_t  _contentLen = 0, _received = 0;

  int      _ledPin = -1;
  bool     _ledActiveHigh = true, _ledState = false;
  uint32_t _ledTime = 0;

  VoidCallback     _onStart;
  VoidCallback     _onSuccess;
  ProgressCallback _onProgress;
  ErrorCallback    _onError;

  void _setupRoutes();
  void _onUpload();
  void _onFinish();
  void _fail(const String &reason);
  void _log(const String &msg);
  void _led(bool on);
  static void _taskFn(void *arg);
};

// Ready-to-use global object:  WiFiProgrammer.begin();
extern WiFiProgrammerClass WiFiProgrammer;
