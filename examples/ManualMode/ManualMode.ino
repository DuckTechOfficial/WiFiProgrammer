/*
  WiFiProgrammer - Manual mode (advanced, optional)

  Normally the library works in the background and nothing is needed in loop().
  If you prefer to control it yourself, use begin(..., false) and call
  WiFiProgrammer.handle() often in loop().

  In this mode do NOT use long delay() in loop().
*/

#include <WiFiProgrammer.h>

#define LED_PIN 2

uint32_t lastToggle = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // last parameter false = manual mode
  WiFiProgrammer.begin(WP_DEFAULT_SSID, WP_DEFAULT_PASS, WP_DEFAULT_KEY, false);
}

void loop() {
  WiFiProgrammer.handle();      // <<< must be called often

  // Your own code with millis() instead of delay()
  if (millis() - lastToggle >= 250) {
    lastToggle = millis();
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
}
