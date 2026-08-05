/// @file Config.h
/// @brief Per-device configuration independent of platform and transport.
#pragma once

#include <cstdint>

#include "AT21CS/Types.h"

namespace AT21CS {

struct Config {
  uint8_t addressBits = 0;
  uint8_t offlineThreshold = 5;
  PartType expectedPart = PartType::UNKNOWN;
  SpeedMode startupSpeed = SpeedMode::HIGH_SPEED;
};

}  // namespace AT21CS
