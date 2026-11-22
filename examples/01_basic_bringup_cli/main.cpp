#include <Arduino.h>

#include "ArduinoCliOutput.h"
#include "BringupCli.h"

namespace {

uint32_t nowMs() { return millis(); }

int readChar() { return Serial.available() > 0 ? Serial.read() : -1; }

at21cs_example::WireInstance wire;
at21cs_example::ArduinoCliOutput output;
at21cs_example::BringupCli bringup{wire, output, nowMs, readChar};

}  // namespace

void setup() {
  Serial.begin(at21cs_example::board::SERIAL_BAUD);
  const uint32_t serialStartMs = millis();
  while (!Serial && static_cast<uint32_t>(millis() - serialStartMs) < 3000U) {
    delay(10);
  }

  bringup.start();
}

void loop() {
  bringup.pollInput();
  bringup.step();
}
