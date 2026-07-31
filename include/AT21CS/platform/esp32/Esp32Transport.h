/// @file Esp32Transport.h
/// @brief Externally owned ESP32 single-wire transport adapter.
#pragma once

#include <cstdint>

#include "AT21CS/Status.h"
#include "AT21CS/Transport.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

namespace AT21CS {

#if defined(AT21CS_TESTING)
namespace test {
class TestAccess;
}
#endif

struct Esp32TransportConfig {
  int sioPin = -1;
  int presencePin = -1;
  bool presenceActiveHigh = true;
};

class Esp32Transport {
 public:
  Esp32Transport() = default;
  ~Esp32Transport() = default;

  Esp32Transport(const Esp32Transport&) = delete;
  Esp32Transport& operator=(const Esp32Transport&) = delete;
  Esp32Transport(Esp32Transport&&) = delete;
  Esp32Transport& operator=(Esp32Transport&&) = delete;

  Status begin(const Esp32TransportConfig& config);
  void end();
  bool isInitialized() const;
  SingleWireTransport descriptor();

 private:
#if defined(AT21CS_TESTING)
  friend class test::TestAccess;
#endif

  // Structural defaults only; physical S2/S3 timing is not yet qualified.
  struct Timing {
    uint16_t bitUs = 12;
    uint16_t low0Us = 8;
    uint16_t low1Us = 1;
    uint16_t readLowUs = 1;
    uint16_t readSampleUs = 1;
    uint16_t startHighUs = 160;  // Active-speed pre-Start high interval.
  };

  static uint64_t _nowUsThunk(void* user);
  static TransferResult _transferThunk(const SingleWireTransfer& transfer,
                                       uint64_t deadlineUs,
                                       void* user);
  static TransferResult _resetAndDiscoverThunk(bool& present,
                                               uint64_t deadlineUs,
                                               void* user);
  static TransferResult _waitUntilUsThunk(uint64_t deadlineUs, void* user);
  static TransferResult _readPresenceThunk(bool& present,
                                           uint64_t deadlineUs,
                                           void* user);

  SingleWireTransport _descriptorUnchecked();
  TransferResult _transfer(const SingleWireTransfer& transfer,
                           uint64_t deadlineUs);
  TransferResult _resetAndDiscover(bool& present, uint64_t deadlineUs);
  TransferResult _waitUntilUs(uint64_t deadlineUs);
  TransferResult _readPresence(bool& present, uint64_t deadlineUs);
  uint64_t _nowUs();
  bool _deadlineReached(uint64_t deadlineUs);
  bool _delayWithinDeadline(uint32_t durationUs, uint64_t deadlineUs);
  bool _setLine(bool released);
  bool _readLine() const;
  void _delayUs(uint32_t durationUs) const;
  bool _writeBit(bool one,
                 const Timing& timing,
                 uint64_t deadlineUs,
                 bool* delivered = nullptr);
  bool _readBit(bool& one, const Timing& timing, uint64_t deadlineUs);
  bool _writeEightBits(uint8_t value,
                       const Timing& timing,
                       uint64_t deadlineUs,
                       bool& allBitsDelivered);
  bool _readEightBits(uint8_t& value,
                      const Timing& timing,
                      uint64_t deadlineUs);
  bool _readAck(bool& ack, const Timing& timing, uint64_t deadlineUs);
  TransportCode _finishStop(uint32_t highUs, uint64_t deadlineUs);
  static TransferResult _staleResult();
  static Timing _timingFor(SpeedMode speed);

  Esp32TransportConfig _config{};
  bool _initialized = false;
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  portMUX_TYPE _timingMux = portMUX_INITIALIZER_UNLOCKED;
#else
  mutable uint32_t _timingMux = 0;
#endif
};

}  // namespace AT21CS
