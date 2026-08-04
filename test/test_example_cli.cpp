#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "BoundedCli.h"
#include "CommandContract.h"

namespace {

using namespace at21cs_example;

LineEvent pushText(BoundedCli& cli, const char* text, Arguments& arguments) {
  LineEvent result = LineEvent::NONE;
  while (*text != '\0') {
    result = cli.push(*text++, arguments);
    if (result != LineEvent::NONE) {
      return result;
    }
  }
  return result;
}

struct Calls {
  unsigned help = 0;
  unsigned read = 0;
  unsigned write = 0;
};

void countHelp(void* context, const Arguments&) {
  ++static_cast<Calls*>(context)->help;
}

void countRead(void* context, const Arguments&) {
  ++static_cast<Calls*>(context)->read;
}

void validateAndCountWrite(void* context, const Arguments& arguments) {
  uint32_t address = 0;
  uint8_t bytes[8] = {};
  size_t length = 0;
  if (!parseDecimalOrExplicitHex(arguments.values[1], 127, address) ||
      !parseHexBytes(arguments.values[2], bytes, sizeof(bytes), length) ||
      length > 128U - address || (address % 8U) + length > 8U) {
    return;
  }
  ++static_cast<Calls*>(context)->write;
}

const CommandRegistration TEST_COMMANDS[] = {
    {CommandId::HELP, CommandForm::SINGLE, countHelp},
    {CommandId::READ_EEPROM, CommandForm::SINGLE, countRead},
    {CommandId::WRITE_PAGE, CommandForm::SINGLE, validateAndCountWrite},
};

struct CatalogCalls {
  unsigned count[14] = {};
};

template <size_t Index>
void countCatalog(void* context, const Arguments&) {
  ++static_cast<CatalogCalls*>(context)->count[Index];
}

const CommandRegistration ALL_SINGLE_COMMANDS[] = {
    {CommandId::HELP, CommandForm::SINGLE, countCatalog<0>},
    {CommandId::STATUS, CommandForm::SINGLE, countCatalog<1>},
    {CommandId::PRESENCE, CommandForm::SINGLE, countCatalog<2>},
    {CommandId::PROBE, CommandForm::SINGLE, countCatalog<3>},
    {CommandId::MANUFACTURER, CommandForm::SINGLE, countCatalog<4>},
    {CommandId::SERIAL_NUMBER, CommandForm::SINGLE, countCatalog<5>},
    {CommandId::READ_EEPROM, CommandForm::SINGLE, countCatalog<6>},
    {CommandId::READ_SECURITY, CommandForm::SINGLE, countCatalog<7>},
    {CommandId::SECURITY_LOCKED, CommandForm::SINGLE, countCatalog<8>},
    {CommandId::ROM_ZONE, CommandForm::SINGLE, countCatalog<9>},
    {CommandId::SPEED, CommandForm::SINGLE, countCatalog<10>},
    {CommandId::RECOVER, CommandForm::SINGLE, countCatalog<11>},
    {CommandId::WRITE_PAGE, CommandForm::SINGLE, countCatalog<12>},
    {CommandId::SHUTDOWN, CommandForm::SINGLE, countCatalog<13>},
};

}  // namespace

void test_bounded_cli_handles_empty_crlf_and_exact_line_limit() {
  BoundedCli cli;
  Arguments arguments{};

  TEST_ASSERT_EQUAL_INT(static_cast<int>(LineEvent::NONE),
                        static_cast<int>(pushText(cli, "\r\n", arguments)));
  TEST_ASSERT_EQUAL_UINT32(0, arguments.count);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(LineEvent::READY),
                        static_cast<int>(pushText(cli, "help\r", arguments)));
  TEST_ASSERT_EQUAL_UINT32(1, arguments.count);
  TEST_ASSERT_EQUAL_STRING("help", arguments.values[0]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LineEvent::NONE),
                        static_cast<int>(cli.push('\n', arguments)));

  char maximumLine[LINE_BYTES] = {};
  for (size_t index = 0; index < LINE_BYTES - 1; ++index) {
    maximumLine[index] = 'a';
    TEST_ASSERT_EQUAL_INT(static_cast<int>(LineEvent::NONE),
                          static_cast<int>(cli.push('a', arguments)));
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LineEvent::READY),
                        static_cast<int>(cli.push('\n', arguments)));
  TEST_ASSERT_EQUAL_UINT32(1, arguments.count);
  TEST_ASSERT_EQUAL_STRING(maximumLine, arguments.values[0]);

  for (size_t index = 0; index < LINE_BYTES; ++index) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(LineEvent::NONE),
                          static_cast<int>(cli.push('b', arguments)));
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LineEvent::TOO_LONG),
                        static_cast<int>(cli.push('\n', arguments)));
  TEST_ASSERT_EQUAL_UINT32(0, arguments.count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LineEvent::READY),
                        static_cast<int>(pushText(cli, "help\n", arguments)));
  TEST_ASSERT_EQUAL_STRING("help", arguments.values[0]);
}

