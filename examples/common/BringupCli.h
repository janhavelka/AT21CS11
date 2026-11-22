#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "BoardConfig.h"
#include "BoundedCli.h"
#include "CliOutput.h"
#include "CommandContract.h"
#include "StatusText.h"
#include "WireInstance.h"

namespace at21cs_example {

// The library is synchronous and its objects are not thread-safe. In an RTOS
// firmware, the safe default is one firmware task owning every wire instance
// and calling them sequentially. Drivers sharing one Bus always share that
// owner. Application tasks may send copied application-defined messages to it,
// but the library defines no task, queue, mailbox, request ID, deadline, result
// dispatcher, retry/backoff, automatic recovery, or attachment generation.
// This loop's fixed cadence requests one visible attempt at a time. Firmware
// calls recover() after attachment and may compare the serial before reusing
// its own associated data. Multiple simultaneous owner tasks on separate wires
// remain outside the current ESP32 timing qualification. For bounded firmware
// scheduling, writeEepromPage() is the preferred unit for an EEPROM write.

class BringupCli {
 public:
  BringupCli(WireInstance& wire, CliOutput& output,
             uint32_t (*nowMs)(), int (*readChar)())
      : _wire(wire), _output(output), _nowMs(nowMs), _readChar(readChar) {}

  // Pending arguments point into this engine's input buffer.
  BringupCli(const BringupCli&) = delete;
  BringupCli& operator=(const BringupCli&) = delete;

  void start() {
    AT21CS::Esp32TransportConfig transportConfig{};
    transportConfig.sioPin = board::SIO_PRIMARY;
    transportConfig.presencePin = board::PRESENCE_PRIMARY;
    transportConfig.presenceActiveHigh = board::PRESENCE_PRIMARY_ACTIVE_HIGH;

    AT21CS::Config driverConfig{};
    driverConfig.addressBits = board::ADDRESS_BITS_PRIMARY;
    driverConfig.offlineThreshold = board::OFFLINE_THRESHOLD_PRIMARY;
    driverConfig.expectedPart = board::EXPECTED_PART_PRIMARY;
    driverConfig.startupSpeed = AT21CS::SpeedMode::HIGH_SPEED;

    const AT21CS::Status status = _wire.start(transportConfig, driverConfig);
    printStatus(_output, "startup", status);
    if (status.code == AT21CS::Err::NOT_PRESENT ||
        (status.code == AT21CS::Err::NACK_DEVICE_ADDRESS &&
         AT21CS::protocolDetailPhase(status.detail) ==
             AT21CS::ProtocolPhase::DEVICE_ADDRESS_READ)) {
      _output.println("device absent; binding retained for recover");
    }
    printHelp();
  }

  void pollInput() {
    if (_commandReady) {
      return;
    }
    for (size_t count = 0; count < LINE_BYTES; ++count) {
      const int value = _readChar();
      if (value < 0) {
        return;
      }
      const LineEvent event =
          _cli.push(static_cast<char>(value), _pendingArguments);
      if (event == LineEvent::READY) {
        _commandReady = true;
        return;
      }
      if (event == LineEvent::TOO_LONG) {
        _output.println("input rejected: line too long");
        return;
      }
      if (event == LineEvent::TOO_MANY_ARGS) {
        _output.println("input rejected: too many arguments");
        return;
      }
    }
  }

  void step() {
    const uint32_t nowMs = _nowMs();
    const bool automaticDue =
        _wire.automaticDue(nowMs, board::DETECT_SAMPLE_MS, board::HOTPLUG_POLL_MS) ||
        _wire.shutdownCompletionDue();
    const WorkChoice choice = _arbiter.choose(_commandReady, automaticDue);

    if (choice == WorkChoice::AUTOMATIC) {
      if (_wire.completeShutdown()) {
        _output.println("shutdown: backend released");
      } else {
        const ServiceResult result = _wire.serviceHotPlug(
            nowMs, board::DETECT_SAMPLE_MS, board::DETECT_DEBOUNCE_MS,
            board::HOTPLUG_POLL_MS);
        printServiceResult(_output, "A", result);
      }
      _arbiter.completed(choice);
    } else if (choice == WorkChoice::COMMAND) {
      const DispatchResult result = dispatch(
          REGISTRATIONS, REGISTRATION_COUNT, _pendingArguments, this);
      printDispatchError(result);
      _commandReady = false;
      _arbiter.completed(choice);
    }
  }

 private:
  static void handleHelp(void* context, const Arguments&) {
    static_cast<BringupCli*>(context)->printHelp();
  }

  static void handleStatus(void* context, const Arguments&) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    printInstanceStatus(app._output, "A", app._wire);
  }

