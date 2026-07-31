#pragma once

#include <cstddef>
#include <cstdint>

#include "AT21CS/Transport.h"

namespace AT21CS::test {

enum class FakeEventKind : uint8_t {
  NOW_US,
  PRESENCE,
  RESET_DISCOVER,
  TRANSFER_BEGIN,
  DEVICE_ADDRESS,
  MEMORY_ADDRESS,
  RESTART,
  TX_DATA,
  RX_DATA,
  STOP,
  WAIT_UNTIL,
  TRANSFER_END
};

struct FakeEvent {
  FakeEventKind kind = FakeEventKind::NOW_US;
  uint64_t atUs = 0;
  uint32_t value = 0;
  size_t index = 0;
};

struct TransferScript {
  TransferResult result{};
  uint8_t rxData[8] = {};
  size_t rxLength = 0;
};

struct BooleanScript {
  TransferResult result{};
  bool value = false;
};

struct WaitScript {
  TransferResult result{};
  bool advanceToDeadline = false;
};

struct CapturedTransfer {
  SpeedMode speed = SpeedMode::HIGH_SPEED;
  uint8_t deviceAddress = 0;
  bool hasMemoryAddress = false;
  uint8_t memoryAddress = 0;
  uint8_t txData[8] = {};
  size_t txLength = 0;
  bool hasRepeatedStart = false;
  uint8_t repeatedDeviceAddress = 0;
  size_t rxLength = 0;
  uint32_t minimumPostTransferHighUs = 0;
  uint64_t deadlineUs = 0;
};

class ScriptedTransport {
 public:
  static constexpr size_t EVENT_CAPACITY = 256;
  static constexpr size_t TRANSFER_CAPACITY = 64;
  static constexpr size_t AUX_CAPACITY = 32;
  static constexpr size_t NOW_CAPACITY = 128;
  static constexpr int32_t SCRIPT_ERROR_DETAIL = -900;

  SingleWireTransport descriptor(bool withPresence = true) {
    SingleWireTransport value{};
    value.user = this;
    value.nowUs = &_nowUs;
    value.transfer = &_transfer;
    value.resetAndDiscover = &_resetAndDiscover;
    value.waitUntilUs = &_waitUntilUs;
    value.readPresence = withPresence ? &_readPresence : nullptr;
    return value;
  }

  bool queueTransfer(const TransferScript& script) {
    if (transferWrite >= TRANSFER_CAPACITY) {
      overflow = true;
      return false;
    }
    transferScripts[transferWrite++] = script;
    return true;
  }

  bool queueReset(const BooleanScript& script) {
    if (resetWrite >= AUX_CAPACITY) {
      overflow = true;
      return false;
    }
    resetScripts[resetWrite++] = script;
    return true;
  }

  bool queuePresence(const BooleanScript& script) {
    if (presenceWrite >= AUX_CAPACITY) {
      overflow = true;
      return false;
    }
    presenceScripts[presenceWrite++] = script;
    return true;
  }

  bool queueWait(const WaitScript& script) {
    if (waitWrite >= AUX_CAPACITY) {
      overflow = true;
      return false;
    }
    waitScripts[waitWrite++] = script;
    return true;
  }

  bool queueNow(uint64_t value) {
    if (nowWrite >= NOW_CAPACITY) {
      overflow = true;
      return false;
    }
    if ((nowWrite != 0 && value < nowValues[nowWrite - 1]) ||
        (nowObserved && value < currentUs)) {
      overflow = true;
      return false;
    }
    nowValues[nowWrite++] = value;
    return true;
  }

  size_t eventCountFor(FakeEventKind kind) const {
    size_t count = 0;
    for (size_t index = 0; index < eventCount; ++index) {
      if (events[index].kind == kind) {
        ++count;
      }
    }
    return count;
  }