void test_bounded_cli_rejects_excess_arguments_without_truncation() {
  BoundedCli cli;
  Arguments arguments{};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(LineEvent::READY),
      static_cast<int>(pushText(cli, "a b c d e f g h\n", arguments)));
  TEST_ASSERT_EQUAL_UINT32(MAX_ARGS, arguments.count);
  TEST_ASSERT_EQUAL_STRING("h", arguments.values[7]);

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(LineEvent::TOO_MANY_ARGS),
      static_cast<int>(pushText(cli, "a b c d e f g h i\n", arguments)));
  TEST_ASSERT_EQUAL_UINT32(0, arguments.count);
}

void test_cli_numeric_and_hex_parsers_are_strict_and_transactional() {
  uint32_t value = 77;
  TEST_ASSERT_TRUE(parseDecimal("0", UINT32_MAX, value));
  TEST_ASSERT_EQUAL_UINT32(0, value);
  TEST_ASSERT_TRUE(parseDecimal("4294967295", UINT32_MAX, value));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value);
  TEST_ASSERT_FALSE(parseDecimal("4294967296", UINT32_MAX, value));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value);
  TEST_ASSERT_FALSE(parseDecimal("-1", UINT32_MAX, value));
  TEST_ASSERT_FALSE(parseDecimal("12x", UINT32_MAX, value));
  TEST_ASSERT_FALSE(parseDecimal("0x10", UINT32_MAX, value));
  TEST_ASSERT_TRUE(parseDecimalOrExplicitHex("0x10", UINT32_MAX, value));
  TEST_ASSERT_EQUAL_UINT32(16, value);
  TEST_ASSERT_TRUE(parseDecimalOrExplicitHex("010", UINT32_MAX, value));
  TEST_ASSERT_EQUAL_UINT32(10, value);
  TEST_ASSERT_FALSE(parseDecimalOrExplicitHex("0x", UINT32_MAX, value));
  TEST_ASSERT_FALSE(parseDecimal("9", 3, value));
  TEST_ASSERT_EQUAL_UINT32(10, value);

  uint8_t bytes[8] = {0xA5, 0xA5, 0xA5, 0xA5,
                      0xA5, 0xA5, 0xA5, 0xA5};
  size_t length = 6;
  TEST_ASSERT_TRUE(parseHexBytes("00aF10", bytes, sizeof(bytes), length));
  TEST_ASSERT_EQUAL_UINT32(3, length);
  const uint8_t expected[] = {0x00, 0xAF, 0x10};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bytes, sizeof(expected));

  const uint8_t maximumExpected[] = {0x00, 0x11, 0x22, 0x33,
                                     0x44, 0x55, 0x66, 0x77};
  TEST_ASSERT_TRUE(parseHexBytes("0011223344556677", bytes, sizeof(bytes),
                                 length));
  TEST_ASSERT_EQUAL_UINT32(sizeof(maximumExpected), length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(maximumExpected, bytes,
                               sizeof(maximumExpected));

  uint8_t preserved[8] = {};
  std::memcpy(preserved, bytes, sizeof(bytes));
  length = 3;
  TEST_ASSERT_FALSE(parseHexBytes("123", bytes, sizeof(bytes), length));
  TEST_ASSERT_EQUAL_UINT32(3, length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(preserved, bytes, sizeof(bytes));
  TEST_ASSERT_FALSE(parseHexBytes("GG", bytes, sizeof(bytes), length));
  TEST_ASSERT_FALSE(parseHexBytes("001122334455667788", bytes, sizeof(bytes),
                                  length));
}

void test_command_dispatch_checks_name_arity_and_confirmation_before_action() {
  CatalogCalls catalogCalls{};
  TEST_ASSERT_EQUAL_UINT32(
      COMMAND_CATALOG_COUNT,
      sizeof(ALL_SINGLE_COMMANDS) / sizeof(ALL_SINGLE_COMMANDS[0]));
  for (size_t index = 0; index < COMMAND_CATALOG_COUNT; ++index) {
    const CommandSpec* const spec = commandSpec(ALL_SINGLE_COMMANDS[index].id);
    TEST_ASSERT_NOT_NULL(spec);
    Arguments catalogArguments{};
    catalogArguments.count =
        static_cast<size_t>(spec->singleArgumentCount) + 1U;
    catalogArguments.values[0] = spec->name;
    for (size_t argument = 1; argument < catalogArguments.count; ++argument) {
      catalogArguments.values[argument] = "0";
    }
    if (spec->id == CommandId::WRITE_PAGE) {
      catalogArguments.values[2] = "AA";
      catalogArguments.values[3] = EEPROM_CONFIRMATION;
    }
    const DispatchResult catalogResult =
        dispatch(ALL_SINGLE_COMMANDS, COMMAND_CATALOG_COUNT, catalogArguments,
                 &catalogCalls);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchCode::HANDLED),
                          static_cast<int>(catalogResult.code));
    TEST_ASSERT_EQUAL_UINT32(1, catalogCalls.count[index]);
  }

  Calls calls{};
  Arguments arguments{};

  const char* help[] = {"help"};
  arguments.count = 1;
  arguments.values[0] = help[0];
  DispatchResult result =
      dispatch(TEST_COMMANDS, 3, arguments, &calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchCode::HANDLED),
                        static_cast<int>(result.code));
  TEST_ASSERT_EQUAL_UINT32(1, calls.help);

  const char* unknown[] = {"missing"};
  arguments = {};
  arguments.count = 1;
  arguments.values[0] = unknown[0];
  result = dispatch(TEST_COMMANDS, 3, arguments, &calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchCode::UNKNOWN_COMMAND),
                        static_cast<int>(result.code));

  const char* shortRead[] = {"read-eeprom", "0"};
  arguments = {};
  arguments.count = 2;
  arguments.values[0] = shortRead[0];
  arguments.values[1] = shortRead[1];
  result = dispatch(TEST_COMMANDS, 3, arguments, &calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchCode::WRONG_ARITY),
                        static_cast<int>(result.code));
  TEST_ASSERT_EQUAL_UINT32(0, calls.read);

  const char* rejectedWrite[] = {"write-page", "0", "AA", "confirm_eeprom_overwrite"};
  arguments = {};
  arguments.count = 4;
  for (size_t index = 0; index < arguments.count; ++index) {
    arguments.values[index] = rejectedWrite[index];
  }
  result = dispatch(TEST_COMMANDS, 3, arguments, &calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchCode::CONFIRMATION_REQUIRED),
                        static_cast<int>(result.code));
  TEST_ASSERT_EQUAL_UINT32(0, calls.write);

  const char* malformedWrite[] = {"write-page", "7", "AABB",
                                  EEPROM_CONFIRMATION};
  for (size_t index = 0; index < arguments.count; ++index) {
    arguments.values[index] = malformedWrite[index];
  }
  result = dispatch(TEST_COMMANDS, 3, arguments, &calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchCode::HANDLED),
                        static_cast<int>(result.code));
  TEST_ASSERT_EQUAL_UINT32(0, calls.write);

  const char* acceptedWrite[] = {"write-page", "0x08", "AABB",
                                 EEPROM_CONFIRMATION};
  for (size_t index = 0; index < arguments.count; ++index) {
    arguments.values[index] = acceptedWrite[index];
  }
  result = dispatch(TEST_COMMANDS, 3, arguments, &calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchCode::HANDLED),
                        static_cast<int>(result.code));
  TEST_ASSERT_EQUAL_UINT32(1, calls.write);
}
