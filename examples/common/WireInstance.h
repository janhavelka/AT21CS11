#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AT21CS/AT21CS.h"
#include "AT21CS/platform/esp32/Esp32Transport.h"

namespace at21cs_example {

// Firmware integration contract: calls are synchronous and these objects are
// not thread-safe. The safe default is one firmware task/loop owning every wire
// instance and calling them sequentially; Drivers sharing one Bus always share
// that owner. Simultaneous calls from separate tasks are not qualified for this
// Backend. Application messages, queues, deadlines, retries, and backoff remain
// firmware policy. After attachment, firmware explicitly recovers and compares
// the serial. For mutable data, a page write is the bounded scheduling unit.
// Without a detect input, polling cannot observe replacement entirely between
// probes. This helper performs only one visible due operation per service call.

enum class AutomaticAction : uint8_t {
  NONE = 0,
  SAMPLE_PRESENCE,
  PROBE,
  RECOVER,
  READ_SERIAL
};

enum class IdentityChange : uint8_t {
  NONE = 0,
  FIRST_SEEN,
  SAME_DEVICE,
  DIFFERENT_DEVICE
};

struct IdentityObservation {
  IdentityChange change = IdentityChange::NONE;
  bool previousKnown = false;
  uint8_t previous[AT21CS::cmd::SECURITY_SERIAL_SIZE] = {};
  uint8_t current[AT21CS::cmd::SECURITY_SERIAL_SIZE] = {};
};

struct ServiceResult {
  AutomaticAction action = AutomaticAction::NONE;
  AT21CS::Status status{};
  bool performed = false;
  bool report = false;
  bool presenceKnown = false;
  bool presence = false;
  IdentityObservation identity{};
};

class HotPlugPolicy {
 public:
  void start(bool initialized) {
    _active = true;
    _automaticBlocked = false;
    _recoveryPending = !initialized;
    _serialPending = initialized;
    _rawKnown = false;
    _candidateKnown = false;
    _debouncedKnown = false;
    _sampleTime = {};
    _candidateTime = {};
    _pollTime = {};
    _presenceErrorKnown = false;
    _identityCurrent = false;
    _lastAction = AutomaticAction::NONE;
    _lastStatus = AT21CS::Status::Ok();
  }

  void stop() {
    _active = false;
    _serialPending = false;
  }

  // Call after a known suspension longer than 0x7fffffff ms. Identity and
  // recovery intent remain, while cadence and detect debounce restart safely.
  void resetTiming() {
    _sampleTime = {};
    _candidateTime = {};
    _pollTime = {};
    _rawKnown = false;
    _candidateKnown = false;
    _debouncedKnown = false;
    _presenceErrorKnown = false;
  }

  AutomaticAction nextAction(uint32_t nowMs,
                             bool detectEnabled,
                             bool bound,
                             bool initialized,
                             AT21CS::DriverState state,
                             uint32_t detectSampleMs,
                             uint32_t hotPlugPollMs) const {
    if (!_active || !bound || state == AT21CS::DriverState::FAULT ||
        _automaticBlocked) {
      return AutomaticAction::NONE;
    }
    if (_serialPending) {
      return AutomaticAction::READ_SERIAL;
    }

    const bool needsRecovery =
        _recoveryPending || !initialized || state == AT21CS::DriverState::OFFLINE;
    if (detectEnabled) {
      if (needsRecovery && _debouncedKnown && _debouncedPresent &&
          _pollTime.due(nowMs, hotPlugPollMs)) {
        return AutomaticAction::RECOVER;
      }
      if (_sampleTime.due(nowMs, detectSampleMs)) {
        return AutomaticAction::SAMPLE_PRESENCE;
      }
      return AutomaticAction::NONE;
    }

    if (!_pollTime.due(nowMs, hotPlugPollMs)) {
      return AutomaticAction::NONE;
    }
    if (!initialized || state == AT21CS::DriverState::OFFLINE) {
      return AutomaticAction::RECOVER;
    }
    if (state == AT21CS::DriverState::READY ||
        state == AT21CS::DriverState::DEGRADED) {
      return AutomaticAction::PROBE;
    }
    return AutomaticAction::NONE;
  }

