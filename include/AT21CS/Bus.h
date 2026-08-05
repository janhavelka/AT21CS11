/// @file Bus.h
/// @brief Shared ownership of one physical AT21CS single-wire bus.
#pragma once

#include <cstddef>
#include <cstdint>

#include "AT21CS/Status.h"
#include "AT21CS/Transport.h"

namespace AT21CS {

class Driver;
#if defined(AT21CS_TESTING)
namespace test {
class TestAccess;
}
#endif

struct BusConfig {
  SingleWireTransport transport{};
};

struct BusSnapshot {
  bool bound = false;
  bool bindingEpochValid = true;
  uint64_t bindingEpoch = 0;
  uint64_t generation = 0;
  uint8_t claimedAddressMask = 0;
  bool resetEstablishedHighSpeed = false;
  /// 0 is inactive; UINT64_MAX is permanent post-write poison; all other
  /// values are finite deadlines.
  uint64_t writeHighUntilUs = 0;
  TransferResult previousTransfer{};
  TransferResult lastTransfer{};
  WriteCycleResult lastWriteCycle{};
};

class Bus {
 public:
  Bus() = default;
  ~Bus() = default;

  Bus(const Bus&) = delete;
  Bus& operator=(const Bus&) = delete;
  Bus(Bus&&) = delete;
  Bus& operator=(Bus&&) = delete;

  Status bind(const BusConfig& config);
  Status end();

  bool isBound() const;
  /// True only when the bound Backend provides the optional raw detect sample.
  bool hasPresenceIndicator() const;
  /// Takes one bounded logical Bus-wide detect sample without SI/O traffic.
  ///
  /// Returns `UNSUPPORTED_COMMAND` when detection is disabled. A callback
  /// error is returned as an error, never converted to logical absence.
  Status readPresenceIndicator(bool& present);
  uint64_t generation() const;
  BusSnapshot snapshot() const;

 private:
  friend class Driver;
#if defined(AT21CS_TESTING)
  friend class test::TestAccess;
#endif

  static constexpr size_t MAX_FRAME_DATA_BYTES = 8;
  static constexpr uint32_t TRANSFER_TIMEOUT_US = 9000;
  static constexpr uint32_t RESET_TIMEOUT_US = 5000;
  static constexpr uint32_t WRITE_HIGH_HOLD_US = 10000;
  static constexpr uint32_t HIGH_SPEED_HTSS_US = 160;
  static constexpr uint32_t STANDARD_SPEED_HTSS_US = 650;
  static constexpr uint32_t SPEED_CHANGE_HOLD_US = 650;

  Status _execute(const SingleWireTransfer& transfer, TransferResult& result);
  Status _executeWrite(const SingleWireTransfer& transfer,
                       WriteCycleResult& result);
  Status _resetAndDiscover(bool& present, TransferResult& result);
  Status _completeWriteHighHold(TransferResult& result);
  Status _readPresence(bool& present, TransferResult& result);
  Status _claimAddress(uint8_t addressBits);
  void _releaseAddress(uint8_t addressBits);
  Status _mapTransferFailure(const TransferResult& result) const;

  SingleWireTransport _transport{};
  bool _bound = false;
  bool _bindingEpochValid = true;
  uint64_t _bindingEpoch = 0;
  uint64_t _generation = 0;
  uint8_t _claimedAddressMask = 0;
  bool _resetEstablishedHighSpeed = false;
  uint64_t _writeHighUntilUs = 0;
  TransferResult _previousTransfer{};
  TransferResult _lastTransfer{};
  WriteCycleResult _lastWriteCycle{};
};

}  // namespace AT21CS
