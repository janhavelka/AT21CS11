#pragma once

#if defined(AT21CS_EXAMPLE_PLATFORM_IDF)
#include "IdfArduinoCompat.h"
#else
#include <Arduino.h>
#endif

namespace bus_diag {
inline void scan() {
  Serial.println("scan: not supported for single-wire AT21CS bus");
}
}
