/*
  WiFiProgrammer - Callbacks and status LED (optional extras)

  - setStatusLed(pin)  : LED blinks fast while the board is being programmed
  - onStart / onProgress / onSuccess / onError : your own functions
    are called automatically.

  Note: callbacks run in the background task, keep them short
  (no delay(), just print or set a flag).
*/

#include <WiFiProgrammer.h>

#define LED_PIN 2

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  WiFiProgrammer.setStatusLed(LED_PIN);   // fast blink while programming

  WiFiProgrammer.onStart([]() {
    Serial.println(">>> Programming started");
  });

  WiFiProgrammer.onProgress([](uint8_t percent) {
    if (percent % 10 == 0) Serial.printf(">>> Progress: %d%%\n", percent);
  });

  WiFiProgrammer.onSuccess([]() {
    Serial.println(">>> Done! Restarting with the new program...");
  });

  WiFiProgrammer.onError([](const char *reason) {
    Serial.printf(">>> Error: %s\n", reason);
  });

  WiFiProgrammer.begin();
}

void loop() {
  // Your own code here. Example: show if programming is in progress
  if (WiFiProgrammer.isUpdating()) {
    // e.g. pause your motors / outputs here
  } else {
    digitalWrite(LED_PIN, millis() / 1000 % 2);   // slow blink 1 s
  }
  delay(10);
}
