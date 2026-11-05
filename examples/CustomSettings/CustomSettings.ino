/*
  WiFiProgrammer - Custom WiFi name / password / key

  begin(ssid, password, key)

  IMPORTANT: the same values must be set in the sender code:
     RX_SSID  = "MyReceiver"
     RX_PASS  = "mypassword123"
     OTA_KEY  = "mySecretKey"
*/

#include <WiFiProgrammer.h>

void setup() {
  Serial.begin(115200);

  //                       WiFi name     password (>= 8)   secret key
  WiFiProgrammer.begin("MyReceiver", "mypassword123", "mySecretKey");
}

void loop() {
  // Your own code here
  Serial.println("Hello from my program!");
  delay(2000);
}
