/**
 * @file BoardConfig.h
 * @brief Example pin defaults for ESP32-S2 / ESP32-S3 demo wiring.
 */

#pragma once

#include <Arduino.h>

namespace board {

static constexpr uint32_t SERIAL_BAUD = 115200;

// Primary AT21CS device instance.
static constexpr int SIO_PRIMARY = 6;
static constexpr int PRESENCE_PRIMARY = -1;
static constexpr uint8_t ADDRESS_BITS_PRIMARY = 0;

// Secondary/tertiary pins used by multi_device_demo.
static constexpr int SIO_SECONDARY = 10;
static constexpr int PRESENCE_SECONDARY = 11;
static constexpr uint8_t ADDRESS_BITS_SECONDARY = 1;

static constexpr int SIO_TERTIARY = 12;
static constexpr int PRESENCE_TERTIARY = 13;
static constexpr uint8_t ADDRESS_BITS_TERTIARY = 2;

inline void initSerial() {
  Serial.begin(SERIAL_BAUD);
  // Allow native USB CDC targets (ESP32-S2/S3) to enumerate before first log.
  const uint32_t startMs = millis();
  while (!Serial && (millis() - startMs) < 3000U) {
    delay(10);
  }
}

}  // namespace board
