/// @file Transport.h
/// @brief Whole-frame framework-neutral single-wire transport contract.
#pragma once

#include <cstddef>
#include <cstdint>

#include "AT21CS/Types.h"

namespace AT21CS {

enum class TransportCode : uint8_t {
  OK = 0,
  NACK,
  TIMEOUT,
  LINE_STUCK,
  IO_ERROR
};

enum class TransferPhase : uint8_t {
  NONE = 0,
  PRESENCE,
  RESET_LOW,
  RESET_RECOVERY,
  DISCOVERY_REQUEST,
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
  WAIT_HIGH
};

constexpr const char* toString(TransportCode value) {
  switch (value) {
    case TransportCode::OK: return "OK";
    case TransportCode::NACK: return "NACK";
    case TransportCode::TIMEOUT: return "TIMEOUT";
    case TransportCode::LINE_STUCK: return "LINE_STUCK";
    case TransportCode::IO_ERROR: return "IO_ERROR";
  }
  return "UNKNOWN";
}

constexpr const char* toString(TransferPhase value) {
  switch (value) {
    case TransferPhase::NONE: return "NONE";
    case TransferPhase::PRESENCE: return "PRESENCE";
    case TransferPhase::RESET_LOW: return "RESET_LOW";
    case TransferPhase::RESET_RECOVERY: return "RESET_RECOVERY";
    case TransferPhase::DISCOVERY_REQUEST: return "DISCOVERY_REQUEST";
    case TransferPhase::DISCOVERY_SAMPLE: return "DISCOVERY_SAMPLE";
    case TransferPhase::DISCOVERY_RELEASE: return "DISCOVERY_RELEASE";
    case TransferPhase::START: return "START";
    case TransferPhase::DEVICE_ADDRESS_WRITE: return "DEVICE_ADDRESS_WRITE";
    case TransferPhase::MEMORY_ADDRESS: return "MEMORY_ADDRESS";
    case TransferPhase::RESTART: return "RESTART";
    case TransferPhase::DEVICE_ADDRESS_READ: return "DEVICE_ADDRESS_READ";
    case TransferPhase::DATA_WRITE: return "DATA_WRITE";
    case TransferPhase::DATA_READ: return "DATA_READ";
    case TransferPhase::STOP: return "STOP";
    case TransferPhase::WAIT_HIGH: return "WAIT_HIGH";
  }
  return "UNKNOWN";
}

inline constexpr int32_t BACKEND_NOT_INITIALIZED_DETAIL = -1;

struct TransferResult {
  TransportCode code = TransportCode::IO_ERROR;
  TransferPhase phase = TransferPhase::NONE;
  int32_t detail = 0;
  size_t dataBytesTransferred = 0;
  bool currentWriteByteMayBeAccepted = false;
  bool firstDeviceAddressAcked = false;
  bool memoryAddressAcked = false;
  bool repeatedDeviceAddressAcked = false;
  bool stopCompleted = false;

  constexpr bool ok() const { return code == TransportCode::OK; }
};

struct WriteCycleResult {
  TransferResult frame{};
  TransferResult hold{};
  bool holdRequired = false;
  bool holdCompleted = false;
};

struct SingleWireTransfer {
  SpeedMode speed = SpeedMode::HIGH_SPEED;
  uint8_t deviceAddress = 0;
  bool hasMemoryAddress = false;
  uint8_t memoryAddress = 0;
  const uint8_t* txData = nullptr;
  size_t txLength = 0;
  bool hasRepeatedStart = false;
  uint8_t repeatedDeviceAddress = 0;
  uint8_t* rxData = nullptr;
  size_t rxLength = 0;
  uint32_t minimumPostTransferHighUs = 0;
};

// Bus-generated callback deadlines are finite values below UINT64_MAX;
// UINT64_MAX is reserved for Bus-internal permanent write-high poison.
using NowUsFn = uint64_t (*)(void* user);
using TransferFn = TransferResult (*)(const SingleWireTransfer& transfer,
                                     uint64_t deadlineUs,
                                     void* user);
using ResetDiscoverFn = TransferResult (*)(bool& present,
                                          uint64_t deadlineUs,
                                          void* user);
using WaitUntilUsFn = TransferResult (*)(uint64_t deadlineUs, void* user);
using ReadPresenceFn = TransferResult (*)(bool& present,
                                         uint64_t deadlineUs,
                                         void* user);

struct SingleWireTransport {
  void* user = nullptr;
  NowUsFn nowUs = nullptr;
  TransferFn transfer = nullptr;
  ResetDiscoverFn resetAndDiscover = nullptr;
  WaitUntilUsFn waitUntilUs = nullptr;
  ReadPresenceFn readPresence = nullptr;
};

}  // namespace AT21CS
