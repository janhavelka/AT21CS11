#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("01_basic_bringup_cli is deprecated. Use examples/presence_and_discovery.");
}

void loop() {
}