  FakeEvent events[EVENT_CAPACITY] = {};
  size_t eventCount = 0;
  CapturedTransfer captured[TRANSFER_CAPACITY] = {};
  size_t capturedCount = 0;
  TransferScript transferScripts[TRANSFER_CAPACITY] = {};
  size_t transferRead = 0;
  size_t transferWrite = 0;
  BooleanScript resetScripts[AUX_CAPACITY] = {};
  size_t resetRead = 0;
  size_t resetWrite = 0;
  BooleanScript presenceScripts[AUX_CAPACITY] = {};
  size_t presenceRead = 0;
  size_t presenceWrite = 0;
  WaitScript waitScripts[AUX_CAPACITY] = {};
  size_t waitRead = 0;
  size_t waitWrite = 0;
  uint64_t nowValues[NOW_CAPACITY] = {};
  size_t nowRead = 0;
  size_t nowWrite = 0;
  uint64_t currentUs = 1000;
  size_t transferCalls = 0;
  size_t resetCalls = 0;
  size_t waitCalls = 0;
  size_t presenceCalls = 0;
  uint64_t lastResetDeadlineUs = 0;
  uint64_t lastWaitDeadlineUs = 0;
  uint64_t lastPresenceDeadlineUs = 0;
  bool nowObserved = false;
  bool overflow = false;

 private:
  static TransferResult scriptError() {
    TransferResult result{};
    result.detail = SCRIPT_ERROR_DETAIL;
    return result;
  }

  bool record(FakeEventKind kind, uint32_t value = 0, size_t index = 0) {
    if (eventCount >= EVENT_CAPACITY) {
      overflow = true;
      return false;
    }
    events[eventCount++] = FakeEvent{kind, currentUs, value, index};
    return true;
  }

  static uint64_t _nowUs(void* user) {
    auto& self = *static_cast<ScriptedTransport*>(user);
    if (self.nowRead < self.nowWrite) {
      const uint64_t scriptedUs = self.nowValues[self.nowRead++];
      if (self.nowObserved && scriptedUs < self.currentUs) {
        self.overflow = true;
      } else {
        self.currentUs = scriptedUs;
      }
    }
    self.nowObserved = true;
    if (!self.record(FakeEventKind::NOW_US)) {
      return 0;
    }
    return self.currentUs;
  }

