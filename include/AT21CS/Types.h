/// @file Types.h
/// @brief Common scalar types for the AT21CS01/AT21CS11 library.
#pragma once

#include <cstdint>

namespace AT21CS {

enum class PartType : uint8_t {
  UNKNOWN = 0,
  AT21CS01,
  AT21CS11
};

enum class SpeedMode : uint8_t {
  HIGH_SPEED = 0,
  STANDARD_SPEED
};

enum class DriverState : uint8_t {
  UNINIT = 0,
  PROBING,
  INIT_CONFIG,
  READY,
  BUSY,
  DEGRADED,
  OFFLINE,
  RECOVERING,
  // Reserved by the public state model; v2 has no transition into this state.
  SLEEPING,
  FAULT
};

enum class WriteEffect : uint8_t {
  NOT_ATTEMPTED = 0,
  MAY_HAVE_COMMITTED,
  COMMITTED
};

enum class MutationEffect : uint8_t {
  NOT_ATTEMPTED = 0,
  MAY_HAVE_COMMITTED,
  ACCEPTED,
  VERIFIED
};

constexpr const char* toString(PartType value) {
  switch (value) {
    case PartType::UNKNOWN: return "UNKNOWN";
    case PartType::AT21CS01: return "AT21CS01";
    case PartType::AT21CS11: return "AT21CS11";
  }
  return "UNKNOWN";
}

constexpr const char* toString(SpeedMode value) {
  switch (value) {
    case SpeedMode::HIGH_SPEED: return "HIGH_SPEED";
    case SpeedMode::STANDARD_SPEED: return "STANDARD_SPEED";
  }
  return "UNKNOWN";
}

constexpr const char* toString(DriverState value) {
  switch (value) {
    case DriverState::UNINIT: return "UNINIT";
    case DriverState::PROBING: return "PROBING";
    case DriverState::INIT_CONFIG: return "INIT_CONFIG";
    case DriverState::READY: return "READY";
    case DriverState::BUSY: return "BUSY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE: return "OFFLINE";
    case DriverState::RECOVERING: return "RECOVERING";
    case DriverState::SLEEPING: return "SLEEPING";
    case DriverState::FAULT: return "FAULT";
  }
  return "UNKNOWN";
}

constexpr const char* toString(WriteEffect value) {
  switch (value) {
    case WriteEffect::NOT_ATTEMPTED: return "NOT_ATTEMPTED";
    case WriteEffect::MAY_HAVE_COMMITTED: return "MAY_HAVE_COMMITTED";
    case WriteEffect::COMMITTED: return "COMMITTED";
  }
  return "UNKNOWN";
}

constexpr const char* toString(MutationEffect value) {
  switch (value) {
    case MutationEffect::NOT_ATTEMPTED: return "NOT_ATTEMPTED";
    case MutationEffect::MAY_HAVE_COMMITTED: return "MAY_HAVE_COMMITTED";
    case MutationEffect::ACCEPTED: return "ACCEPTED";
    case MutationEffect::VERIFIED: return "VERIFIED";
  }
  return "UNKNOWN";
}

}  // namespace AT21CS
