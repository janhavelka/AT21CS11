#pragma once

#include <Arduino.h>

namespace bus_diag {
inline void scan() {
  Serial.println("scan: not supported for single-wire AT21CS bus");
}
}