  static TransferResult _transfer(const SingleWireTransfer& transfer,
                                  uint64_t deadlineUs,
                                  void* user) {
    auto& self = *static_cast<ScriptedTransport*>(user);
    ++self.transferCalls;
    if (self.transferRead >= self.transferWrite ||
        self.capturedCount >= TRANSFER_CAPACITY) {
      self.overflow = true;
      return scriptError();
    }
    const TransferScript& script = self.transferScripts[self.transferRead++];

    CapturedTransfer& capture = self.captured[self.capturedCount++];
    capture.speed = transfer.speed;
    capture.deviceAddress = transfer.deviceAddress;
    capture.hasMemoryAddress = transfer.hasMemoryAddress;
    capture.memoryAddress = transfer.memoryAddress;
    capture.txLength = transfer.txLength;
    for (size_t index = 0; index < transfer.txLength && index < 8; ++index) {
      capture.txData[index] = transfer.txData[index];
    }
    capture.hasRepeatedStart = transfer.hasRepeatedStart;
    capture.repeatedDeviceAddress = transfer.repeatedDeviceAddress;
    capture.rxLength = transfer.rxLength;
    capture.minimumPostTransferHighUs = transfer.minimumPostTransferHighUs;
    capture.deadlineUs = deadlineUs;

    bool traceOk = self.record(FakeEventKind::TRANSFER_BEGIN);
    const bool firstAddressAttempted =
        script.result.phase != TransferPhase::NONE &&
        script.result.phase != TransferPhase::START;
    if (traceOk && firstAddressAttempted) {
      traceOk = self.record(FakeEventKind::DEVICE_ADDRESS,
                            transfer.deviceAddress);
    }
    const bool memoryAddressAttempted =
        transfer.hasMemoryAddress &&
        (script.result.firstDeviceAddressAcked ||
         script.result.phase == TransferPhase::MEMORY_ADDRESS);
    if (traceOk && memoryAddressAttempted) {
      traceOk = self.record(FakeEventKind::MEMORY_ADDRESS,
                            transfer.memoryAddress);
    }
    const bool repeatedAddressAttempted =
        transfer.hasRepeatedStart && script.result.memoryAddressAcked &&
        (script.result.phase == TransferPhase::RESTART ||
         script.result.phase == TransferPhase::DEVICE_ADDRESS_READ ||
         script.result.repeatedDeviceAddressAcked ||
         script.result.dataBytesTransferred != 0 ||
         script.result.phase == TransferPhase::DATA_READ ||
         script.result.phase == TransferPhase::STOP);
    if (traceOk && repeatedAddressAttempted) {
      traceOk = self.record(FakeEventKind::RESTART,
                            transfer.repeatedDeviceAddress);
    }
    size_t txEvents = script.result.dataBytesTransferred;
    if (script.result.currentWriteByteMayBeAccepted ||
        (script.result.code == TransportCode::NACK &&
         script.result.phase == TransferPhase::DATA_WRITE)) {
      ++txEvents;
    }
    if (txEvents > transfer.txLength) {
      txEvents = transfer.txLength;
    }
    for (size_t index = 0; traceOk && index < txEvents; ++index) {
      traceOk = self.record(FakeEventKind::TX_DATA,
                            transfer.txData[index], index);
    }
    size_t rxEvents = script.result.dataBytesTransferred;
    if (rxEvents > transfer.rxLength) {
      rxEvents = transfer.rxLength;
    }
    for (size_t index = 0; traceOk && index < rxEvents; ++index) {
      traceOk = self.record(FakeEventKind::RX_DATA, 0, index);
    }
    if (traceOk && script.result.stopCompleted) {
      traceOk = self.record(FakeEventKind::STOP);
    }
    if (traceOk) {
      traceOk = self.record(FakeEventKind::TRANSFER_END);
    }
    if (!traceOk) {
      return scriptError();
    }

    const size_t copyLength =
        script.rxLength < transfer.rxLength ? script.rxLength : transfer.rxLength;
    for (size_t index = 0; index < copyLength; ++index) {
      transfer.rxData[index] = script.rxData[index];
    }
    return script.result;
  }

  static TransferResult _resetAndDiscover(bool& present,
                                          uint64_t deadlineUs,
                                          void* user) {
    auto& self = *static_cast<ScriptedTransport*>(user);
    ++self.resetCalls;
    self.lastResetDeadlineUs = deadlineUs;
    if (!self.record(FakeEventKind::RESET_DISCOVER)) {
      return scriptError();
    }
    present = false;
    if (self.resetRead >= self.resetWrite) {
      self.overflow = true;
      return scriptError();
    }
    const BooleanScript& script = self.resetScripts[self.resetRead++];
    present = script.value;
    return script.result;
  }

  static TransferResult _waitUntilUs(uint64_t deadlineUs, void* user) {
    auto& self = *static_cast<ScriptedTransport*>(user);
    ++self.waitCalls;
    self.lastWaitDeadlineUs = deadlineUs;
    if (!self.record(FakeEventKind::WAIT_UNTIL,
                     static_cast<uint32_t>(deadlineUs & UINT32_MAX))) {
      return scriptError();
    }
    if (self.waitRead >= self.waitWrite) {
      self.overflow = true;
      return scriptError();
    }
    const WaitScript& script = self.waitScripts[self.waitRead++];
    if (script.advanceToDeadline && deadlineUs > self.currentUs) {
      self.currentUs = deadlineUs;
    }
    return script.result;
  }

  static TransferResult _readPresence(bool& present,
                                      uint64_t deadlineUs,
                                      void* user) {
    auto& self = *static_cast<ScriptedTransport*>(user);
    ++self.presenceCalls;
    self.lastPresenceDeadlineUs = deadlineUs;
    if (!self.record(FakeEventKind::PRESENCE)) {
      return scriptError();
    }
    present = false;
    if (self.presenceRead >= self.presenceWrite) {
      self.overflow = true;
      return scriptError();
    }
    const BooleanScript& script = self.presenceScripts[self.presenceRead++];
    present = script.value;
    return script.result;
  }
};

}  // namespace AT21CS::test
