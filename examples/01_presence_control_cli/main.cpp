#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"

AT21CS::Driver gDevice;

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                  - show commands");
  Serial.println("  present               - run presence check");
  Serial.println("  reset                 - reset + discovery");
  Serial.println("  probe                 - discovery probe (raw)");
  Serial.println("  recover               - recover from degraded/offline");
  Serial.println("  high                  - set high speed");
  Serial.println("  std                   - set standard speed (AT21CS01 only)");
  Serial.println("  is_high               - check high speed mode");
  Serial.println("  is_std                - check standard speed mode");
  Serial.println("  part                  - print detected part");
  Serial.println("  id                    - read manufacturer ID");
  Serial.println("  health                - print health counters/state");
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== 01_presence_control_cli ===");
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

  if (line == "help" || line == "?") {
    printHelp();
  } else if (line == "present") {
    bool present = false;
    ex::printStatus(gDevice.isPresent(present));
    Serial.printf("present=%s\n", present ? "true" : "false");
  } else if (line == "reset") {
    ex::printStatus(gDevice.resetAndDiscover());
  } else if (line == "probe") {
    ex::printStatus(gDevice.probe());
  } else if (line == "recover") {
    ex::printStatus(gDevice.recover());
  } else if (line == "high") {
    ex::printStatus(gDevice.setHighSpeed());
    Serial.printf("speed=%s\n", ex::speedToStr(gDevice.speedMode()));
  } else if (line == "std") {
    ex::printStatus(gDevice.setStandardSpeed());
    Serial.printf("speed=%s\n", ex::speedToStr(gDevice.speedMode()));
  } else if (line == "is_high") {
    bool enabled = false;
    ex::printStatus(gDevice.isHighSpeed(enabled));
    Serial.printf("isHighSpeed=%s\n", enabled ? "true" : "false");
  } else if (line == "is_std") {
    bool enabled = false;
    ex::printStatus(gDevice.isStandardSpeed(enabled));
    Serial.printf("isStandardSpeed=%s\n", enabled ? "true" : "false");
  } else if (line == "part") {
    Serial.printf("part=%s\n", ex::partToStr(gDevice.detectedPart()));
  } else if (line == "id") {
    uint32_t id = 0;
    ex::printStatus(gDevice.readManufacturerId(id));
    Serial.printf("manufacturerId=0x%06lX\n", static_cast<unsigned long>(id));
  } else if (line == "health") {
    ex::printHealth(gDevice);
  } else {
    Serial.printf("Unknown command: %s\n", line.c_str());
  }

  Serial.print("> ");
}