  bool notePresenceSample(uint32_t nowMs,
                          const AT21CS::Status& status,
                          bool present,
                          uint32_t debounceMs) {
    _sampleTime.mark(nowMs);
    _lastAction = AutomaticAction::SAMPLE_PRESENCE;
    _lastStatus = status;

    if (!status.ok()) {
      const bool report =
          !_presenceErrorKnown || !sameStatus(_lastPresenceError, status);
      _presenceErrorKnown = true;
      _lastPresenceError = status;
      _rawKnown = false;
      _candidateKnown = false;
      _debouncedKnown = false;
      _candidateTime = {};
      return report;
    }

    _presenceErrorKnown = false;
    _rawKnown = true;
    _rawPresent = present;
    if (!_candidateKnown || _candidatePresent != present) {
      _candidateKnown = true;
      _candidatePresent = present;
      _candidateTime.mark(nowMs);
      return false;
    }
    if (!_candidateTime.due(nowMs, debounceMs)) {
      return false;
    }
    if (_debouncedKnown && _debouncedPresent == present) {
      return false;
    }

    const bool hadDebouncedValue = _debouncedKnown;
    const bool wasPresent = _debouncedPresent;
    _debouncedKnown = true;
    _debouncedPresent = present;
    if (!present) {
      _recoveryPending = true;
      _identityCurrent = false;
    } else if ((hadDebouncedValue && !wasPresent) ||
               (!hadDebouncedValue && _recoveryPending)) {
      _pollTime = {};
    }
    return true;
  }

  void noteProbe(uint32_t nowMs,
                 const AT21CS::Status& status,
                 AT21CS::DriverState stateAfter) {
    _pollTime.mark(nowMs);
    _lastAction = AutomaticAction::PROBE;
    _lastStatus = status;
    if (!status.ok()) {
      _identityCurrent = false;
    }
    if (terminal(status, stateAfter)) {
      _automaticBlocked = true;
    }
  }

  void noteRecovery(uint32_t nowMs,
                    const AT21CS::Status& status,
                    AT21CS::DriverState stateAfter,
                    bool manual) {
    _pollTime.mark(nowMs);
    _lastAction = AutomaticAction::RECOVER;
    _lastStatus = status;
    // Recovery may reset or replace the addressed device. Preserve the last
    // known bytes for comparison, but do not call them current until the
    // separately scheduled serial read succeeds.
    _identityCurrent = false;
    if (status.ok()) {
      _automaticBlocked = false;
      _recoveryPending = false;
      _serialPending = true;
      return;
    }
    _recoveryPending = true;
    if (!manual && terminal(status, stateAfter)) {
      _automaticBlocked = true;
    }
  }

  IdentityObservation noteSerial(const AT21CS::Status& status,
                                 const AT21CS::SerialNumberInfo& serial) {
    IdentityObservation observation{};
    _serialPending = false;
    _lastAction = AutomaticAction::READ_SERIAL;
    _lastStatus = status;
    if (!status.ok()) {
      _identityCurrent = false;
      return observation;
    }

    observation.previousKnown = _identityKnown;
    if (_identityKnown) {
      std::memcpy(observation.previous, _identity,
                  AT21CS::cmd::SECURITY_SERIAL_SIZE);
    }
    std::memcpy(observation.current, serial.bytes,
                AT21CS::cmd::SECURITY_SERIAL_SIZE);
    if (!_identityKnown) {
      observation.change = IdentityChange::FIRST_SEEN;
    } else if (std::memcmp(_identity, serial.bytes,
                           AT21CS::cmd::SECURITY_SERIAL_SIZE) == 0) {
      observation.change = IdentityChange::SAME_DEVICE;
    } else {
      observation.change = IdentityChange::DIFFERENT_DEVICE;
    }
    std::memcpy(_identity, serial.bytes, AT21CS::cmd::SECURITY_SERIAL_SIZE);
    _identityKnown = true;
    _identityCurrent = true;
    return observation;
  }

  bool automaticBlocked() const { return _automaticBlocked; }
  bool recoveryPending() const { return _recoveryPending; }
  bool rawKnown() const { return _rawKnown; }
  bool debouncedKnown() const { return _debouncedKnown; }
  bool debouncedPresent() const { return _debouncedPresent; }
  bool identityKnown() const { return _identityKnown; }
  bool identityCurrent() const { return _identityCurrent; }
  const uint8_t* identity() const { return _identity; }
  AutomaticAction lastAction() const { return _lastAction; }
  AT21CS::Status lastStatus() const { return _lastStatus; }

