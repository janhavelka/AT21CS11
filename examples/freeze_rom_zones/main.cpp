#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"

AT21CS::Driver gDevice;

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                              - show commands");
  Serial.println("  is_frozen                         - check freeze state (opcode 1h read)");
  Serial.println("  freeze                            - execute freeze command (0x55/0xAA)");
  Serial.println("  set_rom <0..3>                    - set a ROM zone after freeze test");
  Serial.println("  read_zone <0..3>                  - read ROM zone register");
  Serial.println("  health                            - print health counters/state");
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== freeze_rom_zones ===");

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

  String tokens[8];
  const int argc = ex::splitTokens(line, tokens, 8);
  if (argc == 0) {
    Serial.print("> ");
    return;
  }

  if (tokens[0] == "help") {
    printHelp();
  } else if (tokens[0] == "is_frozen") {
    bool frozen = false;
    const AT21CS::Status st = gDevice.areRomZonesFrozen(frozen);
    ex::printStatus(st);
    Serial.printf("frozen=%s\n", frozen ? "true" : "false");
  } else if (tokens[0] == "freeze") {
    const AT21CS::Status st = gDevice.freezeRomZones();
    ex::printStatus(st);
  } else if (tokens[0] == "set_rom" && argc >= 2) {
    uint8_t zone = 0;
    if (!ex::parseU8(tokens[1], zone)) {
      Serial.println("Invalid zone index");
    } else {
      const AT21CS::Status st = gDevice.setZoneRom(zone);
      ex::printStatus(st);
      Serial.println("Expected after freeze: command should fail/NACK");
    }
  } else if (tokens[0] == "read_zone" && argc >= 2) {
    uint8_t zone = 0;
    if (!ex::parseU8(tokens[1], zone)) {
      Serial.println("Invalid zone index");
    } else {
      uint8_t value = 0;
      const AT21CS::Status st = gDevice.readRomZoneRegister(zone, value);
      ex::printStatus(st);
      Serial.printf("zone=%u register=0x%02X\n", zone, value);
    }
  } else if (tokens[0] == "health") {
    ex::printHealth(gDevice);
  } else {
    Serial.printf("Unknown command: %s\n", line.c_str());
  }

  Serial.print("> ");
}
