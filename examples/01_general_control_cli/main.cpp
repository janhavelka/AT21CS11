#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"
#include "../common/LoadCellMap.h"

AT21CS::Driver gDevice;

void printBytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    Serial.printf("%02X ", data[i]);
  }
  Serial.println();
}

const char* sourceToStr(lcmap::CalibrationSource source) {
  switch (source) {
    case lcmap::CalibrationSource::MASTER:
      return "MASTER";
    case lcmap::CalibrationSource::MIRROR:
      return "MIRROR";
    default:
      return "NONE";
  }
}

void printLoadCellLayout() {
  Serial.println("LoadCellMap layout:");
  Serial.printf("  Security identity: addr=0x%02X size=%u\n", lcmap::SECURITY_IDENTITY_ADDR,
                static_cast<unsigned>(sizeof(lcmap::SecurityIdentityV1)));
  Serial.printf("  Calibration master: addr=0x%02X size=%u\n", lcmap::CALIBRATION_MASTER_ADDR,
                static_cast<unsigned>(sizeof(lcmap::CalibrationBlockV1)));
  Serial.printf("  Calibration mirror: addr=0x%02X size=%u\n", lcmap::CALIBRATION_MIRROR_ADDR,
                static_cast<unsigned>(sizeof(lcmap::CalibrationBlockV1)));
  Serial.printf("  Runtime block:      addr=0x%02X size=%u\n", lcmap::RUNTIME_ADDR,
                static_cast<unsigned>(sizeof(lcmap::RuntimeBlockV1)));
  Serial.printf("  Counter block:      addr=0x%02X size=%u\n", lcmap::COUNTERS_ADDR,
                static_cast<unsigned>(sizeof(lcmap::CounterBlockV1)));

  Serial.println("Key field addresses:");
  Serial.printf("  capacityGrams      @ 0x%02X\n", lcmap::field::CAPACITY_GRAMS);
  Serial.printf("  zeroBalanceRaw     @ 0x%02X\n", lcmap::field::ZERO_BALANCE_RAW);
  Serial.printf("  spanRawAtCapacity  @ 0x%02X\n", lcmap::field::SPAN_RAW_AT_CAPACITY);
  Serial.printf("  installTareRaw     @ 0x%02X\n", lcmap::field::INSTALL_TARE_RAW);
  Serial.printf("  overloadCount      @ 0x%02X\n", lcmap::field::OVERLOAD_COUNT);
}

void printLoadCellRecords() {
  lcmap::SecurityIdentityV1 identity = {};
  bool identityValid = false;
  ex::printStatus(lcmap::readSecurityIdentity(gDevice, identity, identityValid));
  Serial.printf(
      "securityIdentity valid=%s hwRev=%u modelId=%u moduleSerial=%lu batch=%u flags=0x%04X\n",
      identityValid ? "true" : "false", static_cast<unsigned>(identity.hwRevision),
      static_cast<unsigned>(identity.modelId), static_cast<unsigned long>(identity.moduleSerial),
      static_cast<unsigned>(identity.batchCode), static_cast<unsigned>(identity.flags));

  lcmap::CalibrationBlockV1 calibration = {};
  lcmap::CalibrationSource source = lcmap::CalibrationSource::NONE;
  bool calibrationValid = false;
  ex::printStatus(lcmap::readCalibrationBest(gDevice, calibration, source, calibrationValid));
  Serial.printf(
      "calibration valid=%s source=%s capacityGrams=%lu zeroRaw=%ld spanRaw=%ld sens=%ld "
      "tempCoeff=%d linearity=%d flags=0x%04X\n",
      calibrationValid ? "true" : "false", sourceToStr(source),
      static_cast<unsigned long>(calibration.capacityGrams),
      static_cast<long>(calibration.zeroBalanceRaw),
      static_cast<long>(calibration.spanRawAtCapacity),
      static_cast<long>(calibration.sensitivityNvPerV), static_cast<int>(calibration.tempCoeffPpmPerC),
      static_cast<int>(calibration.linearityPpm), static_cast<unsigned>(calibration.flags));

  lcmap::RuntimeBlockV1 runtime = {};
  bool runtimeValid = false;
  ex::printStatus(lcmap::readRuntime(gDevice, runtime, runtimeValid));
  Serial.printf(
      "runtime valid=%s seq=%lu tare=%ld zeroTrim=%ld spanTrimPpm=%ld filter=%u diag=%u "
      "flags=0x%04X\n",
      runtimeValid ? "true" : "false", static_cast<unsigned long>(runtime.seq),
      static_cast<long>(runtime.installTareRaw), static_cast<long>(runtime.userZeroTrimRaw),
      static_cast<long>(runtime.userSpanTrimPpm), static_cast<unsigned>(runtime.filterProfile),
      static_cast<unsigned>(runtime.diagnosticsMode), static_cast<unsigned>(runtime.flags));

  lcmap::CounterBlockV1 counters = {};
  bool countersValid = false;
  ex::printStatus(lcmap::readCounters(gDevice, counters, countersValid));
  Serial.printf(
      "counters valid=%s seq=%lu overload=%lu overTemp=%lu powerCycles=%lu saturation=%lu "
      "flags=0x%04X\n",
      countersValid ? "true" : "false", static_cast<unsigned long>(counters.seq),
      static_cast<unsigned long>(counters.overloadCount),
      static_cast<unsigned long>(counters.overTempCount),
      static_cast<unsigned long>(counters.powerCycleCount),
      static_cast<unsigned long>(counters.saturationCount), static_cast<unsigned>(counters.flags));
}

