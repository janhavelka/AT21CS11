#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "BringupCli.h"
#include "support/TestAccess.h"

namespace {

using namespace at21cs_example;
using AT21CS::test::TestAccess;

// Literal bytes emitted by the original Arduino example, including its mix of
// println (CRLF) and printf (LF) line endings.
constexpr char HELP[] =
    "Commands:\r\n"
    "  help\n"
    "  status\n"
    "  presence\n"
    "  probe\n"
    "  manufacturer\n"
    "  serial\n"
    "  read-eeprom <address> <length>\n"
    "  read-security <address> <length>\n"
    "  security-locked\n"
    "  rom-zone <0..3>\n"
    "  speed <high|standard>\n"
    "  recover\n"
    "  write-page <address> <2..16 hexadecimal digits> CONFIRM_EEPROM_OVERWRITE\n"
    "  shutdown\n";

class RecordingOutput : public CliOutput {
 public:
  void write(const char* data, size_t length) override {
    bytes.append(data, length);
  }

  std::string bytes;
};

const char* input = "";

int readChar() {
  return *input == '\0' ? -1 : static_cast<unsigned char>(*input++);
}

uint32_t nowMs() { return 10; }

struct Console {
  RecordingOutput output;
  WireInstance wire;
  BringupCli cli{wire, output, nowMs, readChar};

  void command(const char* text) {
    input = text;
    output.bytes.clear();
    cli.pollInput();
    cli.step();
  }
};

void expectBytes(const std::string& expected, const RecordingOutput& output,
                 const char* context = "") {
  TEST_ASSERT_EQUAL_size_t_MESSAGE(expected.size(), output.bytes.size(), context);
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected.data(), output.bytes.data(),
                                   expected.size(), context);
}

}  // namespace

void test_bringup_help_dispatch_and_confirmation_output() {
  Console console;
  console.command("help\r\n");
  expectBytes(HELP, console.output);
  // Consume the LF left after the CR completed the command.
  console.cli.pollInput();
  console.cli.step();
  expectBytes(HELP, console.output);

  struct Case { const char* command; const char* reply; };
  const Case cases[] = {
      {"missing\n", "unknown command; use help\r\n"},
      {"read-eeprom 0\n", "usage: read-eeprom <address> <length>\n"},
      {"write-page 0 AA\n",
       "usage: write-page <address> <2..16 hexadecimal digits> CONFIRM_EEPROM_OVERWRITE\n"},
      {"write-page 0 AA confirm\n",
       "write rejected; final token must be exactly CONFIRM_EEPROM_OVERWRITE\n"},
      {"a b c d e f g h i\n", "input rejected: too many arguments\r\n"},
      {"presence\n", "presence: inactive\r\n"},
  };
  for (const Case& item : cases) {
    console.command(item.command);
    expectBytes(item.reply, console.output, item.command);
  }
}

void test_bringup_poll_budget_and_overlong_line_recovery() {
  Console console;
  const std::string text = std::string(LINE_BYTES, 'a') + "\nhelp\n";
  input = text.c_str();
  console.cli.pollInput();
  TEST_ASSERT_EQUAL_STRING("\nhelp\n", input);
  TEST_ASSERT_TRUE(console.output.bytes.empty());
  console.cli.pollInput();
  expectBytes("input rejected: line too long\r\n", console.output);
  console.output.bytes.clear();
  console.cli.pollInput();
  console.cli.step();
  expectBytes(HELP, console.output);
}

