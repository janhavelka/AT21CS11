#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"

AT21CS::Driver gDevice;

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                              - show commands");
  Serial.println("  read_zone <0..3>                  - read ROM zone register");
  Serial.println("  is_rom <0..3>                     - check ROM/EERPOM zone state");
  Serial.println("  set_rom <0..3>                    - set zone to ROM");
  Serial.println("  frozen                            - check if ROM zones are frozen");
  Serial.println("  freeze                            - freeze ROM zone configuration");
  Serial.println("  write_test <addr> <value>         - EEPROM write test for ROM-zone NACK");
  Serial.println("  health                            - print health counters/state");
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== 03_rom_freeze_cli ===");

  AT21CS::Config cfg;
  cfg.sioPin = board::SIO_PRIMARY;
  cfg.presencePin = board::PRESENCE_PRIMARY;
  cfg.addressBits = board::ADDRESS_BITS_PRIMARY;

  ex::printStatus(gDevice.begin(cfg));

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
  } else if (tokens[0] == "read_zone" && argc >= 2) {
    uint8_t zone = 0;
    if (!ex::parseU8(tokens[1], zone)) {
      Serial.println("Usage: read_zone <0..3>");
    } else {
      uint8_t value = 0;
      ex::printStatus(gDevice.readRomZoneRegister(zone, value));
      Serial.printf("zone=%u register=0x%02X\n", zone, value);
    }
  } else if (tokens[0] == "is_rom" && argc >= 2) {
    uint8_t zone = 0;
    if (!ex::parseU8(tokens[1], zone)) {
      Serial.println("Usage: is_rom <0..3>");
    } else {
      bool isRom = false;
      ex::printStatus(gDevice.isZoneRom(zone, isRom));
      Serial.printf("zone=%u isRom=%s\n", zone, isRom ? "true" : "false");
    }
  } else if (tokens[0] == "set_rom" && argc >= 2) {
    uint8_t zone = 0;
    if (!ex::parseU8(tokens[1], zone)) {
      Serial.println("Usage: set_rom <0..3>");
    } else {
      ex::printStatus(gDevice.setZoneRom(zone));
    }
  } else if (tokens[0] == "frozen") {
    bool frozen = false;
    ex::printStatus(gDevice.areRomZonesFrozen(frozen));
    Serial.printf("frozen=%s\n", frozen ? "true" : "false");
  } else if (tokens[0] == "freeze") {
    ex::printStatus(gDevice.freezeRomZones());
  } else if (tokens[0] == "write_test" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t value = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], value)) {
      Serial.println("Usage: write_test <addr> <value>");
    } else {
      ex::printStatus(gDevice.writeEepromByte(addr, value));
      Serial.println("Expected on ROM address: NACK_DATA and no write cycle");
    }
  } else if (tokens[0] == "health") {
    ex::printHealth(gDevice);
  } else {
    Serial.printf("Unknown command: %s\n", line.c_str());
  }

  Serial.print("> ");
}
