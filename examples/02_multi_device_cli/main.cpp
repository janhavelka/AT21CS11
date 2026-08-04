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

// The safe RTOS pattern is one firmware task owning both synchronous wire
// instances and calling them sequentially. A shared physical wire instead uses
// one Backend and one Bus with uniquely addressed Drivers--never one Bus per
// address. Queues, retries, deadlines, and replacement policy belong to the
// consuming firmware, not this library.

namespace {

using namespace at21cs_example;

struct Application {
  WireInstance wireA;
  WireInstance wireB;
  BoundedCli cli;
  Arguments pendingArguments{};
  WorkArbiter workArbiter;
  WireArbiter wireArbiter;
  bool commandReady = false;
  bool shutdownBPending = false;
};

Application app;

WireInstance* selectWire(const char* text) {
  if (std::strcmp(text, "A") == 0) {
    return &app.wireA;
  }
  if (std::strcmp(text, "B") == 0) {
    return &app.wireB;
  }
  return nullptr;
}

const char* wireName(const WireInstance* wire) {
  return wire == &app.wireA ? "A" : "B";
}

void printHelp();

void handleHelp(void*, const Arguments&) {
  printHelp();
}

void handleStatus(void*, const Arguments& arguments) {
  WireInstance* const wire = selectWire(arguments.values[1]);
  if (wire == nullptr) {
    Serial.println("invalid wire; use A or B");
    return;
  }
  printInstanceStatus(wireName(wire), *wire);
}

void handlePresence(void*, const Arguments& arguments) {
  WireInstance* const wire = selectWire(arguments.values[1]);
  if (wire == nullptr) {
    Serial.println("invalid wire; use A or B");
    return;
  }
  if (!wire->active()) {
    Serial.printf("[%s] presence: inactive\n", wireName(wire));
    return;
  }
  if (!wire->bus.hasPresenceIndicator()) {
    Serial.printf("[%s] presence: disabled\n", wireName(wire));
    return;
  }
  const ServiceResult result =
      wire->samplePresence(millis(), board::DETECT_DEBOUNCE_MS);
  printStatus("presence", result.status);
  if (result.presenceKnown) {
    Serial.printf("[%s] logical presence: %s\n", wireName(wire),
                  result.presence ? "present" : "absent");
  }
}

void handleProbe(void*, const Arguments& arguments) {
  WireInstance* const wire = selectWire(arguments.values[1]);
  if (wire == nullptr) {
    Serial.println("invalid wire; use A or B");
    return;
  }
  printStatus("probe", wire->driver.probe());
}

void handleSerial(void*, const Arguments& arguments) {
  WireInstance* const wire = selectWire(arguments.values[1]);
  if (wire == nullptr) {
    Serial.println("invalid wire; use A or B");
    return;
  }
  const ServiceResult result = wire->readSerial();
  printStatus("serial", result.status);
  if (result.status.ok()) {
    printIdentity(result.identity);
  }
}

void handleReadEeprom(void*, const Arguments& arguments) {
  WireInstance* const wire = selectWire(arguments.values[1]);
  if (wire == nullptr) {
    Serial.println("invalid wire; use A or B");
    return;
  }

  uint32_t address = 0;
  uint32_t length = 0;
  if (!parseDecimalOrExplicitHex(arguments.values[2],
                                 AT21CS::cmd::EEPROM_SIZE - 1, address) ||
      !parseDecimal(arguments.values[3], READ_BUFFER_BYTES, length) ||
      length == 0 || length > AT21CS::cmd::EEPROM_SIZE - address) {
    Serial.println("invalid EEPROM range; use address 0..127 and length 1..32");
    return;
  }

  uint8_t data[READ_BUFFER_BYTES] = {};
  const AT21CS::Status status = wire->driver.readEeprom(
      static_cast<uint8_t>(address), data, static_cast<size_t>(length));
  printStatus("read-eeprom", status);
  if (status.ok()) {
    printBytes(data, static_cast<size_t>(length));
  }
}

void handleRecover(void*, const Arguments& arguments) {
  WireInstance* const wire = selectWire(arguments.values[1]);
  if (wire == nullptr) {
    Serial.println("invalid wire; use A or B");
    return;
  }
  const ServiceResult result = wire->recover(millis());
  printStatus("recover", result.status);
  if (result.status.ok()) {
    Serial.printf("[%s] serial comparison scheduled\n", wireName(wire));
  }
}

void handleShutdown(void*, const Arguments& arguments) {
  if (std::strcmp(arguments.values[1], "all") == 0) {
    printStatus("shutdown A", app.wireA.shutdown());
    app.shutdownBPending = true;
    return;
  }
  WireInstance* const wire = selectWire(arguments.values[1]);
  if (wire == nullptr) {
    Serial.println("invalid shutdown target; use A, B, or all");
    return;
  }
  printStatus(wire == &app.wireA ? "shutdown A" : "shutdown B",
              wire->shutdown());
}

static const CommandRegistration REGISTRATIONS[] = {
    {CommandId::HELP, CommandForm::MULTI, handleHelp},
    {CommandId::STATUS, CommandForm::MULTI, handleStatus},
    {CommandId::PRESENCE, CommandForm::MULTI, handlePresence},
    {CommandId::PROBE, CommandForm::MULTI, handleProbe},
    {CommandId::SERIAL_NUMBER, CommandForm::MULTI, handleSerial},
    {CommandId::READ_EEPROM, CommandForm::MULTI, handleReadEeprom},
    {CommandId::RECOVER, CommandForm::MULTI, handleRecover},
    {CommandId::SHUTDOWN, CommandForm::MULTI, handleShutdown},
};

static constexpr size_t REGISTRATION_COUNT =
    sizeof(REGISTRATIONS) / sizeof(REGISTRATIONS[0]);

void printHelp() {
  Serial.println("Commands (<wire> is exactly A or B):");
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
                    commandUsage(*result.spec, CommandForm::MULTI));
      break;
    case DispatchCode::CONFIRMATION_REQUIRED:
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

bool serviceAutomatic(uint32_t nowMs) {
  if (app.wireA.completeShutdown()) {
    Serial.println("[A] shutdown: backend released");
    return true;
  }
  if (app.wireB.completeShutdown()) {
    Serial.println("[B] shutdown: backend released");
    return true;
  }
  if (app.shutdownBPending) {
    printStatus("shutdown B", app.wireB.shutdown());
    app.shutdownBPending = false;
    return true;
  }

  const bool aDue = app.wireA.automaticDue(
      nowMs, board::DETECT_SAMPLE_MS, board::HOTPLUG_POLL_MS);
  const bool bDue = app.wireB.automaticDue(
      nowMs, board::DETECT_SAMPLE_MS, board::HOTPLUG_POLL_MS);
  const WireChoice choice = app.wireArbiter.choose(aDue, bDue);
  if (choice == WireChoice::NONE) {
    return false;
  }

  WireInstance& wire = choice == WireChoice::A ? app.wireA : app.wireB;
  const char* const name = choice == WireChoice::A ? "A" : "B";
  const ServiceResult result = wire.serviceHotPlug(
      nowMs, board::DETECT_SAMPLE_MS, board::DETECT_DEBOUNCE_MS,
      board::HOTPLUG_POLL_MS);
  printServiceResult(name, result);
  app.wireArbiter.completed(choice);
  return result.performed;
}

AT21CS::Status startWire(WireInstance& wire,
                         int sioPin,
                         int presencePin,
                         bool presenceActiveHigh,
                         uint8_t addressBits,
                         uint8_t offlineThreshold,
                         AT21CS::PartType expectedPart) {
  AT21CS::Esp32TransportConfig transportConfig{};
  transportConfig.sioPin = sioPin;
  transportConfig.presencePin = presencePin;
  transportConfig.presenceActiveHigh = presenceActiveHigh;

  AT21CS::Config driverConfig{};
  driverConfig.addressBits = addressBits;
  driverConfig.offlineThreshold = offlineThreshold;
  driverConfig.expectedPart = expectedPart;
  driverConfig.startupSpeed = AT21CS::SpeedMode::HIGH_SPEED;
  return wire.start(transportConfig, driverConfig);
}

}  // namespace