void writeLoadCellDemoData() {
  lcmap::SecurityIdentityV1 identity = {};
  identity.hwRevision = 1;
  identity.modelId = 1101;
  identity.moduleSerial = 1000001;
  identity.batchCode = 2602;
  identity.flags = 0x0001;
  ex::printStatus(lcmap::writeSecurityIdentity(gDevice, identity));

  lcmap::CalibrationBlockV1 calibration = {};
  calibration.flags = 0x0001;
  calibration.capacityGrams = 50000;
  calibration.zeroBalanceRaw = -17320;
  calibration.spanRawAtCapacity = 947112;
  calibration.sensitivityNvPerV = 2000000;
  calibration.tempCoeffPpmPerC = -35;
  calibration.linearityPpm = 120;
  ex::printStatus(lcmap::writeCalibrationBoth(gDevice, calibration));

  lcmap::RuntimeBlockV1 runtime = {};
  runtime.flags = 0x0001;
  runtime.seq = 1;
  runtime.installTareRaw = -120;
  runtime.userZeroTrimRaw = 0;
  runtime.userSpanTrimPpm = 0;
  runtime.filterProfile = 2;
  runtime.diagnosticsMode = 0;
  ex::printStatus(lcmap::writeRuntime(gDevice, runtime));

  lcmap::CounterBlockV1 counters = {};
  counters.flags = 0x0001;
  counters.seq = 1;
  counters.overloadCount = 0;
  counters.overTempCount = 0;
  counters.powerCycleCount = 1;
  counters.saturationCount = 0;
  ex::printStatus(lcmap::writeCounters(gDevice, counters));
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                                 - show commands");
  Serial.println("  present                              - run presence check");
  Serial.println("  reset                                - reset + discovery");
  Serial.println("  probe                                - probe / detect device");
  Serial.println("  recover                              - manual recovery");
  Serial.println("  high                                 - set high speed");
  Serial.println("  std                                  - set standard speed (AT21CS01 only)");
  Serial.println("  is_high                              - check high speed mode");
  Serial.println("  is_std                               - check standard speed mode");
  Serial.println("  part                                 - print detected part");
  Serial.println("  id                                   - read manufacturer ID");
  Serial.println("  current                              - current address read");
  Serial.println("  e_read <addr> <len>                  - EEPROM read");
  Serial.println("  e_write <addr> <value>               - EEPROM byte write");
  Serial.println("  e_page <addr> <v0> [..v7]            - EEPROM page write");
  Serial.println("  s_read <addr> <len>                  - Security read");
  Serial.println("  s_write <addr> <value>               - Security user byte write");
  Serial.println("  s_page <addr> <v0> [..v7]            - Security user page write");
  Serial.println("  s_locked                             - check security lock");
  Serial.println("  s_lock                               - lock security register");
  Serial.println("  serial                               - read serial + CRC");
  Serial.println("  read_zone <0..3>                     - read ROM zone register");
  Serial.println("  is_rom <0..3>                        - check zone ROM/EEPROM state");
  Serial.println("  set_rom <0..3>                       - set zone to ROM");
  Serial.println("  frozen                               - check if ROM zones are frozen");
  Serial.println("  freeze                               - freeze ROM zone configuration");
  Serial.println("  lc_layout                            - print full load-cell map layout");
  Serial.println("  lc_write_demo                        - write demo LoadCellMap records");
  Serial.println("  lc_read                              - read and validate LoadCellMap records");
  Serial.println("  lc_set_tare <signed_raw>             - update runtime tare field");
  Serial.println("  lc_inc_overload [count]              - increment overload counter");
  Serial.println("  lc_fwrite <addr> <float>             - write float32 to EEPROM via map helper");
  Serial.println("  lc_fread <addr>                      - read float32 from EEPROM via map helper");
  Serial.println("  wait [timeout_ms]                    - waitReady polling");
  Serial.println("  health                               - print health counters/state");
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== 01_general_control_cli ===");
  Serial.printf("SI/O=%d presencePin=%d A2:A0=%u\n", board::SIO_PRIMARY,
                board::PRESENCE_PRIMARY, board::ADDRESS_BITS_PRIMARY);

  AT21CS::Config cfg;
  cfg.sioPin = board::SIO_PRIMARY;
  cfg.presencePin = board::PRESENCE_PRIMARY;
  cfg.addressBits = board::ADDRESS_BITS_PRIMARY;

  const AT21CS::Status st = gDevice.begin(cfg);
  ex::printStatus(st);
  Serial.printf("detectedPart=%s speed=%s\n", ex::partToStr(gDevice.detectedPart()),
                ex::speedToStr(gDevice.speedMode()));

  printHelp();
  Serial.print("> ");
}

