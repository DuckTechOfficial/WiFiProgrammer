/*
  WiFiProgrammer - Basic example (the simplest possible)

  Just ONE line is needed:  WiFiProgrammer.begin();

  After uploading this once by USB, you can send any new program
  to this board over WiFi, as long as the new program also
  contains WiFiProgrammer.begin().

  The library works in the background, so even delay() is fine.

  Make a .bin file:  Sketch -> Export Compiled Binary
  and send  Basic.ino.bin  with the sender board.
*/

#include <WiFiProgrammer.h>

#define LED_PIN 2   // built-in LED of most ESP32 boards

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  WiFiProgrammer.begin();        // <<< the only line you need
}

void loop() {
  // Your own code. delay() is allowed.
  digitalWrite(LED_PIN, HIGH);
  delay(1000);
  digitalWrite(LED_PIN, LOW);
  delay(1000);
}
