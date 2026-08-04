#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "BoundedCli.h"

namespace at21cs_example {

static constexpr size_t READ_BUFFER_BYTES = 32;
static constexpr char EEPROM_CONFIRMATION[] = "CONFIRM_EEPROM_OVERWRITE";

enum class CommandId : uint8_t {
  HELP = 0,
  STATUS,
  PRESENCE,
  PROBE,
  MANUFACTURER,
  SERIAL_NUMBER,
  READ_EEPROM,
  READ_SECURITY,
  SECURITY_LOCKED,
  ROM_ZONE,
  SPEED,
  RECOVER,
  WRITE_PAGE,
  SHUTDOWN
};

enum class CommandRisk : uint8_t {
  SAFE = 0,
  DESTRUCTIVE
};

enum class CommandForm : uint8_t {
  SINGLE = 0,
  MULTI
};

struct CommandSpec {
  CommandId id;
  const char* name;
  const char* singleUsage;
  uint8_t singleArgumentCount;
  const char* multiUsage;
  uint8_t multiArgumentCount;
  CommandRisk risk;
};

static constexpr CommandSpec COMMAND_CATALOG[] = {
    {CommandId::HELP, "help", "help", 0, "help", 0, CommandRisk::SAFE},
    {CommandId::STATUS, "status", "status", 0, "status <wire>", 1,
     CommandRisk::SAFE},
    {CommandId::PRESENCE, "presence", "presence", 0, "presence <wire>", 1,
     CommandRisk::SAFE},
    {CommandId::PROBE, "probe", "probe", 0, "probe <wire>", 1,
     CommandRisk::SAFE},
    {CommandId::MANUFACTURER, "manufacturer", "manufacturer", 0, nullptr, 0,
     CommandRisk::SAFE},
    {CommandId::SERIAL_NUMBER, "serial", "serial", 0, "serial <wire>", 1,
     CommandRisk::SAFE},
    {CommandId::READ_EEPROM, "read-eeprom",
     "read-eeprom <address> <length>", 2,
     "read-eeprom <wire> <address> <length>", 3, CommandRisk::SAFE},
    {CommandId::READ_SECURITY, "read-security",
     "read-security <address> <length>", 2, nullptr, 0, CommandRisk::SAFE},
    {CommandId::SECURITY_LOCKED, "security-locked", "security-locked", 0,
     nullptr, 0, CommandRisk::SAFE},
    {CommandId::ROM_ZONE, "rom-zone", "rom-zone <0..3>", 1, nullptr, 0,
     CommandRisk::SAFE},
    {CommandId::SPEED, "speed", "speed <high|standard>", 1, nullptr, 0,
     CommandRisk::SAFE},
    {CommandId::RECOVER, "recover", "recover", 0, "recover <wire>", 1,
     CommandRisk::SAFE},
    {CommandId::WRITE_PAGE, "write-page",
     "write-page <address> <2..16 hexadecimal digits> "
     "CONFIRM_EEPROM_OVERWRITE",
     3, nullptr, 0, CommandRisk::DESTRUCTIVE},
    {CommandId::SHUTDOWN, "shutdown", "shutdown", 0,
     "shutdown <wire|all>", 1, CommandRisk::SAFE},
};

static constexpr size_t COMMAND_CATALOG_COUNT =
    sizeof(COMMAND_CATALOG) / sizeof(COMMAND_CATALOG[0]);

inline const CommandSpec* commandSpec(CommandId id) {
  for (size_t index = 0; index < COMMAND_CATALOG_COUNT; ++index) {
    if (COMMAND_CATALOG[index].id == id) {
      return &COMMAND_CATALOG[index];
    }
  }
  return nullptr;
}

using CommandHandler = void (*)(void* context, const Arguments& arguments);

struct CommandRegistration {
  CommandId id;
  CommandForm form;
  CommandHandler handler;
};

enum class DispatchCode : uint8_t {
  HANDLED = 0,
  EMPTY,
  UNKNOWN_COMMAND,
  WRONG_ARITY,
  CONFIRMATION_REQUIRED,
  INVALID_REGISTRATION
};

struct DispatchResult {
  DispatchCode code = DispatchCode::EMPTY;
  const CommandSpec* spec = nullptr;
};

inline const char* commandUsage(const CommandSpec& spec, CommandForm form) {
  return form == CommandForm::SINGLE ? spec.singleUsage : spec.multiUsage;
}

inline uint8_t commandArgumentCount(const CommandSpec& spec,
                                    CommandForm form) {
  return form == CommandForm::SINGLE ? spec.singleArgumentCount
                                     : spec.multiArgumentCount;
}

inline DispatchResult dispatch(const CommandRegistration* registrations,
                               size_t registrationCount,
                               const Arguments& arguments,
                               void* context) {
  if (arguments.count == 0) {
    return {};
  }
  if (registrations == nullptr || arguments.values[0] == nullptr) {
    return {DispatchCode::INVALID_REGISTRATION, nullptr};
  }

  for (size_t index = 0; index < registrationCount; ++index) {
    const CommandRegistration& registration = registrations[index];
    const CommandSpec* const spec = commandSpec(registration.id);
    const char* const usage =
        spec == nullptr ? nullptr : commandUsage(*spec, registration.form);
    const uint8_t argumentCount =
        spec == nullptr ? 0 : commandArgumentCount(*spec, registration.form);
    if (spec == nullptr || usage == nullptr || registration.handler == nullptr ||
        argumentCount > MAX_ARGS - 1) {
      return {DispatchCode::INVALID_REGISTRATION, spec};
    }
    if (std::strcmp(spec->name, arguments.values[0]) != 0) {
      continue;
    }
    if (argumentCount != arguments.count - 1) {
      return {DispatchCode::WRONG_ARITY, spec};
    }
    if (spec->risk == CommandRisk::DESTRUCTIVE &&
        std::strcmp(arguments.values[arguments.count - 1],
                    EEPROM_CONFIRMATION) != 0) {
      return {DispatchCode::CONFIRMATION_REQUIRED, spec};
    }

    registration.handler(context, arguments);
    return {DispatchCode::HANDLED, spec};
  }

  return {DispatchCode::UNKNOWN_COMMAND, nullptr};
}

}  // namespace at21cs_example
