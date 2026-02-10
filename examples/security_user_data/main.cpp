#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"

AT21CS::Driver gDevice;

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                              - show commands");
  Serial.println("  read <addr> <len>                 - security register read");
  Serial.println("  write_byte <addr> <value>         - write user area (0x10..0x1F)");
  Serial.println("  write_page <addr> <v0> [..v7]     - user area page write");
  Serial.println("  lock_state                        - check permanent security lock");
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

  Serial.println("\n=== security_user_data ===");

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
  } else if (tokens[0] == "read" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 32) {
      Serial.println("Invalid args: read <addr> <len 1..32>");
    } else {
      uint8_t data[32] = {0};
      const AT21CS::Status st = gDevice.readSecurity(addr, data, len);
      ex::printStatus(st);
      Serial.print("data=");
      printBytes(data, len);
    }
  } else if (tokens[0] == "write_byte" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t value = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], value)) {
      Serial.println("Invalid args: write_byte <addr> <value>");
    } else {
      const AT21CS::Status st = gDevice.writeSecurityUserByte(addr, value);
      ex::printStatus(st);
    }
  } else if (tokens[0] == "write_page" && argc >= 3) {
    uint8_t addr = 0;
    if (!ex::parseU8(tokens[1], addr)) {
      Serial.println("Invalid address");
    } else {
      uint8_t data[8] = {0};
      size_t len = 0;
      for (int i = 2; i < argc && len < 8; ++i) {
        uint8_t value = 0;
        if (!ex::parseU8(tokens[i], value)) {
          len = 0;
          break;
        }
        data[len++] = value;
      }
      if (len == 0) {
        Serial.println("Invalid data bytes");
      } else {
        const AT21CS::Status st = gDevice.writeSecurityUserPage(addr, data, len);
        ex::printStatus(st);
      }
    }
  } else if (tokens[0] == "lock_state") {
    bool locked = false;
    const AT21CS::Status st = gDevice.isSecurityLocked(locked);
    ex::printStatus(st);
    Serial.printf("locked=%s\n", locked ? "true" : "false");
  } else if (tokens[0] == "health") {
    ex::printHealth(gDevice);
  } else {
    Serial.printf("Unknown command: %s\n", line.c_str());
  }

  Serial.print("> ");
}
