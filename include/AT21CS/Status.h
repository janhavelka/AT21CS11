/// @file Status.h
/// @brief Deterministic status and protocol diagnostics.
#pragma once

#include <cstdint>

namespace AT21CS {

enum class Err : uint8_t {
  OK = 0,
  NOT_BOUND,
  NOT_INITIALIZED,
  INVALID_STATE,
  BUSY,
  INVALID_CONFIG,
  INVALID_PARAM,
  NOT_PRESENT,
  NACK_DEVICE_ADDRESS,
  NACK_MEMORY_ADDRESS,
  NACK_DATA,
  TRANSPORT_TIMEOUT,
  LINE_STUCK,
  IO_ERROR,
  CLOCK_STALLED,
  UNSUPPORTED_COMMAND,
  CRC_MISMATCH,
  PART_MISMATCH,
  VERIFY_MISMATCH,
  INDETERMINATE
};

enum class ProtocolPhase : uint8_t {
  NONE = 0,
  PRESENCE,
  RESET,
  DISCOVERY_SAMPLE,
  DISCOVERY_RELEASE,
  START,
  DEVICE_ADDRESS_WRITE,
  MEMORY_ADDRESS,
  RESTART,
  DEVICE_ADDRESS_READ,
  DATA_WRITE,
  DATA_READ,
  STOP,
  WRITE_HIGH_HOLD,
  VERIFY
};

constexpr const char* toString(Err value) {
  switch (value) {
    case Err::OK: return "OK";
    case Err::NOT_BOUND: return "NOT_BOUND";
    case Err::NOT_INITIALIZED: return "NOT_INITIALIZED";
    case Err::INVALID_STATE: return "INVALID_STATE";
    case Err::BUSY: return "BUSY";
    case Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case Err::INVALID_PARAM: return "INVALID_PARAM";
    case Err::NOT_PRESENT: return "NOT_PRESENT";
    case Err::NACK_DEVICE_ADDRESS: return "NACK_DEVICE_ADDRESS";
    case Err::NACK_MEMORY_ADDRESS: return "NACK_MEMORY_ADDRESS";
    case Err::NACK_DATA: return "NACK_DATA";
    case Err::TRANSPORT_TIMEOUT: return "TRANSPORT_TIMEOUT";
    case Err::LINE_STUCK: return "LINE_STUCK";
    case Err::IO_ERROR: return "IO_ERROR";
    case Err::CLOCK_STALLED: return "CLOCK_STALLED";
    case Err::UNSUPPORTED_COMMAND: return "UNSUPPORTED_COMMAND";
    case Err::CRC_MISMATCH: return "CRC_MISMATCH";
    case Err::PART_MISMATCH: return "PART_MISMATCH";
    case Err::VERIFY_MISMATCH: return "VERIFY_MISMATCH";
    case Err::INDETERMINATE: return "INDETERMINATE";
  }
  return "UNKNOWN";
}

constexpr const char* toString(ProtocolPhase value) {
  switch (value) {
    case ProtocolPhase::NONE: return "NONE";
    case ProtocolPhase::PRESENCE: return "PRESENCE";
    case ProtocolPhase::RESET: return "RESET";
    case ProtocolPhase::DISCOVERY_SAMPLE: return "DISCOVERY_SAMPLE";
    case ProtocolPhase::DISCOVERY_RELEASE: return "DISCOVERY_RELEASE";
    case ProtocolPhase::START: return "START";
    case ProtocolPhase::DEVICE_ADDRESS_WRITE: return "DEVICE_ADDRESS_WRITE";
    case ProtocolPhase::MEMORY_ADDRESS: return "MEMORY_ADDRESS";
    case ProtocolPhase::RESTART: return "RESTART";
    case ProtocolPhase::DEVICE_ADDRESS_READ: return "DEVICE_ADDRESS_READ";
    case ProtocolPhase::DATA_WRITE: return "DATA_WRITE";
    case ProtocolPhase::DATA_READ: return "DATA_READ";
    case ProtocolPhase::STOP: return "STOP";
    case ProtocolPhase::WRITE_HIGH_HOLD: return "WRITE_HIGH_HOLD";
    case ProtocolPhase::VERIFY: return "VERIFY";
  }
  return "UNKNOWN";
}

constexpr int32_t makeProtocolDetail(ProtocolPhase phase, uint16_t byteIndex = 0) {
  return (static_cast<int32_t>(static_cast<uint8_t>(phase)) << 16) |
         static_cast<int32_t>(byteIndex);
}

constexpr ProtocolPhase protocolDetailPhase(int32_t detail) {
  return static_cast<ProtocolPhase>((static_cast<uint32_t>(detail) >> 16) & 0xFFu);
}

constexpr uint16_t protocolDetailIndex(int32_t detail) {
  return static_cast<uint16_t>(static_cast<uint32_t>(detail) & 0xFFFFu);
}

struct Status {
  Err code = Err::OK;
  int32_t detail = 0;
  const char* msg = "OK";

  constexpr bool ok() const { return code == Err::OK; }
  constexpr bool is(Err expected) const { return code == expected; }

  static constexpr Status Ok() { return {}; }
  static constexpr Status Error(Err code, int32_t detail = 0) {
    return Status{code, detail, toString(code)};
  }
};

}  // namespace AT21CS
