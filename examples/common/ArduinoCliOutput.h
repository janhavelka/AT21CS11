#pragma once

#include <Arduino.h>

#include "CliOutput.h"

namespace at21cs_example {

class ArduinoCliOutput final : public CliOutput {
 public:
  void write(const char* bytes, size_t length) override {
    Serial.write(reinterpret_cast<const uint8_t*>(bytes), length);
  }
};

}  // namespace at21cs_example