void test_bringup_invalid_arguments_are_rejected_before_wire_activity() {
  Console console;
  auto& backend = console.wire.backend;
  // A discovery response, a manufacturer address ACK, then ID 0x00D380.
  for (bool high : {false, true, true, true, false}) {
    TEST_ASSERT_TRUE(TestAccess::queueLineLevel(backend, high));
  }
  for (uint32_t bit = 1U << 23; bit != 0; bit >>= 1) {
    TEST_ASSERT_TRUE(TestAccess::queueLineLevel(backend, (0x00D380U & bit) != 0));
  }
  TEST_ASSERT_TRUE(TestAccess::queueLineLevel(backend, true));
  console.cli.start();
  TEST_ASSERT_TRUE(console.wire.driver.isInitialized());
  // Isolate command traffic from the independently scheduled hot-plug work.
  console.wire.policy().stop();
  const uint16_t before = TestAccess::lineEventCount(backend);

  constexpr char eeprom[] = "invalid EEPROM range; use address 0..127 and length 1..32\r\n";
  constexpr char security[] = "invalid Security range; use address 0..31 and length 1..32\r\n";
  constexpr char page[] = "invalid page write: use one page, 1..8 bytes, exact hex\r\n";
  struct Case { const char* command; const char* reply; };
  const Case cases[] = {
      {"read-eeprom 128 1\n", eeprom}, {"read-eeprom 0 0\n", eeprom},
      {"read-eeprom bad 1\n", eeprom}, {"read-eeprom 127 2\n", eeprom},
      {"read-eeprom 0 33\n", eeprom}, {"read-eeprom 0 0x1\n", eeprom},
      {"read-security 32 1\n", security}, {"read-security 0 0\n", security},
      {"read-security 31 2\n", security}, {"read-security 0 33\n", security},
      {"rom-zone 4\n", "invalid ROM zone; use 0..3\r\n"},
      {"speed fastest\n", "invalid speed; use high or standard\r\n"},
      {"write-page 128 AA CONFIRM_EEPROM_OVERWRITE\n", page},
      {"write-page 0 ABC CONFIRM_EEPROM_OVERWRITE\n", page},
      {"write-page 0 GG CONFIRM_EEPROM_OVERWRITE\n", page},
      {"write-page 0 001122334455667788 CONFIRM_EEPROM_OVERWRITE\n", page},
      {"write-page 7 AABB CONFIRM_EEPROM_OVERWRITE\n", page},
      {"write-page 127 AABB CONFIRM_EEPROM_OVERWRITE\n", page},
      {"write-page 0 AA wrong\n",
       "write rejected; final token must be exactly CONFIRM_EEPROM_OVERWRITE\n"},
  };
  for (const Case& item : cases) {
    console.command(item.command);
    expectBytes(item.reply, console.output, item.command);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(before, TestAccess::lineEventCount(backend),
                                     item.command);
  }
}

void test_bringup_absent_startup_presence_and_shutdown_output() {
  Console console;
  console.cli.start();
  expectBytes(std::string("startup: NOT_PRESENT detail=0 phase=NONE index=0 msg=NOT_PRESENT\n"
                          "device absent; binding retained for recover\r\n") + HELP,
              console.output);
  TEST_ASSERT_TRUE(console.wire.driver.isBound());
  console.command("presence\n");
  expectBytes("presence: disabled\r\n", console.output);
  console.command("shutdown\n");
  // Automatic work gets its turn while the second command remains pending.
  expectBytes("[A automatic recover] NOT_PRESENT detail=0\n", console.output);
  console.output.bytes.clear();
  console.cli.step();
  expectBytes("shutdown: OK detail=0 phase=NONE index=0 msg=OK\n", console.output);
  TEST_ASSERT_TRUE(console.wire.backend.isInitialized());
  console.output.bytes.clear();
  console.cli.step();
  expectBytes("shutdown: backend released\r\n", console.output);
  TEST_ASSERT_FALSE(console.wire.backend.isInitialized());
}

void test_multi_wire_status_output_preserves_byte_and_number_formats() {
  Console console;
  console.cli.start();
  console.cli.step();
  console.output.bytes.clear();
  // The multi-device example uses the shared formatter for either wire.
  printInstanceStatus(console.output, "B", console.wire);
  expectBytes("[B] active=yes bus=bound driver=bound initialized=no state=OFFLINE "
              "address=0 failures=2 automatic-blocked=no recovery-pending=yes\n"
              "[B] expected=AT21CS11 detected=UNKNOWN manufacturer=0x000000 revision=0 "
              "speed=HIGH_SPEED known=no generation=2\n"
              "[B] identity-known=no current=no presence=disabled\r\n"
              "[B] last-automatic=recover status=NOT_PRESENT detail=0\n", console.output);
  console.output.bytes.clear();
  TestAccess::seedGeneration(console.wire.bus, UINT64_C(4294967303));
  printInstanceStatus(console.output, "B", console.wire);
  TEST_ASSERT_TRUE(console.output.bytes.find("generation=4294967303\n") != std::string::npos);
  console.output.bytes.clear();
  const uint8_t data[] = {0x00, 0x0A, 0xFF};
  printBytes(console.output, data, sizeof(data));
  expectBytes("00 0A FF\r\n", console.output);
}

void test_cli_output_accepts_exact_fit_and_reports_truncation() {
  RecordingOutput output;
  const std::string exact(CliOutput::FORMAT_BYTES - 1, 'x');
  TEST_ASSERT_TRUE(output.printf("%s", exact.c_str()));
  expectBytes(exact, output);
  output.bytes.clear();
  TEST_ASSERT_FALSE(output.printf("%s!", exact.c_str()));
  expectBytes("output truncated\r\n", output);
}
