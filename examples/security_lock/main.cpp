#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"

AT21CS::Driver gDevice;

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                              - show commands");
  Serial.println("  is_locked                         - check security lock state");
  Serial.println("  lock                              - execute permanent lock command");
  Serial.println("  write_test <addr> <value>         - user write test (expect NACK_DATA when locked)");
  Serial.println("  read <addr> <len>                 - read security bytes");
  Serial.println("  health                            - print health counters/state");
}

void printBytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    Serial.printf("%02X ", data[i]);
  }
  Serial.println();
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== security_lock ===");

  AT21CS::Config cfg;
  cfg.sioPin = board::SIO_PRIMARY;
  cfg.presencePin = board::PRESENCE_PRIMARY;
  cfg.addressBits = board::ADDRESS_BITS_PRIMARY;

  const AT21CS::Status st = gDevice.begin(cfg);
  ex::printStatus(st);

  printHelp();
  Serial.print("> ");
}

void loop() {
  gDevice.tick(millis());

  String line;
  if (!ex::readLine(line)) {
    return;
  }

  String tokens[12];
  const int argc = ex::splitTokens(line, tokens, 12);
  if (argc == 0) {
    Serial.print("> ");
    return;
  }

  if (tokens[0] == "help") {
    printHelp();
  } else if (tokens[0] == "is_locked") {
    bool locked = false;
    const AT21CS::Status st = gDevice.isSecurityLocked(locked);
    ex::printStatus(st);
    Serial.printf("locked=%s\n", locked ? "true" : "false");
  } else if (tokens[0] == "lock") {
    const AT21CS::Status st = gDevice.lockSecurityRegister();
    ex::printStatus(st);
  } else if (tokens[0] == "write_test" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t value = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], value)) {
      Serial.println("Invalid args: write_test <addr> <value>");
    } else {
      const AT21CS::Status st = gDevice.writeSecurityUserByte(addr, value);
      ex::printStatus(st);
      Serial.println("Expected after lock: NACK_DATA on the data byte");
    }
  } else if (tokens[0] == "read" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 32) {
      Serial.println("Invalid args: read <addr> <len>");
    } else {
      uint8_t data[32] = {0};
      const AT21CS::Status st = gDevice.readSecurity(addr, data, len);
      ex::printStatus(st);
      Serial.print("data=");
      printBytes(data, len);
    }
  } else if (tokens[0] == "health") {
    ex::printHealth(gDevice);
  } else {
    Serial.printf("Unknown command: %s\n", line.c_str());
  }

  Serial.print("> ");
}
