#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "AT21CS/AT21CS.h"
#include "AT21CS/platform/esp32/Esp32Transport.h"

namespace AT21CS::test {

class TestAccess {
 public:
  static Status execute(Bus& bus,
                        const SingleWireTransfer& transfer,
                        TransferResult& result) {
    return bus._execute(transfer, result);
  }

  static Status executeWrite(Bus& bus,
                             const SingleWireTransfer& transfer,
                             WriteCycleResult& result) {
    return bus._executeWrite(transfer, result);
  }

  static Status resetAndDiscover(Bus& bus,
                                 bool& present,
                                 TransferResult& result) {
    return bus._resetAndDiscover(present, result);
  }

  static void seedBindingEpoch(Bus& bus, uint64_t value, bool valid = true) {
    bus._bindingEpoch = value;
    bus._bindingEpochValid = valid;
  }

  static void seedGeneration(Bus& bus, uint64_t value) {
    bus._generation = value;
  }

  static bool hasCurrentBusBinding(const Driver& driver) {
    return driver._hasCurrentBusBinding();
  }

  static void seedDriverState(Driver& driver,
                              DriverState state,
                              bool initialized) {
    driver._setState(state, initialized);
  }

  static void seedDriverHealth(Driver& driver,
                               uint8_t consecutiveFailures,
                               uint32_t totalSuccess,
                               uint32_t totalFailures) {
    driver._consecutiveFailures = consecutiveFailures;
    driver._totalSuccess = totalSuccess;
    driver._totalFailures = totalFailures;
  }

  static void activateWithoutHardware(Esp32Transport& transport,
                                      int presencePin = 2,
                                      int sioPin = 1) {
    transport._config.sioPin = sioPin;
    transport._config.presencePin = presencePin;
    transport._config.presenceActiveHigh = true;
    transport._timingMux = 0;
    transport._testNowUs = 0;
    transport._testCycle = 0;
    transport._testNowCallCycles = 0;
    transport._testClockFrozen = false;
    transport._testLineReleased = true;
    transport._testPresenceLevel = false;
    transport._testLevelCount = 0;
    transport._testLevelRead = 0;
    transport._testEventCount = 0;
    transport._testReadCount = 0;
    transport._testDelayAdvanceLimitUs =
        std::numeric_limits<uint32_t>::max();
    transport._testFreezeAfterDelay = false;
    transport._testTimingLockFailure = false;
    transport._testTimingLockDepth = 0;
    transport._testTimingLockAcquireCount = 0;
    transport._testTimingLockReleaseCount = 0;
    transport._testOverflow = false;
    transport._initialized = true;
  }

  static bool pinNumbersInRange(const Esp32TransportConfig& config,
                                int pinCount) {
    return Esp32Transport::_pinNumbersInRange(config, pinCount);
  }

  static int sioPin(const Esp32Transport& transport) {
    return transport._config.sioPin;
  }

  static void setNowUs(Esp32Transport& transport, uint64_t nowUs) {
    transport._testNowUs = nowUs;
  }

  static uint64_t nowUs(const Esp32Transport& transport) {
    return transport._testNowUs;
  }

  static uint32_t cycle(const Esp32Transport& transport) {
    return transport._testCycle;
  }

  static void setNowCallCycles(Esp32Transport& transport,
                               uint32_t cycles) {
    transport._testNowCallCycles = cycles;
  }

  static void setPresenceLevel(Esp32Transport& transport, bool high) {
    transport._testPresenceLevel = high;
  }

  static bool beginSegment(Esp32Transport& transport,
                           uint64_t deadlineUs) {
    Esp32Transport::SegmentClock clock{};
    return transport._beginSegment(deadlineUs, clock);
  }

  static void freezeClock(Esp32Transport& transport, bool frozen) {
    transport._testClockFrozen = frozen;
  }

  static void freezeAfterCoarseDelay(Esp32Transport& transport,
                                     uint32_t advanceUs) {
    transport._testDelayAdvanceLimitUs = advanceUs;
    transport._testFreezeAfterDelay = true;
  }

  static void failTimingLock(Esp32Transport& transport, bool fail) {
    transport._testTimingLockFailure = fail;
  }