  static void handlePresence(void* context, const Arguments&) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    if (!app._wire.active()) {
      app._output.println("presence: inactive");
      return;
    }
    if (!app._wire.bus.hasPresenceIndicator()) {
      app._output.println("presence: disabled");
      return;
    }
    const ServiceResult result = app._wire.samplePresence(
        app._nowMs(), board::DETECT_DEBOUNCE_MS);
    printStatus(app._output, "presence", result.status);
    if (result.presenceKnown) {
      app._output.println(result.presence ? "logical presence: present"
                                          : "logical presence: absent");
    }
  }

  static void handleProbe(void* context, const Arguments&) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    printStatus(app._output, "probe", app._wire.driver.probe());
  }

  static void handleManufacturer(void* context, const Arguments&) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    uint32_t manufacturerId = 0;
    const AT21CS::Status status =
        app._wire.driver.readManufacturerId(manufacturerId);
    printStatus(app._output, "manufacturer", status);
    if (status.ok()) {
      app._output.printf("raw=0x%06lX part=%s revision=%u\n",
                         static_cast<unsigned long>(manufacturerId),
                         AT21CS::toString(app._wire.driver.detectedPart()),
                         static_cast<unsigned>(app._wire.driver.siliconRevision()));
    }
  }

  static void handleSerial(void* context, const Arguments&) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    const ServiceResult result = app._wire.readSerial();
    printStatus(app._output, "serial", result.status);
    if (result.status.ok()) {
      printIdentity(app._output, result.identity);
    }
  }

  static bool parseReadRange(const Arguments& arguments,
                             size_t memorySize,
                             uint8_t& address,
                             size_t& length) {
    uint32_t parsedAddress = 0;
    uint32_t parsedLength = 0;
    if (!parseDecimalOrExplicitHex(arguments.values[1],
                                   static_cast<uint32_t>(memorySize - 1),
                                   parsedAddress) ||
        !parseDecimal(arguments.values[2],
                      static_cast<uint32_t>(READ_BUFFER_BYTES), parsedLength) ||
        parsedLength == 0 || parsedLength > memorySize - parsedAddress) {
      return false;
    }
    address = static_cast<uint8_t>(parsedAddress);
    length = static_cast<size_t>(parsedLength);
    return true;
  }

  static void handleReadEeprom(void* context, const Arguments& arguments) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    uint8_t address = 0;
    size_t length = 0;
    if (!parseReadRange(arguments, AT21CS::cmd::EEPROM_SIZE, address, length)) {
      app._output.println("invalid EEPROM range; use address 0..127 and length 1..32");
      return;
    }
    uint8_t data[READ_BUFFER_BYTES] = {};
    const AT21CS::Status status =
        app._wire.driver.readEeprom(address, data, length);
    printStatus(app._output, "read-eeprom", status);
    if (status.ok()) {
      printBytes(app._output, data, length);
    }
  }

  static void handleReadSecurity(void* context, const Arguments& arguments) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    uint8_t address = 0;
    size_t length = 0;
    if (!parseReadRange(arguments, AT21CS::cmd::SECURITY_SIZE, address, length)) {
      app._output.println("invalid Security range; use address 0..31 and length 1..32");
      return;
    }
    uint8_t data[READ_BUFFER_BYTES] = {};
    const AT21CS::Status status =
        app._wire.driver.readSecurity(address, data, length);
    printStatus(app._output, "read-security", status);
    if (status.ok()) {
      printBytes(app._output, data, length);
    }
  }

  static void handleSecurityLocked(void* context, const Arguments&) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    bool locked = false;
    const AT21CS::Status status =
        app._wire.driver.readSecurityLockState(locked);
    printStatus(app._output, "security-locked", status);
    if (status.ok()) {
      app._output.println(locked ? "locked" : "unlocked");
    }
  }

  static void handleRomZone(void* context, const Arguments& arguments) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    uint32_t zone = 0;
    if (!parseDecimal(arguments.values[1],
                      AT21CS::cmd::ROM_ZONE_REGISTER_COUNT - 1, zone)) {
      app._output.println("invalid ROM zone; use 0..3");
      return;
    }
    bool enabled = false;
    const AT21CS::Status status = app._wire.driver.readRomZoneState(
        static_cast<uint8_t>(zone), enabled);
    printStatus(app._output, "rom-zone", status);
    if (status.ok()) {
      app._output.println(enabled ? "ROM enabled" : "writable EEPROM");
    }
  }

  static void handleSpeed(void* context, const Arguments& arguments) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    AT21CS::SpeedMode mode = AT21CS::SpeedMode::HIGH_SPEED;
    if (std::strcmp(arguments.values[1], "high") == 0) {
      mode = AT21CS::SpeedMode::HIGH_SPEED;
    } else if (std::strcmp(arguments.values[1], "standard") == 0) {
      mode = AT21CS::SpeedMode::STANDARD_SPEED;
    } else {
      app._output.println("invalid speed; use high or standard");
      return;
    }
    printStatus(app._output, "speed", app._wire.driver.setSpeedMode(mode));
  }

  static void handleRecover(void* context, const Arguments&) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    const ServiceResult result = app._wire.recover(app._nowMs());
    printStatus(app._output, "recover", result.status);
    if (result.status.ok()) {
      app._output.println("serial comparison scheduled for the next service call");
    }
  }

  static void handleWritePage(void* context, const Arguments& arguments) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    uint32_t parsedAddress = 0;
    uint8_t data[AT21CS::cmd::PAGE_SIZE] = {};
    size_t length = 0;
    if (!parseDecimalOrExplicitHex(arguments.values[1],
                                   AT21CS::cmd::EEPROM_SIZE - 1,
                                   parsedAddress) ||
        !parseHexBytes(arguments.values[2], data, sizeof(data), length) ||
        length > AT21CS::cmd::EEPROM_SIZE - parsedAddress ||
        (parsedAddress % AT21CS::cmd::PAGE_SIZE) + length >
            AT21CS::cmd::PAGE_SIZE) {
      app._output.println("invalid page write: use one page, 1..8 bytes, exact hex");
      return;
    }

    AT21CS::WriteResult writeResult{};
    const AT21CS::Status status = app._wire.driver.writeEepromPage(
        static_cast<uint8_t>(parsedAddress), data, length, writeResult);
    printStatus(app._output, "write-page", status);
    printWriteResult(app._output, writeResult);
  }

  static void handleShutdown(void* context, const Arguments&) {
    BringupCli& app = *static_cast<BringupCli*>(context);
    printStatus(app._output, "shutdown", app._wire.shutdown());
  }

  static constexpr CommandRegistration REGISTRATIONS[] = {
      {CommandId::HELP, CommandForm::SINGLE, handleHelp},
      {CommandId::STATUS, CommandForm::SINGLE, handleStatus},
      {CommandId::PRESENCE, CommandForm::SINGLE, handlePresence},
      {CommandId::PROBE, CommandForm::SINGLE, handleProbe},
      {CommandId::MANUFACTURER, CommandForm::SINGLE, handleManufacturer},
      {CommandId::SERIAL_NUMBER, CommandForm::SINGLE, handleSerial},
      {CommandId::READ_EEPROM, CommandForm::SINGLE, handleReadEeprom},
      {CommandId::READ_SECURITY, CommandForm::SINGLE, handleReadSecurity},
      {CommandId::SECURITY_LOCKED, CommandForm::SINGLE, handleSecurityLocked},
      {CommandId::ROM_ZONE, CommandForm::SINGLE, handleRomZone},
      {CommandId::SPEED, CommandForm::SINGLE, handleSpeed},
      {CommandId::RECOVER, CommandForm::SINGLE, handleRecover},
      {CommandId::WRITE_PAGE, CommandForm::SINGLE, handleWritePage},
      {CommandId::SHUTDOWN, CommandForm::SINGLE, handleShutdown},
  };

  static constexpr size_t REGISTRATION_COUNT =
      sizeof(REGISTRATIONS) / sizeof(REGISTRATIONS[0]);

  void printHelp() {
    _output.println("Commands:");
    for (size_t index = 0; index < REGISTRATION_COUNT; ++index) {
      const CommandSpec* const spec = commandSpec(REGISTRATIONS[index].id);
      if (spec != nullptr) {
        _output.printf("  %s\n", commandUsage(*spec, REGISTRATIONS[index].form));
      }
    }
  }

  void printDispatchError(const DispatchResult& result) {
    switch (result.code) {
      case DispatchCode::UNKNOWN_COMMAND:
        _output.println("unknown command; use help");
        break;
      case DispatchCode::WRONG_ARITY:
        _output.printf("usage: %s\n",
                       commandUsage(*result.spec, CommandForm::SINGLE));
        break;
      case DispatchCode::CONFIRMATION_REQUIRED:
        _output.printf("write rejected; final token must be exactly %s\n",
                       EEPROM_CONFIRMATION);
        break;
      case DispatchCode::INVALID_REGISTRATION:
        _output.println("internal command registration error");
        break;
      case DispatchCode::HANDLED:
      case DispatchCode::EMPTY:
        break;
    }
  }

  WireInstance& _wire;
  CliOutput& _output;
  uint32_t (*_nowMs)();
  int (*_readChar)();
  BoundedCli _cli;
  Arguments _pendingArguments{};
  WorkArbiter _arbiter;
  bool _commandReady = false;
};

}  // namespace at21cs_example
