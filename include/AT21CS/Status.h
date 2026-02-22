/// @file Status.h
/// @brief Status and error codes for the AT21CS01/AT21CS11 driver.
#pragma once

#include <cstdint>

namespace AT21CS {

/// Error codes for all fallible driver operations.
enum class Err : uint8_t {
  OK = 0,
  NOT_INITIALIZED,
  INVALID_STATE,
  INVALID_CONFIG,
  INVALID_PARAM,
  NOT_PRESENT,
  DISCOVERY_FAILED,
  NACK_DEVICE_ADDRESS,
  NACK_MEMORY_ADDRESS,
  NACK_DATA,
  BUSY_TIMEOUT,
  UNSUPPORTED_COMMAND,
  CRC_MISMATCH,
  PART_MISMATCH,
  IO_ERROR
};

/// Status structure returned by all fallible APIs.
struct Status {
  Err code;
  int32_t detail;
  const char* msg;

  /// @return true when code == Err::OK.
  constexpr bool ok() const { return code == Err::OK; }

  /// Create a successful status value.
  static constexpr Status Ok() {
    return Status{Err::OK, 0, "OK"};
  }

  /// Create an error status value.
  static constexpr Status Error(Err err, const char* message, int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

}  // namespace AT21CS
