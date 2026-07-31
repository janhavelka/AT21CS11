/// @file Config.h
/// @brief Configuration types for the AT21CS01/AT21CS11 driver.
#pragma once

#include <cstdint>

#include "AT21CS/Transport.h"

namespace AT21CS {

/// @brief Supported device variants.
enum class PartType : uint8_t {
  UNKNOWN = 0,
  AT21CS01,
  AT21CS11
};

/// @brief Communication speed profile.
enum class SpeedMode : uint8_t {
  HIGH_SPEED = 0,
  STANDARD_SPEED
};

/// Millisecond timestamp callback.
/// @param user User context pointer passed through from Config
/// @return Current monotonic milliseconds
using NowMsFn = uint32_t (*)(void* user);

/// Microsecond sleep callback.
/// @param us Microseconds to sleep/busy-wait
/// @param user User context pointer passed through from Config
using SleepUsFn = void (*)(uint32_t us, void* user);

/// @brief Driver configuration.
struct Config {
  /// Compatibility SI/O pin for the built-in ESP32/Arduino backend.
  ///
  /// Used only when `transport == nullptr`. Leave this at -1 when injecting an
  /// explicit `SingleWireTransport`; the injected backend owns all pin policy.
  /// Required for the compatibility backend and valid range is 0..63.
  int sioPin = -1;

  /// Compatibility external presence pin for the built-in ESP32/Arduino backend.
  ///
  /// Used only when `transport == nullptr`. Set to -1 to disable fast presence
  /// checks. When configured, this pin is authoritative for presence and must
  /// be different from `sioPin`. Injected transports must use
  /// `SingleWireTransport::presencePresent` instead.
  int presencePin = -1;

  /// Compatibility presence pin polarity for the built-in backend:
  /// true=HIGH means present, false=LOW means present.
  bool presenceActiveHigh = true;

  /// Device address bits A2:A0 (0-7).
  uint8_t addressBits = 0;

  /// Consecutive failures required to move state to OFFLINE. Clamped to 1 in begin().
  uint8_t offlineThreshold = 5;

  /// Maximum time to wait for t_WR completion in waitReady()/write helpers.
  /// Range: 1..250 ms.
  uint32_t writeTimeoutMs = 25;

  /// Discovery retries performed by begin() and recover().
  uint8_t discoveryRetries = 2;

  /// Expected part type; UNKNOWN accepts either AT21CS01 or AT21CS11.
  PartType expectedPart = PartType::UNKNOWN;

  /// Desired speed mode after begin() (AT21CS11 only supports HIGH_SPEED).
  SpeedMode startupSpeed = SpeedMode::HIGH_SPEED;

  /// Optional monotonic millisecond source.
  /// If null, an injected transport may supply the timebase; otherwise the
  /// built-in platform backend provides it when available.
  NowMsFn nowMs = nullptr;

  /// Optional microsecond delay hook used by bit-banging timing.
  /// If null, an injected transport must supply the wait primitive; otherwise
  /// the built-in platform backend provides it when available.
  SleepUsFn sleepUs = nullptr;

  /// User context for timing callbacks.
  void* timeUser = nullptr;

  /// Optional explicit single-wire backend. When set, the driver uses this
  /// transport instead of the built-in platform pin path.
  const SingleWireTransport* transport = nullptr;
};

}  // namespace AT21CS
