#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AT21CS/AT21CS.h"
#include "BoardConfig.h"
#include "BoundedCli.h"
#include "CommandContract.h"
#include "StatusText.h"
#include "WireInstance.h"

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

namespace {

using namespace at21cs_example;

struct Application {
  WireInstance wire;
  BoundedCli cli;
  Arguments pendingArguments{};
  WorkArbiter arbiter;
  bool commandReady = false;
};

Application app;

void printHelp();

void handleHelp(void*, const Arguments&) {
  printHelp();
}

void handleStatus(void* context, const Arguments&) {
  Application& application = *static_cast<Application*>(context);
  printInstanceStatus("A", application.wire);
}

void handlePresence(void* context, const Arguments&) {
  Application& application = *static_cast<Application*>(context);
  if (!application.wire.active()) {
    Serial.println("presence: inactive");
    return;
  }
  if (!application.wire.bus.hasPresenceIndicator()) {
    Serial.println("presence: disabled");
    return;
  }
  const ServiceResult result = application.wire.samplePresence(
      millis(), board::DETECT_DEBOUNCE_MS);
  printStatus("presence", result.status);
  if (result.presenceKnown) {
    Serial.println(result.presence ? "logical presence: present"
                                   : "logical presence: absent");
  }
}

void handleProbe(void* context, const Arguments&) {
  Application& application = *static_cast<Application*>(context);
  printStatus("probe", application.wire.driver.probe());
}

void handleManufacturer(void* context, const Arguments&) {
  Application& application = *static_cast<Application*>(context);
  uint32_t manufacturerId = 0;
  const AT21CS::Status status =
      application.wire.driver.readManufacturerId(manufacturerId);
  printStatus("manufacturer", status);
  if (status.ok()) {
    Serial.printf("raw=0x%06lX part=%s revision=%u\n",
                  static_cast<unsigned long>(manufacturerId),
                  AT21CS::toString(application.wire.driver.detectedPart()),
                  static_cast<unsigned>(
                      application.wire.driver.siliconRevision()));
  }
}

void handleSerial(void* context, const Arguments&) {
  Application& application = *static_cast<Application*>(context);
  const ServiceResult result = application.wire.readSerial();
  printStatus("serial", result.status);
  if (result.status.ok()) {
    printIdentity(result.identity);
  }
}

bool parseReadRange(const Arguments& arguments,
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

void handleReadEeprom(void* context, const Arguments& arguments) {
  Application& application = *static_cast<Application*>(context);
  uint8_t address = 0;
  size_t length = 0;
  if (!parseReadRange(arguments, AT21CS::cmd::EEPROM_SIZE, address, length)) {
    Serial.println("invalid EEPROM range; use address 0..127 and length 1..32");
    return;
  }
  uint8_t data[READ_BUFFER_BYTES] = {};
  const AT21CS::Status status =
      application.wire.driver.readEeprom(address, data, length);
  printStatus("read-eeprom", status);
  if (status.ok()) {
    printBytes(data, length);
  }
}

void handleReadSecurity(void* context, const Arguments& arguments) {
  Application& application = *static_cast<Application*>(context);
  uint8_t address = 0;
  size_t length = 0;
  if (!parseReadRange(arguments, AT21CS::cmd::SECURITY_SIZE, address, length)) {
    Serial.println("invalid Security range; use address 0..31 and length 1..32");
    return;
  }
  uint8_t data[READ_BUFFER_BYTES] = {};
  const AT21CS::Status status =
      application.wire.driver.readSecurity(address, data, length);
  printStatus("read-security", status);
  if (status.ok()) {
    printBytes(data, length);
  }
}

void handleSecurityLocked(void* context, const Arguments&) {
  Application& application = *static_cast<Application*>(context);
  bool locked = false;
  const AT21CS::Status status =
      application.wire.driver.readSecurityLockState(locked);
  printStatus("security-locked", status);
  if (status.ok()) {
    Serial.println(locked ? "locked" : "unlocked");
  }
}

void handleRomZone(void* context, const Arguments& arguments) {
  Application& application = *static_cast<Application*>(context);
  uint32_t zone = 0;
  if (!parseDecimal(arguments.values[1],
                    AT21CS::cmd::ROM_ZONE_REGISTER_COUNT - 1, zone)) {
    Serial.println("invalid ROM zone; use 0..3");
    return;
  }
  bool enabled = false;
  const AT21CS::Status status = application.wire.driver.readRomZoneState(
      static_cast<uint8_t>(zone), enabled);
  printStatus("rom-zone", status);
  if (status.ok()) {
    Serial.println(enabled ? "ROM enabled" : "writable EEPROM");
  }
}

void handleSpeed(void* context, const Arguments& arguments) {
  Application& application = *static_cast<Application*>(context);
  AT21CS::SpeedMode mode = AT21CS::SpeedMode::HIGH_SPEED;
  if (std::strcmp(arguments.values[1], "high") == 0) {
    mode = AT21CS::SpeedMode::HIGH_SPEED;
  } else if (std::strcmp(arguments.values[1], "standard") == 0) {
    mode = AT21CS::SpeedMode::STANDARD_SPEED;
  } else {
    Serial.println("invalid speed; use high or standard");
    return;
  }
  printStatus("speed", application.wire.driver.setSpeedMode(mode));
}

void handleRecover(void* context, const Arguments&) {
  Application& application = *static_cast<Application*>(context);
  const ServiceResult result = application.wire.recover(millis());
  printStatus("recover", result.status);
  if (result.status.ok()) {
    Serial.println("serial comparison scheduled for the next service call");
  }
}

void handleWritePage(void* context, const Arguments& arguments) {
  Application& application = *static_cast<Application*>(context);
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
    Serial.println("invalid page write: use one page, 1..8 bytes, exact hex");
    return;
  }

  AT21CS::WriteResult writeResult{};
  const AT21CS::Status status = application.wire.driver.writeEepromPage(
      static_cast<uint8_t>(parsedAddress), data, length, writeResult);
  printStatus("write-page", status);
  printWriteResult(writeResult);
}

void handleShutdown(void* context, const Arguments&) {
  Application& application = *static_cast<Application*>(context);
  printStatus("shutdown", application.wire.shutdown());
}

static const CommandRegistration REGISTRATIONS[] = {
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
  Serial.println("Commands:");
  for (size_t index = 0; index < REGISTRATION_COUNT; ++index) {
    const CommandSpec* const spec = commandSpec(REGISTRATIONS[index].id);
    if (spec != nullptr) {
      Serial.printf("  %s\n", commandUsage(*spec, REGISTRATIONS[index].form));
    }
  }
}

void printDispatchError(const DispatchResult& result) {
  switch (result.code) {
    case DispatchCode::UNKNOWN_COMMAND:
      Serial.println("unknown command; use help");
      break;
    case DispatchCode::WRONG_ARITY:
      Serial.printf("usage: %s\n",
                    commandUsage(*result.spec, CommandForm::SINGLE));
      break;
    case DispatchCode::CONFIRMATION_REQUIRED:
      Serial.printf("write rejected; final token must be exactly %s\n",
                    EEPROM_CONFIRMATION);
      break;
    case DispatchCode::INVALID_REGISTRATION:
      Serial.println("internal command registration error");
      break;
    case DispatchCode::HANDLED:
    case DispatchCode::EMPTY:
      break;
  }
}

void pollCli() {
  if (app.commandReady) {
    return;
  }
  for (size_t count = 0;
       count < LINE_BYTES && Serial.available() > 0;
       ++count) {
    const LineEvent event =
        app.cli.push(static_cast<char>(Serial.read()), app.pendingArguments);
    if (event == LineEvent::READY) {
      app.commandReady = true;
      return;
    }
    if (event == LineEvent::TOO_LONG) {
      Serial.println("input rejected: line too long");
      return;
    }
    if (event == LineEvent::TOO_MANY_ARGS) {
      Serial.println("input rejected: too many arguments");
      return;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(at21cs_example::board::SERIAL_BAUD);
  const uint32_t serialStartMs = millis();
  while (!Serial && static_cast<uint32_t>(millis() - serialStartMs) < 3000U) {
    delay(10);
  }

  AT21CS::Esp32TransportConfig transportConfig{};
  transportConfig.sioPin = at21cs_example::board::SIO_PRIMARY;
  transportConfig.presencePin = at21cs_example::board::PRESENCE_PRIMARY;
  transportConfig.presenceActiveHigh =
      at21cs_example::board::PRESENCE_PRIMARY_ACTIVE_HIGH;

  AT21CS::Config driverConfig{};
  driverConfig.addressBits = at21cs_example::board::ADDRESS_BITS_PRIMARY;
  driverConfig.offlineThreshold =
      at21cs_example::board::OFFLINE_THRESHOLD_PRIMARY;
  driverConfig.expectedPart = at21cs_example::board::EXPECTED_PART_PRIMARY;
  driverConfig.startupSpeed = AT21CS::SpeedMode::HIGH_SPEED;

  const AT21CS::Status status = app.wire.start(transportConfig, driverConfig);
  at21cs_example::printStatus("startup", status);
  if (status.code == AT21CS::Err::NOT_PRESENT ||
      (status.code == AT21CS::Err::NACK_DEVICE_ADDRESS &&
       AT21CS::protocolDetailPhase(status.detail) ==
           AT21CS::ProtocolPhase::DEVICE_ADDRESS_READ)) {
    Serial.println("device absent; binding retained for recover");
  }
  printHelp();
}

void loop() {
  pollCli();
  const uint32_t nowMs = millis();
  const bool automaticDue = app.wire.automaticDue(
      nowMs, at21cs_example::board::DETECT_SAMPLE_MS,
      at21cs_example::board::HOTPLUG_POLL_MS) ||
      app.wire.shutdownCompletionDue();
  const at21cs_example::WorkChoice choice =
      app.arbiter.choose(app.commandReady, automaticDue);

  if (choice == at21cs_example::WorkChoice::AUTOMATIC) {
    if (app.wire.completeShutdown()) {
      Serial.println("shutdown: backend released");
    } else {
      const at21cs_example::ServiceResult result = app.wire.serviceHotPlug(
          nowMs, at21cs_example::board::DETECT_SAMPLE_MS,
          at21cs_example::board::DETECT_DEBOUNCE_MS,
          at21cs_example::board::HOTPLUG_POLL_MS);
      at21cs_example::printServiceResult("A", result);
    }
    app.arbiter.completed(choice);
  } else if (choice == at21cs_example::WorkChoice::COMMAND) {
    const at21cs_example::DispatchResult result = at21cs_example::dispatch(
        REGISTRATIONS, REGISTRATION_COUNT, app.pendingArguments, &app);
    printDispatchError(result);
    app.commandReady = false;
    app.arbiter.completed(choice);
  }
}
