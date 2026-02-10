/// @file Config.h
/// @brief Configuration types for the AT21CS01/AT21CS11 driver.
#pragma once

#include <cstdint>

namespace AT21CS {

/// Supported device variants.
enum class PartType : uint8_t {
  UNKNOWN = 0,
  AT21CS01,
  AT21CS11
};

/// Communication speed profile.
enum class SpeedMode : uint8_t {
  HIGH_SPEED = 0,
  STANDARD_SPEED
};

/// Driver configuration.
struct Config {
  /// SI/O GPIO pin used by this device instance (required).
  int sioPin = -1;

  /// Optional external presence pin. Set to -1 to disable fast presence checks.
  /// When configured, the driver treats this pin as authoritative for presence.
  int presencePin = -1;

  /// Optional presence pin polarity: true=HIGH means present, false=LOW means present.
  bool presenceActiveHigh = true;

  /// Device address bits A2:A0 (0-7).
  uint8_t addressBits = 0;

  /// Consecutive failures required to move state to OFFLINE.
  uint8_t offlineThreshold = 5;

  /// Maximum time to wait for t_WR completion in waitReady()/write helpers.
  uint32_t writeTimeoutMs = 6;

  /// Discovery retries performed by begin() and recover().
  uint8_t discoveryRetries = 2;

  /// Expected part type; UNKNOWN accepts either AT21CS01 or AT21CS11.
  PartType expectedPart = PartType::UNKNOWN;

  /// Desired speed mode after begin() (AT21CS11 only supports HIGH_SPEED).
  SpeedMode startupSpeed = SpeedMode::HIGH_SPEED;
};

}  // namespace AT21CS