void setup() {
  Serial.begin(at21cs_example::board::SERIAL_BAUD);
  const uint32_t serialStartMs = millis();
  while (!Serial && static_cast<uint32_t>(millis() - serialStartMs) < 3000U) {
    delay(10);
  }

  const AT21CS::Status statusA = startWire(
      app.wireA, at21cs_example::board::SIO_PRIMARY,
      at21cs_example::board::PRESENCE_PRIMARY,
      at21cs_example::board::PRESENCE_PRIMARY_ACTIVE_HIGH,
      at21cs_example::board::ADDRESS_BITS_PRIMARY,
      at21cs_example::board::OFFLINE_THRESHOLD_PRIMARY,
      at21cs_example::board::EXPECTED_PART_PRIMARY);
  at21cs_example::printStatus("startup A", statusA);

  const AT21CS::Status statusB = startWire(
      app.wireB, at21cs_example::board::SIO_SECONDARY,
      at21cs_example::board::PRESENCE_SECONDARY,
      at21cs_example::board::PRESENCE_SECONDARY_ACTIVE_HIGH,
      at21cs_example::board::ADDRESS_BITS_SECONDARY,
      at21cs_example::board::OFFLINE_THRESHOLD_SECONDARY,
      at21cs_example::board::EXPECTED_PART_SECONDARY);
  at21cs_example::printStatus("startup B", statusB);
  Serial.println("A and B are separate wires; address zero is valid on both.");
  printHelp();
}

void loop() {
  pollCli();
  const uint32_t nowMs = millis();
  const bool automaticDue =
      app.shutdownBPending ||
      app.wireA.shutdownCompletionDue() || app.wireB.shutdownCompletionDue() ||
      app.wireA.automaticDue(nowMs, at21cs_example::board::DETECT_SAMPLE_MS,
                             at21cs_example::board::HOTPLUG_POLL_MS) ||
      app.wireB.automaticDue(nowMs, at21cs_example::board::DETECT_SAMPLE_MS,
                             at21cs_example::board::HOTPLUG_POLL_MS);
  const at21cs_example::WorkChoice choice =
      app.workArbiter.choose(app.commandReady, automaticDue);

  if (choice == at21cs_example::WorkChoice::AUTOMATIC) {
    if (serviceAutomatic(nowMs)) {
      app.workArbiter.completed(choice);
    }
  } else if (choice == at21cs_example::WorkChoice::COMMAND) {
    const at21cs_example::DispatchResult result = at21cs_example::dispatch(
        REGISTRATIONS, REGISTRATION_COUNT, app.pendingArguments, &app);
    printDispatchError(result);
    app.commandReady = false;
    app.workArbiter.completed(choice);
  }
}
