#include "AT21CS/Bus.h"

#include <limits>

#include "TransferValidation.h"

namespace AT21CS {
namespace {

constexpr bool checkedDeadlineAdd(uint64_t base,
                                  uint64_t increment,
                                  uint64_t& sum) {
  if (base >= (std::numeric_limits<uint64_t>::max() - increment)) {
    sum = 0;
    return false;
  }
  sum = base + increment;
  return true;
}

constexpr bool hasNoProtocolEvidence(const TransferResult& result) {
  return result.dataBytesTransferred == 0 &&
         !result.currentWriteByteMayBeAccepted &&
         !result.firstDeviceAddressAcked && !result.memoryAddressAcked &&
         !result.repeatedDeviceAddressAcked && !result.stopCompleted;
}

constexpr bool isKnownTransportCode(TransportCode code) {
  return code == TransportCode::OK || code == TransportCode::NACK ||
         code == TransportCode::TIMEOUT ||
         code == TransportCode::LINE_STUCK || code == TransportCode::IO_ERROR;
}

bool requiredAddressEvidencePresent(const SingleWireTransfer& transfer,
                                    const TransferResult& result) {
  return result.firstDeviceAddressAcked &&
         (!transfer.hasMemoryAddress || result.memoryAddressAcked) &&
         (!transfer.hasRepeatedStart || result.repeatedDeviceAddressAcked);
}

bool hasOnlyApplicableAddressEvidence(const SingleWireTransfer& transfer,
                                      const TransferResult& result) {
  return (transfer.hasMemoryAddress || !result.memoryAddressAcked) &&
         (transfer.hasRepeatedStart || !result.repeatedDeviceAddressAcked) &&
         (!result.memoryAddressAcked || result.firstDeviceAddressAcked) &&
         (!result.repeatedDeviceAddressAcked ||
          (result.firstDeviceAddressAcked && result.memoryAddressAcked));
}

TransferPhase firstAddressPhase(const SingleWireTransfer& transfer) {
  const bool isDirectRead = transfer.rxLength != 0 && !transfer.hasMemoryAddress;
  return isDirectRead || ((transfer.deviceAddress & 0x01u) != 0u)
             ? TransferPhase::DEVICE_ADDRESS_READ
             : TransferPhase::DEVICE_ADDRESS_WRITE;
}

bool validNackShape(const SingleWireTransfer& transfer,
                    const TransferResult& result) {
  if (result.currentWriteByteMayBeAccepted ||
      !hasOnlyApplicableAddressEvidence(transfer, result)) {
    return false;
  }

  if (result.phase == firstAddressPhase(transfer)) {
    return result.dataBytesTransferred == 0 &&
           !result.firstDeviceAddressAcked && !result.memoryAddressAcked &&
           !result.repeatedDeviceAddressAcked;
  }
  if (result.phase == TransferPhase::MEMORY_ADDRESS) {
    return transfer.hasMemoryAddress && result.dataBytesTransferred == 0 &&
           result.firstDeviceAddressAcked && !result.memoryAddressAcked &&
           !result.repeatedDeviceAddressAcked;
  }
  if (result.phase == TransferPhase::DEVICE_ADDRESS_READ) {
    return transfer.hasRepeatedStart && result.dataBytesTransferred == 0 &&
           result.firstDeviceAddressAcked && result.memoryAddressAcked &&
           !result.repeatedDeviceAddressAcked;
  }
  if (result.phase == TransferPhase::DATA_WRITE) {
    return transfer.txLength != 0 &&
           result.dataBytesTransferred < transfer.txLength &&
           requiredAddressEvidencePresent(transfer, result);
  }
  return false;
}

bool validFailureShape(const SingleWireTransfer& transfer,
                       const TransferResult& result) {
  const bool noAddressEvidence = !result.firstDeviceAddressAcked &&
                                 !result.memoryAddressAcked &&
                                 !result.repeatedDeviceAddressAcked;
  const bool firstOnly = result.firstDeviceAddressAcked &&
                         !result.memoryAddressAcked &&
                         !result.repeatedDeviceAddressAcked;
  const bool throughMemory = result.firstDeviceAddressAcked &&
                             result.memoryAddressAcked &&
                             !result.repeatedDeviceAddressAcked;
  const bool allRequired = requiredAddressEvidencePresent(transfer, result);

  switch (result.phase) {
    case TransferPhase::NONE:
    case TransferPhase::START:
      return noAddressEvidence && result.dataBytesTransferred == 0 &&
             !result.currentWriteByteMayBeAccepted;
    case TransferPhase::DEVICE_ADDRESS_WRITE:
      return firstAddressPhase(transfer) ==
                 TransferPhase::DEVICE_ADDRESS_WRITE &&
             noAddressEvidence && result.dataBytesTransferred == 0 &&
             !result.currentWriteByteMayBeAccepted;
    case TransferPhase::MEMORY_ADDRESS:
      return transfer.hasMemoryAddress && firstOnly &&
             result.dataBytesTransferred == 0 &&
             !result.currentWriteByteMayBeAccepted;
    case TransferPhase::RESTART:
      return transfer.hasRepeatedStart && throughMemory &&
             result.dataBytesTransferred == 0 &&
             !result.currentWriteByteMayBeAccepted;
    case TransferPhase::DEVICE_ADDRESS_READ:
      if (transfer.hasRepeatedStart) {
        return throughMemory && result.dataBytesTransferred == 0 &&
               !result.currentWriteByteMayBeAccepted;
      }
      return firstAddressPhase(transfer) == TransferPhase::DEVICE_ADDRESS_READ &&
             noAddressEvidence && result.dataBytesTransferred == 0 &&
             !result.currentWriteByteMayBeAccepted;
    case TransferPhase::DATA_WRITE:
      return transfer.txLength != 0 && allRequired &&
             result.dataBytesTransferred < transfer.txLength;
    case TransferPhase::DATA_READ:
      return transfer.rxLength != 0 && allRequired &&
             result.dataBytesTransferred <= transfer.rxLength &&
             !result.currentWriteByteMayBeAccepted;
    case TransferPhase::STOP: {
      const size_t payloadLength =
          transfer.txLength != 0 ? transfer.txLength : transfer.rxLength;
      return allRequired && result.dataBytesTransferred == payloadLength &&
             !result.currentWriteByteMayBeAccepted;
    }
    case TransferPhase::PRESENCE:
    case TransferPhase::RESET_LOW:
    case TransferPhase::RESET_RECOVERY:
    case TransferPhase::DISCOVERY_REQUEST:
    case TransferPhase::DISCOVERY_SAMPLE:
    case TransferPhase::DISCOVERY_RELEASE:
    case TransferPhase::WAIT_HIGH:
      return false;
  }
  return false;
}

bool validTransferResult(const SingleWireTransfer& transfer,
                         const TransferResult& result) {
  const size_t payloadLength =
      transfer.txLength != 0 ? transfer.txLength : transfer.rxLength;
  if (!isKnownTransportCode(result.code) ||
      result.dataBytesTransferred > payloadLength ||
      !hasOnlyApplicableAddressEvidence(transfer, result)) {
    return false;
  }

  if (result.code == TransportCode::OK) {
    return result.phase == TransferPhase::STOP && result.stopCompleted &&
           !result.currentWriteByteMayBeAccepted &&
           result.dataBytesTransferred == payloadLength &&
           requiredAddressEvidencePresent(transfer, result);
  }
  if (result.code == TransportCode::NACK) {
    return validNackShape(transfer, result);
  }

  if (result.currentWriteByteMayBeAccepted) {
    if (result.phase != TransferPhase::DATA_WRITE || transfer.txLength == 0 ||
        result.dataBytesTransferred >= transfer.txLength ||
        !requiredAddressEvidencePresent(transfer, result)) {
      return false;
    }
  }
  return validFailureShape(transfer, result);
}

bool validAuxiliaryFailurePhase(TransferPhase phase,
                                TransferPhase successPhase) {
  if (phase == TransferPhase::NONE || phase == successPhase) {
    return true;
  }
  if (successPhase != TransferPhase::DISCOVERY_RELEASE) {
    return false;
  }
  return phase == TransferPhase::RESET_LOW ||
         phase == TransferPhase::RESET_RECOVERY ||
         phase == TransferPhase::DISCOVERY_REQUEST ||
         phase == TransferPhase::DISCOVERY_SAMPLE;
}

bool validAuxiliaryResult(const TransferResult& result,
                          TransferPhase successPhase) {
  if (!isKnownTransportCode(result.code) ||
      result.code == TransportCode::NACK || !hasNoProtocolEvidence(result)) {
    return false;
  }
  return result.code == TransportCode::OK
             ? result.phase == successPhase
             : validAuxiliaryFailurePhase(result.phase, successPhase);
}

Status mapCode(const TransferResult& result) {
  switch (result.code) {
    case TransportCode::OK:
      return Status::Ok();
    case TransportCode::TIMEOUT:
      return Status::Error(Err::TRANSPORT_TIMEOUT, result.detail);
    case TransportCode::LINE_STUCK:
      return Status::Error(Err::LINE_STUCK, result.detail);
    case TransportCode::IO_ERROR:
      return Status::Error(Err::IO_ERROR, result.detail);
    case TransportCode::NACK:
      break;
  }
  return Status::Error(Err::IO_ERROR, result.detail);
}

Status waitForHighDeadline(const SingleWireTransport& transport,
                           uint64_t deadlineUs,
                           TransferResult& result) {
  result = transport.waitUntilUs(deadlineUs, transport.user);
  if (!validAuxiliaryResult(result, TransferPhase::WAIT_HIGH)) {
    return Status::Error(Err::IO_ERROR, result.detail);
  }
  const Status waitStatus = mapCode(result);
  if (!waitStatus.ok()) {
    return waitStatus;
  }
  if (transport.nowUs(transport.user) < deadlineUs) {
    return Status::Error(Err::CLOCK_STALLED);
  }
  return Status::Ok();
}

}  // namespace

Status Bus::bind(const BusConfig& config) {
  if (config.transport.nowUs == nullptr || config.transport.transfer == nullptr ||
      config.transport.resetAndDiscover == nullptr ||
      config.transport.waitUntilUs == nullptr) {
    return Status::Error(Err::INVALID_CONFIG);
  }
  if (_bound && _writeHighUntilUs != 0) {
    return Status::Error(Err::BUSY);
  }
  if (!_bindingEpochValid ||
      _bindingEpoch == std::numeric_limits<uint64_t>::max()) {
    return Status::Error(Err::INVALID_STATE);
  }

  ++_bindingEpoch;
  _transport = config.transport;
  _bound = true;
  _resetEstablishedHighSpeed = false;
  return Status::Ok();
}

Status Bus::end() {
  if (!_bound) {
    return Status::Ok();
  }
  if (_claimedAddressMask != 0) {
    return Status::Error(Err::BUSY, static_cast<int32_t>(_claimedAddressMask));
  }
  if (_writeHighUntilUs != 0) {
    TransferResult hold{};
    const Status status = _completeWriteHighHold(hold);
    if (!status.ok()) {
      return status;
    }
  }

  _transport = {};
  _bound = false;
  _standardSpeedAddressMask = 0;
  _resetEstablishedHighSpeed = false;
  if (_bindingEpoch == std::numeric_limits<uint64_t>::max()) {
    _bindingEpochValid = false;
  } else {
    ++_bindingEpoch;
  }
  return Status::Ok();
}

bool Bus::isBound() const {
  return _bound;
}

bool Bus::hasPresenceIndicator() const {
  return _bound && _transport.readPresence != nullptr;
}

Status Bus::readPresenceIndicator(bool& present) {
  TransferResult result{};
  return _readPresence(present, result);
}

uint64_t Bus::generation() const {
  return _generation;
}

BusSnapshot Bus::snapshot() const {
  BusSnapshot value{};
  value.bound = _bound;
  value.bindingEpochValid = _bindingEpochValid;
  value.bindingEpoch = _bindingEpoch;
  value.generation = _generation;
  value.claimedAddressMask = _claimedAddressMask;
  value.resetEstablishedHighSpeed = _resetEstablishedHighSpeed;
  value.writeHighUntilUs = _writeHighUntilUs;
  value.previousTransfer = _previousTransfer;
  value.lastTransfer = _lastTransfer;
  value.lastWriteCycle = _lastWriteCycle;
  return value;
}

Status Bus::_execute(const SingleWireTransfer& transfer, TransferResult& result) {
  result = {};
  if (!detail::validTransferRequest(transfer, MAX_FRAME_DATA_BYTES,
                                    HIGH_SPEED_HTSS_US,
                                    STANDARD_SPEED_HTSS_US) ||
      transfer.txLength != 0) {
    return Status::Error(Err::INVALID_PARAM);
  }
  if (!_bound) {
    return Status::Error(Err::NOT_BOUND);
  }
  if (_writeHighUntilUs != 0) {
    TransferResult hold{};
    const Status holdStatus = _completeWriteHighHold(hold);
    if (!holdStatus.ok()) {
      return holdStatus;
    }
  }

  const uint64_t nowUs = _transport.nowUs(_transport.user);
  const uint32_t transferTimeoutUs = _transferTimeoutUs(transfer.speed);
  uint64_t deadlineUs = 0;
  if (!checkedDeadlineAdd(nowUs, transferTimeoutUs, deadlineUs)) {
    return Status::Error(Err::CLOCK_STALLED);
  }

  result = _transport.transfer(transfer, deadlineUs, _transport.user);
  _previousTransfer = _lastTransfer;
  _lastTransfer = result;
  if (!validTransferResult(transfer, result)) {
    return Status::Error(Err::IO_ERROR, result.detail);
  }
  return _mapTransferFailure(result);
}

Status Bus::_executeWrite(const SingleWireTransfer& transfer,
                          WriteCycleResult& result) {
  result = {};
  if (!detail::validTransferRequest(transfer, MAX_FRAME_DATA_BYTES,
                                    HIGH_SPEED_HTSS_US,
                                    STANDARD_SPEED_HTSS_US) ||
      transfer.txLength == 0 ||
      transfer.rxLength != 0) {
    return Status::Error(Err::INVALID_PARAM);
  }
  if (!_bound) {
    return Status::Error(Err::NOT_BOUND);
  }
  if (_writeHighUntilUs != 0) {
    TransferResult retainedHold{};
    const Status holdStatus = _completeWriteHighHold(retainedHold);
    if (!holdStatus.ok()) {
      return holdStatus;
    }
  }

  const uint64_t nowUs = _transport.nowUs(_transport.user);
  const uint32_t transferTimeoutUs = _transferTimeoutUs(transfer.speed);
  uint64_t preflightEndUs = 0;
  const uint64_t requiredRangeUs =
      static_cast<uint64_t>(transferTimeoutUs) + WRITE_HIGH_HOLD_US;
  if (!checkedDeadlineAdd(nowUs, requiredRangeUs, preflightEndUs)) {
    return Status::Error(Err::CLOCK_STALLED);
  }
  (void)preflightEndUs;
  const uint64_t deadlineUs = nowUs + transferTimeoutUs;

  result.frame = _transport.transfer(transfer, deadlineUs, _transport.user);
  _previousTransfer = _lastTransfer;
  _lastTransfer = result.frame;

  const bool shapeValid = validTransferResult(transfer, result.frame);
  const Status frameStatus =
      shapeValid ? _mapTransferFailure(result.frame)
                 : Status::Error(Err::IO_ERROR, result.frame.detail);
  result.holdRequired = result.frame.dataBytesTransferred > 0 ||
                        (result.frame.currentWriteByteMayBeAccepted &&
                         result.frame.phase == TransferPhase::DATA_WRITE &&
                         result.frame.dataBytesTransferred < transfer.txLength);

  if (!result.holdRequired) {
    _lastWriteCycle = result;
    return frameStatus;
  }

  const uint64_t holdStartUs = _transport.nowUs(_transport.user);
  if (!checkedDeadlineAdd(holdStartUs, WRITE_HIGH_HOLD_US,
                          _writeHighUntilUs)) {
    _writeHighUntilUs = std::numeric_limits<uint64_t>::max();
    _lastWriteCycle = result;
    return Status::Error(Err::CLOCK_STALLED);
  }

  _lastWriteCycle = result;
  TransferResult hold{};
  const Status holdStatus = _completeWriteHighHold(hold);
  result = _lastWriteCycle;
  if (!frameStatus.ok()) {
    return frameStatus;
  }
  return holdStatus;
}

Status Bus::_resetAndDiscover(bool& present, TransferResult& result) {
  present = false;
  result = {};
  if (!_bound) {
    return Status::Error(Err::NOT_BOUND);
  }
  if (_generation == std::numeric_limits<uint64_t>::max()) {
    return Status::Error(Err::INVALID_STATE);
  }
  if (_writeHighUntilUs != 0) {
    TransferResult hold{};
    const Status holdStatus = _completeWriteHighHold(hold);
    if (!holdStatus.ok()) {
      return holdStatus;
    }
  }

  const uint64_t nowUs = _transport.nowUs(_transport.user);
  uint64_t deadlineUs = 0;
  if (!checkedDeadlineAdd(nowUs, RESET_TIMEOUT_US, deadlineUs)) {
    return Status::Error(Err::CLOCK_STALLED);
  }

  ++_generation;
  _resetEstablishedHighSpeed = false;
  result = _transport.resetAndDiscover(present, deadlineUs, _transport.user);
  _previousTransfer = _lastTransfer;
  _lastTransfer = result;
  if (!validAuxiliaryResult(result, TransferPhase::DISCOVERY_RELEASE)) {
    present = false;
    return Status::Error(Err::IO_ERROR, result.detail);
  }
  const Status status = mapCode(result);
  if (!status.ok()) {
    present = false;
    return status;
  }
  if (!present) {
    return Status::Error(Err::NOT_PRESENT);
  }
  _resetEstablishedHighSpeed = true;
  return Status::Ok();
}

Status Bus::_completeWriteHighHold(TransferResult& result) {
  result = {};
  if (_writeHighUntilUs == 0) {
    return Status::Ok();
  }
  if (!_bound) {
    return Status::Error(Err::NOT_BOUND);
  }
  if (_writeHighUntilUs == std::numeric_limits<uint64_t>::max()) {
    return Status::Error(Err::CLOCK_STALLED);
  }

  if (_transport.nowUs(_transport.user) >= _writeHighUntilUs) {
    result.code = TransportCode::OK;
    result.phase = TransferPhase::WAIT_HIGH;
    _lastWriteCycle.hold = result;
    _lastWriteCycle.holdCompleted = true;
    _writeHighUntilUs = 0;
    return Status::Ok();
  }

  const Status waitStatus =
      waitForHighDeadline(_transport, _writeHighUntilUs, result);
  _lastWriteCycle.hold = result;
  _lastWriteCycle.holdCompleted = false;
  if (!waitStatus.ok()) {
    return waitStatus;
  }

  _lastWriteCycle.holdCompleted = true;
  _writeHighUntilUs = 0;
  return Status::Ok();
}

Status Bus::_readPresence(bool& present, TransferResult& result) {
  present = false;
  result = {};
  if (!_bound) {
    return Status::Error(Err::NOT_BOUND);
  }
  if (_transport.readPresence == nullptr) {
    return Status::Error(Err::UNSUPPORTED_COMMAND);
  }

  const uint64_t nowUs = _transport.nowUs(_transport.user);
  uint64_t deadlineUs = 0;
  if (!checkedDeadlineAdd(nowUs, PRESENCE_TIMEOUT_US, deadlineUs)) {
    return Status::Error(Err::CLOCK_STALLED);
  }
  result = _transport.readPresence(present, deadlineUs, _transport.user);
  _previousTransfer = _lastTransfer;
  _lastTransfer = result;
  if (!validAuxiliaryResult(result, TransferPhase::PRESENCE)) {
    present = false;
    return Status::Error(Err::IO_ERROR, result.detail);
  }
  const Status status = mapCode(result);
  if (!status.ok()) {
    present = false;
  }
  return status;
}

Status Bus::_claimAddress(uint8_t addressBits,
                          bool standardSpeed,
                          uint8_t replacedAddressMask) {
  if (addressBits > 7u) {
    return Status::Error(Err::INVALID_PARAM, static_cast<int32_t>(addressBits));
  }
  if (!_bound || !_bindingEpochValid) {
    return Status::Error(Err::NOT_BOUND);
  }
  if (replacedAddressMask != 0u &&
      ((replacedAddressMask &
        static_cast<uint8_t>(replacedAddressMask - 1u)) != 0u ||
       (_claimedAddressMask & replacedAddressMask) == 0u)) {
    return Status::Error(Err::INVALID_STATE);
  }

  const uint8_t bit = static_cast<uint8_t>(1u << addressBits);
  if ((bit & replacedAddressMask) == 0u &&
      (_claimedAddressMask & bit) != 0u) {
    return Status::Error(Err::INVALID_CONFIG, static_cast<int32_t>(addressBits));
  }

  const uint8_t retainedClaims = static_cast<uint8_t>(
      _claimedAddressMask & static_cast<uint8_t>(~replacedAddressMask));
  const uint8_t retainedStandard = static_cast<uint8_t>(
      _standardSpeedAddressMask & static_cast<uint8_t>(~replacedAddressMask));
  if ((standardSpeed && retainedClaims != 0u) ||
      (!standardSpeed && retainedStandard != 0u)) {
    return Status::Error(Err::INVALID_CONFIG, static_cast<int32_t>(addressBits));
  }

  _claimedAddressMask = static_cast<uint8_t>(_claimedAddressMask | bit);
  if (standardSpeed) {
    _standardSpeedAddressMask =
        static_cast<uint8_t>(_standardSpeedAddressMask | bit);
  }
  return Status::Ok();
}

Status Bus::_reserveStandardSpeed(uint8_t addressBits) {
  if (addressBits > 7u) {
    return Status::Error(Err::INVALID_PARAM, static_cast<int32_t>(addressBits));
  }
  const uint8_t bit = static_cast<uint8_t>(1u << addressBits);
  if ((_claimedAddressMask & bit) == 0u) {
    return Status::Error(Err::INVALID_STATE);
  }
  if (_claimedAddressMask != bit) {
    return Status::Error(Err::UNSUPPORTED_COMMAND);
  }
  _standardSpeedAddressMask = bit;
  return Status::Ok();
}

void Bus::_releaseStandardSpeed(uint8_t addressBits) {
  if (addressBits <= 7u) {
    const uint8_t bit = static_cast<uint8_t>(1u << addressBits);
    _standardSpeedAddressMask = static_cast<uint8_t>(
        _standardSpeedAddressMask & static_cast<uint8_t>(~bit));
  }
}

void Bus::_releaseAddress(uint8_t addressBits) {
  if (addressBits <= 7u) {
    const uint8_t bit = static_cast<uint8_t>(1u << addressBits);
    _claimedAddressMask = static_cast<uint8_t>(_claimedAddressMask &
                                               static_cast<uint8_t>(~bit));
    _standardSpeedAddressMask = static_cast<uint8_t>(
        _standardSpeedAddressMask & static_cast<uint8_t>(~bit));
  }
}

Status Bus::_mapTransferFailure(const TransferResult& result) const {
  if (result.code != TransportCode::NACK) {
    return mapCode(result);
  }
  switch (result.phase) {
    case TransferPhase::DEVICE_ADDRESS_WRITE:
      return Status::Error(
          Err::NACK_DEVICE_ADDRESS,
          makeProtocolDetail(ProtocolPhase::DEVICE_ADDRESS_WRITE));
    case TransferPhase::DEVICE_ADDRESS_READ:
      return Status::Error(
          Err::NACK_DEVICE_ADDRESS,
          makeProtocolDetail(ProtocolPhase::DEVICE_ADDRESS_READ));
    case TransferPhase::MEMORY_ADDRESS:
      return Status::Error(Err::NACK_MEMORY_ADDRESS,
                           makeProtocolDetail(ProtocolPhase::MEMORY_ADDRESS));
    case TransferPhase::DATA_WRITE:
      return Status::Error(
          Err::NACK_DATA,
          makeProtocolDetail(
              ProtocolPhase::DATA_WRITE,
              static_cast<uint16_t>(result.dataBytesTransferred)));
    default:
      return Status::Error(Err::IO_ERROR, result.detail);
  }
}

}  // namespace AT21CS
