/// @file Esp32Transport.h
/// @brief Externally owned ESP32 single-wire transport adapter.
#pragma once

#include <cstdint>
#include <limits>

#include "AT21CS/Status.h"
#include "AT21CS/Transport.h"

#if !defined(ARDUINO_ARCH_ESP32) && !defined(AT21CS_TESTING)
#error "Esp32Transport is available only for Arduino-ESP32"
#endif

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#define AT21CS_ESP32_IRAM_ATTR IRAM_ATTR
#else
#define AT21CS_ESP32_IRAM_ATTR
#endif

namespace AT21CS {

#if defined(AT21CS_TESTING)
namespace test {
class TestAccess;
}
#endif

/// Pin configuration for one physical SI/O wire and its optional detect input.
struct Esp32TransportConfig {
  /// Required SI/O GPIO. The library has no board-specific default.
  int sioPin = -1;
  /// Optional raw Bus-wide detect GPIO; exactly -1 disables it.
  ///
  /// An enabled pin must be a valid input distinct from `sioPin`. The Backend
  /// disables internal pulls, so board hardware must provide a stable bias.
  int presencePin = -1;
  /// Logical detect polarity; ignored when `presencePin == -1`.
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

  struct Timing {
    uint32_t bitNs = 16000;
    uint32_t low0Ns = 10000;
    uint32_t low1Ns = 1500;
    uint32_t readLowNs = 1200;
    uint32_t readSampleFromFallNs = 1800;
    uint32_t startHighUs = 160;
  };

  struct SegmentClock {
    uint32_t startCycle = 0;
    uint32_t cyclesPerUs = 0;
    uint32_t deadlineCycles = 0;
    uint64_t startUs = 0;
  };

  struct ReadBitResult {
    bool completed = false;
    bool sampled = false;
    bool value = true;
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
  AT21CS_ESP32_IRAM_ATTR TransferResult _transfer(
      const SingleWireTransfer& transfer,
      uint64_t deadlineUs);
  AT21CS_ESP32_IRAM_ATTR TransferResult _resetAndDiscover(
      bool& present,
      uint64_t deadlineUs);
  TransferResult _waitUntilUs(uint64_t deadlineUs);
  TransferResult _readPresence(bool& present, uint64_t deadlineUs);
  AT21CS_ESP32_IRAM_ATTR uint64_t _nowUs();
  bool _deadlineReached(uint64_t deadlineUs);
  bool _delayWithinDeadline(uint32_t durationUs, uint64_t deadlineUs);
  AT21CS_ESP32_IRAM_ATTR bool _beginSegment(uint64_t deadlineUs,
                                            SegmentClock& clock);
  AT21CS_ESP32_IRAM_ATTR bool _spinFrom(
      uint32_t edgeCycle,
      uint32_t durationNs,
      const SegmentClock& clock) const;
  AT21CS_ESP32_IRAM_ATTR static uint32_t _cyclesForNs(
      uint32_t durationNs,
      uint32_t cyclesPerUs);
  AT21CS_ESP32_IRAM_ATTR static bool _segmentExpired(
      const SegmentClock& clock,
      uint32_t currentCycle);
  AT21CS_ESP32_IRAM_ATTR uint32_t _cycleCount() const;
  bool _acquireTimingLock();
  void _releaseTimingLock();
  AT21CS_ESP32_IRAM_ATTR void _enterCritical();
  AT21CS_ESP32_IRAM_ATTR void _exitCritical();
  void _suspendScheduler();
  void _resumeScheduler();
  AT21CS_ESP32_IRAM_ATTR void _setLine(bool released);
  AT21CS_ESP32_IRAM_ATTR bool _readLine() const;
  void _delayUs(uint32_t durationUs) const;
  AT21CS_ESP32_IRAM_ATTR bool _writeBit(
      bool one,
      const Timing& timing,
      const SegmentClock& clock,
      bool* delivered = nullptr);
  AT21CS_ESP32_IRAM_ATTR ReadBitResult _readBit(
      const Timing& timing,
      const SegmentClock& clock);
  AT21CS_ESP32_IRAM_ATTR bool _writeEightBits(
      uint8_t value,
      const Timing& timing,
      const SegmentClock& clock,
      bool& allBitsDelivered);
  AT21CS_ESP32_IRAM_ATTR bool _readEightBits(
      uint8_t& value,
      const Timing& timing,
      const SegmentClock& clock);
  AT21CS_ESP32_IRAM_ATTR ReadBitResult _readAck(
      const Timing& timing,
      const SegmentClock& clock);
  TransportCode _finishStop(uint32_t highUs, uint64_t deadlineUs);
  static TransferResult _staleResult();
  static Timing _timingFor(SpeedMode speed);
  static bool _pinNumbersInRange(const Esp32TransportConfig& config,
                                 int pinCount);

  static constexpr uint32_t FINAL_WAIT_POLL_LIMIT = 100000;
  static constexpr uint32_t FINAL_WAIT_CYCLE_GUARD_US = 2000;
  static constexpr uint32_t QUALIFIED_MIN_CPU_CYCLES_PER_US = 80;
  static constexpr uint32_t TIMING_SPIN_LIMIT = 10000;
  static constexpr int32_t FROZEN_CLOCK_DETAIL = -5;

  Esp32TransportConfig _config{};
  bool _initialized = false;
#if defined(ARDUINO_ARCH_ESP32)
  portMUX_TYPE _timingMux = portMUX_INITIALIZER_UNLOCKED;
  void* _pmLock = nullptr;
#else
  mutable uint32_t _timingMux = 0;
#endif
#if defined(AT21CS_TESTING) && !defined(ARDUINO_ARCH_ESP32)
  struct TestLineEvent {
    uint32_t cycle = 0;
    bool released = true;
  };
  static constexpr uint16_t TEST_LEVEL_CAPACITY = 128;
  static constexpr uint16_t TEST_EVENT_CAPACITY = 512;
  mutable uint64_t _testNowUs = 0;
  mutable uint32_t _testCycle = 0;
  mutable uint32_t _testNowCallCycles = 0;
  mutable bool _testClockFrozen = false;
  mutable bool _testLineReleased = true;
  mutable bool _testPresenceLevel = false;
  mutable bool _testLevels[TEST_LEVEL_CAPACITY] = {};
  mutable uint16_t _testLevelCount = 0;
  mutable uint16_t _testLevelRead = 0;
  mutable TestLineEvent _testEvents[TEST_EVENT_CAPACITY] = {};
  mutable uint16_t _testEventCount = 0;
  mutable uint32_t _testReadCycles[TEST_LEVEL_CAPACITY] = {};
  mutable uint16_t _testReadCount = 0;
  mutable uint32_t _testDelayAdvanceLimitUs =
      std::numeric_limits<uint32_t>::max();
  mutable bool _testFreezeAfterDelay = false;
  mutable bool _testTimingLockFailure = false;
  mutable uint16_t _testTimingLockDepth = 0;
  mutable uint16_t _testTimingLockAcquireCount = 0;
  mutable uint16_t _testTimingLockReleaseCount = 0;
  mutable bool _testOverflow = false;
#endif
};

}  // namespace AT21CS

#undef AT21CS_ESP32_IRAM_ATTR