 private:
  struct Cadence {
    bool valid = false;
    uint32_t sinceMs = 0;

    bool due(uint32_t nowMs, uint32_t intervalMs) const {
      return !valid ||
             static_cast<uint32_t>(nowMs - sinceMs) >= intervalMs;
    }

    void mark(uint32_t nowMs) {
      valid = true;
      sinceMs = nowMs;
    }
  };

  static bool sameStatus(const AT21CS::Status& left,
                         const AT21CS::Status& right) {
    return left.code == right.code && left.detail == right.detail;
  }

  static bool terminal(const AT21CS::Status& status,
                       AT21CS::DriverState state) {
    return status.code == AT21CS::Err::NOT_BOUND ||
           status.code == AT21CS::Err::INVALID_STATE ||
           state == AT21CS::DriverState::FAULT;
  }

  bool _active = false;
  bool _rawKnown = false;
  bool _rawPresent = false;
  bool _candidateKnown = false;
  bool _candidatePresent = false;
  bool _debouncedKnown = false;
  bool _debouncedPresent = false;
  bool _recoveryPending = false;
  bool _serialPending = false;
  bool _automaticBlocked = false;
  Cadence _sampleTime{};
  Cadence _candidateTime{};
  Cadence _pollTime{};
  bool _presenceErrorKnown = false;
  AT21CS::Status _lastPresenceError{};
  uint8_t _identity[AT21CS::cmd::SECURITY_SERIAL_SIZE] = {};
  bool _identityKnown = false;
  bool _identityCurrent = false;
  AutomaticAction _lastAction = AutomaticAction::NONE;
  AT21CS::Status _lastStatus{};
};

enum class WorkChoice : uint8_t { NONE = 0, COMMAND, AUTOMATIC };

class WorkArbiter {
 public:
  WorkChoice choose(bool commandReady, bool automaticDue) const {
    if (commandReady && automaticDue) {
      return _automaticPriority ? WorkChoice::AUTOMATIC : WorkChoice::COMMAND;
    }
    if (commandReady) {
      return WorkChoice::COMMAND;
    }
    if (automaticDue) {
      return WorkChoice::AUTOMATIC;
    }
    return WorkChoice::NONE;
  }

  void completed(WorkChoice choice) {
    if (choice == WorkChoice::COMMAND) {
      _automaticPriority = true;
    } else if (choice == WorkChoice::AUTOMATIC) {
      _automaticPriority = false;
    }
  }

 private:
  bool _automaticPriority = false;
};

enum class WireChoice : uint8_t { NONE = 0, A, B };

class WireArbiter {
 public:
  WireChoice choose(bool aDue, bool bDue) const {
    if (aDue && bDue) {
      return _preferA ? WireChoice::A : WireChoice::B;
    }
    if (aDue) {
      return WireChoice::A;
    }
    if (bDue) {
      return WireChoice::B;
    }
    return WireChoice::NONE;
  }

  void completed(WireChoice choice) {
    if (choice == WireChoice::A) {
      _preferA = false;
    } else if (choice == WireChoice::B) {
      _preferA = true;
    }
  }

 private:
  bool _preferA = true;
};

class WireInstance {
 public:
  AT21CS::Status start(const AT21CS::Esp32TransportConfig& transportConfig,
                       const AT21CS::Config& driverConfig) {
    if (_started || backend.isInitialized() || bus.isBound() ||
        driver.isBound()) {
      return AT21CS::Status::Error(AT21CS::Err::INVALID_STATE);
    }
    _shutdownRequested = false;

    AT21CS::Status status = backend.begin(transportConfig);
    if (!status.ok()) {
      return status;
    }
    AT21CS::BusConfig busConfig{};
    busConfig.transport = backend.descriptor();
    status = bus.bind(busConfig);
    if (!status.ok()) {
      backend.end();
      return status;
    }

    _started = true;
    status = driver.begin(bus, driverConfig);
    if (!driver.isBound()) {
      _policy.stop();
      const AT21CS::Status endStatus = bus.end();
      if (!endStatus.ok()) {
        _shutdownRequested = true;
        return endStatus;
      }
      backend.end();
      _started = false;
      return status;
    }
    _policy.start(status.ok());
    return status;
  }

