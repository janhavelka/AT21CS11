#pragma once

#include <cstddef>
#include <cstdint>

#include "AT21CS/Transport.h"

namespace AT21CS::detail {

inline bool validTransferRequest(const SingleWireTransfer& transfer,
                                 size_t maxFrameDataBytes,
                                 uint32_t highSpeedHtssUs,
                                 uint32_t standardSpeedHtssUs) {
  if ((transfer.speed != SpeedMode::HIGH_SPEED &&
       transfer.speed != SpeedMode::STANDARD_SPEED) ||
      transfer.txLength > maxFrameDataBytes ||
      transfer.rxLength > maxFrameDataBytes ||
      (transfer.txLength != 0 && transfer.rxLength != 0) ||
      ((transfer.txLength == 0) != (transfer.txData == nullptr)) ||
      ((transfer.rxLength == 0) != (transfer.rxData == nullptr))) {
    return false;
  }

  const uint32_t requiredHighUs =
      transfer.speed == SpeedMode::HIGH_SPEED ? highSpeedHtssUs
                                               : standardSpeedHtssUs;
  if (transfer.minimumPostTransferHighUs < requiredHighUs ||
      (!transfer.hasMemoryAddress && transfer.memoryAddress != 0) ||
      (!transfer.hasRepeatedStart && transfer.repeatedDeviceAddress != 0)) {
    return false;
  }

  // Every frame without a read payload is a write/address-only frame. A
  // memory-address phase also always starts with the raw write address.
  if ((transfer.rxLength == 0 || transfer.hasMemoryAddress) &&
      (transfer.deviceAddress & 0x01u) != 0u) {
    return false;
  }
  if (transfer.hasRepeatedStart &&
      (!transfer.hasMemoryAddress || transfer.rxLength == 0 ||
       transfer.txLength != 0 ||
       transfer.repeatedDeviceAddress !=
           static_cast<uint8_t>(transfer.deviceAddress | 0x01u))) {
    return false;
  }
  if (transfer.rxLength != 0 && transfer.hasMemoryAddress &&
      !transfer.hasRepeatedStart) {
    return false;
  }
  if (transfer.rxLength != 0 && !transfer.hasMemoryAddress &&
      (transfer.deviceAddress & 0x01u) == 0u) {
    return false;
  }
  return true;
}

}  // namespace AT21CS::detail