void loop() {
  gDevice.tick(millis());

  String line;
  if (!ex::readLine(line)) {
    return;
  }

  String tokens[20];
  const int argc = ex::splitTokens(line, tokens, 20);
  if (argc == 0) {
    Serial.print("> ");
    return;
  }

  if (tokens[0] == "help" || tokens[0] == "?") {
    printHelp();
  } else if (tokens[0] == "present") {
    bool present = false;
    ex::printStatus(gDevice.isPresent(present));
    Serial.printf("present=%s\n", present ? "true" : "false");
  } else if (tokens[0] == "reset") {
    ex::printStatus(gDevice.resetAndDiscover());
  } else if (tokens[0] == "probe") {
    ex::printStatus(gDevice.probe());
  } else if (tokens[0] == "recover") {
    ex::printStatus(gDevice.recover());
  } else if (tokens[0] == "high") {
    ex::printStatus(gDevice.setHighSpeed());
    Serial.printf("speed=%s\n", ex::speedToStr(gDevice.speedMode()));
  } else if (tokens[0] == "std") {
    ex::printStatus(gDevice.setStandardSpeed());
    Serial.printf("speed=%s\n", ex::speedToStr(gDevice.speedMode()));
  } else if (tokens[0] == "is_high") {
    bool enabled = false;
    ex::printStatus(gDevice.isHighSpeed(enabled));
    Serial.printf("isHighSpeed=%s\n", enabled ? "true" : "false");
  } else if (tokens[0] == "is_std") {
    bool enabled = false;
    ex::printStatus(gDevice.isStandardSpeed(enabled));
    Serial.printf("isStandardSpeed=%s\n", enabled ? "true" : "false");
  } else if (tokens[0] == "part") {
    Serial.printf("part=%s\n", ex::partToStr(gDevice.detectedPart()));
  } else if (tokens[0] == "id") {
    uint32_t id = 0;
    ex::printStatus(gDevice.readManufacturerId(id));
    Serial.printf("manufacturerId=0x%06lX\n", static_cast<unsigned long>(id));
  } else if (tokens[0] == "current") {
    uint8_t value = 0;
    ex::printStatus(gDevice.readCurrentAddress(value));
    Serial.printf("current=0x%02X\n", value);
  } else if (tokens[0] == "e_read" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 32) {
      Serial.println("Usage: e_read <addr> <len 1..32>");
    } else {
      uint8_t data[32] = {0};
      ex::printStatus(gDevice.readEeprom(addr, data, len));
      Serial.print("data=");
      printBytes(data, len);
    }
  } else if (tokens[0] == "e_write" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t value = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], value)) {
      Serial.println("Usage: e_write <addr> <value>");
    } else {
      ex::printStatus(gDevice.writeEepromByte(addr, value));
    }
  } else if (tokens[0] == "e_page" && argc >= 3) {
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
        Serial.println("Usage: e_page <addr> <v0> [..v7]");
      } else {
        ex::printStatus(gDevice.writeEepromPage(addr, data, len));
      }
    }
  } else if (tokens[0] == "s_read" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 32) {
      Serial.println("Usage: s_read <addr> <len 1..32>");
    } else {
      uint8_t data[32] = {0};
      ex::printStatus(gDevice.readSecurity(addr, data, len));
      Serial.print("data=");
      printBytes(data, len);
    }
  } else if (tokens[0] == "s_write" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t value = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], value)) {
      Serial.println("Usage: s_write <addr> <value>");
    } else {
      ex::printStatus(gDevice.writeSecurityUserByte(addr, value));
    }
  } else if (tokens[0] == "s_page" && argc >= 3) {
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
        Serial.println("Usage: s_page <addr> <v0> [..v7]");
      } else {
        ex::printStatus(gDevice.writeSecurityUserPage(addr, data, len));
      }
    }
  } else if (tokens[0] == "s_locked") {
    bool locked = false;
    ex::printStatus(gDevice.isSecurityLocked(locked));
    Serial.printf("locked=%s\n", locked ? "true" : "false");
  } else if (tokens[0] == "s_lock") {
    ex::printStatus(gDevice.lockSecurityRegister());
  } else if (tokens[0] == "serial") {
    AT21CS::SerialNumberInfo sn = {};
    ex::printStatus(gDevice.readSerialNumber(sn));
    Serial.print("serial=");
    printBytes(sn.bytes, AT21CS::cmd::SECURITY_SERIAL_SIZE);
    Serial.printf("productIdOk=%s crcOk=%s\n", sn.productIdOk ? "true" : "false",
                  sn.crcOk ? "true" : "false");
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
  } else if (tokens[0] == "lc_layout") {
    printLoadCellLayout();
  } else if (tokens[0] == "lc_write_demo") {
    writeLoadCellDemoData();
  } else if (tokens[0] == "lc_read") {
    printLoadCellRecords();
  } else if (tokens[0] == "lc_set_tare" && argc >= 2) {
    int32_t tareRaw = 0;
    if (!ex::parseI32(tokens[1], tareRaw)) {
      Serial.println("Usage: lc_set_tare <signed_raw>");
    } else {
      lcmap::RuntimeBlockV1 runtime = {};
      bool valid = false;
      const AT21CS::Status readSt = lcmap::readRuntime(gDevice, runtime, valid);
      ex::printStatus(readSt);
      if (readSt.ok()) {
        if (!valid) {
          runtime = {};
          runtime.flags = 0x0001;
          runtime.filterProfile = 2;
        }
        runtime.seq += 1;
        runtime.installTareRaw = tareRaw;
        ex::printStatus(lcmap::writeRuntime(gDevice, runtime));
      }
    }
  } else if (tokens[0] == "lc_inc_overload") {
    uint32_t increment = 1;
    if (argc >= 2 && !ex::parseU32(tokens[1], increment)) {
      Serial.println("Usage: lc_inc_overload [count]");
    } else {
      lcmap::CounterBlockV1 counters = {};
      bool valid = false;
      const AT21CS::Status readSt = lcmap::readCounters(gDevice, counters, valid);
      ex::printStatus(readSt);
      if (readSt.ok()) {
        if (!valid) {
          counters = {};
          counters.flags = 0x0001;
        }
        counters.seq += 1;
        counters.overloadCount += increment;
        ex::printStatus(lcmap::writeCounters(gDevice, counters));
      }
    }
  } else if (tokens[0] == "lc_fwrite" && argc >= 3) {
    uint8_t addr = 0;
    float value = 0.0F;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseF32(tokens[2], value)) {
      Serial.println("Usage: lc_fwrite <addr> <float>");
    } else {
      ex::printStatus(lcmap::writeFloat32(gDevice, addr, value));
    }
  } else if (tokens[0] == "lc_fread" && argc >= 2) {
    uint8_t addr = 0;
    if (!ex::parseU8(tokens[1], addr)) {
      Serial.println("Usage: lc_fread <addr>");
    } else {
      float value = 0.0F;
      ex::printStatus(lcmap::readFloat32(gDevice, addr, value));
      Serial.printf("float@0x%02X = %.7f\n", addr, static_cast<double>(value));
    }
  } else if (tokens[0] == "wait") {
    uint32_t timeoutMs = 6;
    if (argc >= 2 && !ex::parseU32(tokens[1], timeoutMs)) {
      Serial.println("Usage: wait [timeout_ms]");
    } else {
      ex::printStatus(gDevice.waitReady(timeoutMs));
    }
  } else if (tokens[0] == "health") {
    ex::printHealth(gDevice);
  } else {
    Serial.printf("Unknown command: %s\n", tokens[0].c_str());
  }

  Serial.print("> ");
}
