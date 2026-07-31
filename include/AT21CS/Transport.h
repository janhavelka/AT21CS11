/// @file Transport.h
/// @brief Framework-neutral single-wire backend contract for AT21CS drivers.
#pragma once

#include <cstdint>

#include "AT21CS/Status.h"

namespace AT21CS {

/// @brief Timing profile passed to timing-critical backend primitives.
///
/// Values are expressed in microseconds. Backends must meet the active AT21CS
/// timing window at the device pin, including callback dispatch, critical
/// sections, GPIO access, and any interrupt masking overhead they introduce.
struct SingleWireTimingProfile {
  uint16_t bitUs;
  uint16_t low0Us;
  uint16_t low1Us;
  uint16_t readLowUs;
  uint16_t readSampleUs;
  uint16_t htssUs;
};

/// @brief Optional backend callbacks for the AT21CS single-wire physical layer.
///
/// The production timing contract is byte-oriented: `writeByteReadAck`,
/// `readByteSendAck`, and `resetAndDiscover` must execute the complete
/// timing-critical sequence internally, MSb-first, with ACK represented as a
/// low bit from the device. Backend implementations are responsible for any
/// critical sections or hardware-specific fast GPIO paths needed to preserve
/// the protocol timing. The core does not wrap injected byte primitives in
/// platform critical sections and does not compensate for callback overhead.
///
/// Injected backends own all board pin policy. Do not combine an injected
/// transport with `Config::sioPin` or `Config::presencePin`; use
/// `presencePresent` for external presence policy.
///
/// Line-level callbacks are intentionally not part of the required high-speed
/// contract. They may be populated for diagnostics, fake transports, or
/// hardware-validated portable backends, but the core driver will reject an
/// injected transport that lacks the byte-level primitives.
struct SingleWireTransport {
  void* user = nullptr;

  /// Optional backend initialization hook. Called from Driver::begin().
  Status (*begin)(void* user) = nullptr;

  /// Optional backend shutdown hook. Called from Driver::end().
  void (*end)(void* user) = nullptr;

  /// Optional presence hook. Return true when the device should be considered present.
  bool (*presencePresent)(void* user) = nullptr;

  /// Required for injected transports. Releases SI/O high between timing phases.
  void (*releaseLine)(void* user) = nullptr;

  /// Optional line-level low pulse hook for validated non-byte backends.
  void (*driveLowForUs)(uint32_t lowUs, void* user) = nullptr;

  /// Optional line-level read hook for validated non-byte backends.
  bool (*readLine)(void* user) = nullptr;

  /// Required for injected transports. Writes one byte and returns true on ACK.
  bool (*writeByteReadAck)(uint8_t value,
                           const SingleWireTimingProfile& timing,
                           void* user) = nullptr;

  /// Required for injected transports. Reads one byte and sends ACK when ack is true.
  uint8_t (*readByteSendAck)(bool ack,
                             const SingleWireTimingProfile& timing,
                             void* user) = nullptr;

  /// Required for injected transports. Performs reset/discovery and returns protocol status.
  Status (*resetAndDiscover)(const SingleWireTimingProfile& highSpeedTiming,
                             void* user) = nullptr;

  /// Optional monotonic millisecond source used when Config::nowMs is null.
  uint32_t (*nowMs)(void* user) = nullptr;

  /// Required for injected transports unless Config::sleepUs is provided.
  void (*sleepUs)(uint32_t us, void* user) = nullptr;
};

}  // namespace AT21CS