  AT21CS::Status shutdown() {
    _shutdownRequested = true;
    _policy.stop();
    if (_backendEndPending) {
      return AT21CS::Status::Ok();
    }
    driver.end();
    const AT21CS::Status status = bus.end();
    if (!status.ok()) {
      return status;
    }
    _backendEndPending = true;
    return AT21CS::Status::Ok();
  }

  bool shutdownCompletionDue() const { return _backendEndPending; }

  bool completeShutdown() {
    if (!_backendEndPending) {
      return false;
    }
    backend.end();
    _backendEndPending = false;
    _started = false;
    return true;
  }

  bool active() const {
    return _started && !_shutdownRequested && bus.isBound();
  }

  bool automaticDue(uint32_t nowMs,
                    uint32_t detectSampleMs,
                    uint32_t hotPlugPollMs) const {
    return automaticAction(nowMs, detectSampleMs, hotPlugPollMs) !=
           AutomaticAction::NONE;
  }

  ServiceResult serviceHotPlug(uint32_t nowMs,
                               uint32_t detectSampleMs,
                               uint32_t debounceMs,
                               uint32_t hotPlugPollMs) {
    ServiceResult result{};
    result.action = automaticAction(nowMs, detectSampleMs, hotPlugPollMs);
    if (result.action == AutomaticAction::NONE) {
      return result;
    }

    result.performed = true;
    switch (result.action) {
      case AutomaticAction::SAMPLE_PRESENCE: {
        bool present = false;
        result.status = bus.readPresenceIndicator(present);
        result.report = _policy.notePresenceSample(
            nowMs, result.status, present, debounceMs);
        result.presenceKnown = result.status.ok();
        result.presence = present;
        break;
      }
      case AutomaticAction::PROBE:
        result.status = driver.probe();
        _policy.noteProbe(nowMs, result.status, driver.state());
        result.report = true;
        break;
      case AutomaticAction::RECOVER:
        result.status = driver.recover();
        _policy.noteRecovery(nowMs, result.status, driver.state(), false);
        result.report = true;
        break;
      case AutomaticAction::READ_SERIAL:
        result = readSerial();
        break;
      case AutomaticAction::NONE:
        break;
    }
    return result;
  }

  ServiceResult samplePresence(uint32_t nowMs, uint32_t debounceMs) {
    ServiceResult result{};
    result.action = AutomaticAction::SAMPLE_PRESENCE;
    result.performed = true;
    bool present = false;
    result.status = bus.readPresenceIndicator(present);
    (void)_policy.notePresenceSample(nowMs, result.status, present, debounceMs);
    result.report = true;
    result.presenceKnown = result.status.ok();
    result.presence = present;
    return result;
  }

  ServiceResult recover(uint32_t nowMs) {
    ServiceResult result{};
    result.action = AutomaticAction::RECOVER;
    result.performed = true;
    result.report = true;
    result.status = driver.recover();
    _policy.noteRecovery(nowMs, result.status, driver.state(), true);
    return result;
  }

  ServiceResult readSerial() {
    ServiceResult result{};
    result.action = AutomaticAction::READ_SERIAL;
    result.performed = true;
    result.report = true;
    AT21CS::SerialNumberInfo serial{};
    result.status = driver.readSerialNumber(serial);
    result.identity = _policy.noteSerial(result.status, serial);
    return result;
  }

  HotPlugPolicy& policy() { return _policy; }
  const HotPlugPolicy& policy() const { return _policy; }

  AT21CS::Esp32Transport backend;
  AT21CS::Bus bus;
  AT21CS::Driver driver;

 private:
  AutomaticAction automaticAction(uint32_t nowMs,
                                  uint32_t detectSampleMs,
                                  uint32_t hotPlugPollMs) const {
    return _policy.nextAction(nowMs, bus.hasPresenceIndicator(),
                              driver.isBound(), driver.isInitialized(),
                              driver.state(), detectSampleMs, hotPlugPollMs);
  }

  HotPlugPolicy _policy{};
  bool _started = false;
  bool _shutdownRequested = false;
  bool _backendEndPending = false;
};

}  // namespace at21cs_example