  static uint16_t timingLockDepth(const Esp32Transport& transport) {
    return transport._testTimingLockDepth;
  }

  static uint16_t timingLockAcquireCount(
      const Esp32Transport& transport) {
    return transport._testTimingLockAcquireCount;
  }

  static uint16_t timingLockReleaseCount(
      const Esp32Transport& transport) {
    return transport._testTimingLockReleaseCount;
  }

  static bool queueLineLevel(Esp32Transport& transport, bool high) {
    if (transport._testLevelCount >= transport.TEST_LEVEL_CAPACITY) {
      return false;
    }
    transport._testLevels[transport._testLevelCount++] = high;
    return true;
  }

  static uint16_t lineEventCount(const Esp32Transport& transport) {
    return transport._testEventCount;
  }

  static uint32_t lineEventCycle(const Esp32Transport& transport,
                                 uint16_t index) {
    return transport._testEvents[index].cycle;
  }

  static bool lineEventReleased(const Esp32Transport& transport,
                                uint16_t index) {
    return transport._testEvents[index].released;
  }

  static uint16_t readCount(const Esp32Transport& transport) {
    return transport._testReadCount;
  }

  static uint32_t readCycle(const Esp32Transport& transport,
                            uint16_t index) {
    return transport._testReadCycles[index];
  }

  static bool lineReleased(const Esp32Transport& transport) {
    return transport._testLineReleased;
  }

  static bool testOverflow(const Esp32Transport& transport) {
    return transport._testOverflow;
  }

  static TransferResult transfer(Esp32Transport& transport,
                                 const SingleWireTransfer& transfer,
                                 uint64_t deadlineUs) {
    return transport._transfer(transfer, deadlineUs);
  }

  static TransferResult resetAndDiscover(Esp32Transport& transport,
                                         bool& present,
                                         uint64_t deadlineUs) {
    return transport._resetAndDiscover(present, deadlineUs);
  }

  static TransferResult waitUntilUs(Esp32Transport& transport,
                                    uint64_t deadlineUs) {
    return transport._waitUntilUs(deadlineUs);
  }

  static uint32_t platformAccessCount(const Esp32Transport& transport) {
    return transport._timingMux;
  }

  static void resetPlatformAccessCount(Esp32Transport& transport) {
    transport._timingMux = 0;
  }

  static bool delayWithinDeadline(Esp32Transport& transport,
                                  uint32_t durationUs,
                                  uint64_t deadlineUs) {
    return transport._delayWithinDeadline(durationUs, deadlineUs);
  }

  static TransportCode finishStop(Esp32Transport& transport,
                                  uint32_t highUs,
                                  uint64_t deadlineUs) {
    return transport._finishStop(highUs, deadlineUs);
  }

  static uint32_t startHighUs(SpeedMode speed) {
    return Esp32Transport::_timingFor(speed).startHighUs;
  }

  static uint32_t bitNs(SpeedMode speed) {
    return Esp32Transport::_timingFor(speed).bitNs;
  }

  static uint32_t low0Ns(SpeedMode speed) {
    return Esp32Transport::_timingFor(speed).low0Ns;
  }

  static uint32_t low1Ns(SpeedMode speed) {
    return Esp32Transport::_timingFor(speed).low1Ns;
  }

  static uint32_t readLowNs(SpeedMode speed) {
    return Esp32Transport::_timingFor(speed).readLowNs;
  }

  static uint32_t readSampleNs(SpeedMode speed) {
    return Esp32Transport::_timingFor(speed).readSampleFromFallNs;
  }

  static uint32_t cyclesForNs(uint32_t durationNs, uint32_t cyclesPerUs) {
    return Esp32Transport::_cyclesForNs(durationNs, cyclesPerUs);
  }

  static uint32_t finalWaitPollLimit() {
    return Esp32Transport::FINAL_WAIT_POLL_LIMIT;
  }

  static uint32_t finalWaitGuardUs() {
    return Esp32Transport::FINAL_WAIT_CYCLE_GUARD_US;
  }

  static int32_t frozenClockDetail() {
    return Esp32Transport::FROZEN_CLOCK_DETAIL;
  }
};

}  // namespace AT21CS::test
