#pragma once

#include <cstdint>

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

  static void activateWithoutHardware(Esp32Transport& transport,
                                      int presencePin = 2) {
    transport._config.sioPin = 1;
    transport._config.presencePin = presencePin;
    transport._config.presenceActiveHigh = true;
    transport._timingMux = 0;
    transport._initialized = true;
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

  static uint16_t startHighUs(SpeedMode speed) {
    return Esp32Transport::_timingFor(speed).startHighUs;
  }
};

}  // namespace AT21CS::test
