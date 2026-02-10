#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"
#include "../common/Log.h"

AT21CS::Driver gDevice;

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                  - show commands");
  Serial.println("  present               - run isPresent() (presence pin + discovery)");
  Serial.println("  reset                 - resetAndDiscover()");
  Serial.println("  probe                 - probe() without health tracking");
  Serial.println("  recover               - recover() and update health");
  Serial.println("  high                  - setHighSpeed()");
  Serial.println("  std                   - setStandardSpeed() (AT21CS01 only)");
  Serial.println("  is_high               - check High-Speed mode");
  Serial.println("  is_std                - check Standard Speed mode");
  Serial.println("  part                  - print detected part");
  Serial.println("  health                - print health counters/state");
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== presence_and_discovery ===");
  Serial.printf("SI/O=%d presencePin=%d A2:A0=%u\n", board::SIO_PRIMARY,
                board::PRESENCE_PRIMARY, board::ADDRESS_BITS_PRIMARY);

  AT21CS::Config cfg;
  cfg.sioPin = board::SIO_PRIMARY;
  cfg.presencePin = board::PRESENCE_PRIMARY;
  cfg.presenceActiveHigh = true;
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
    const AT21CS::Status st = gDevice.isPresent(present);
    ex::printStatus(st);
    Serial.printf("present=%s\n", present ? "true" : "false");
  } else if (line == "reset") {
    const AT21CS::Status st = gDevice.resetAndDiscover();
    ex::printStatus(st);
  } else if (line == "probe") {
    const AT21CS::Status st = gDevice.probe();
    ex::printStatus(st);
  } else if (line == "recover") {
    const AT21CS::Status st = gDevice.recover();
    ex::printStatus(st);
  } else if (line == "high") {
    const AT21CS::Status st = gDevice.setHighSpeed();
    ex::printStatus(st);
    Serial.printf("speed=%s\n", ex::speedToStr(gDevice.speedMode()));
  } else if (line == "std") {
    const AT21CS::Status st = gDevice.setStandardSpeed();
    ex::printStatus(st);
    Serial.printf("speed=%s\n", ex::speedToStr(gDevice.speedMode()));
  } else if (line == "is_high") {
    bool enabled = false;
    const AT21CS::Status st = gDevice.isHighSpeed(enabled);
    ex::printStatus(st);
    Serial.printf("isHighSpeed=%s\n", enabled ? "true" : "false");
  } else if (line == "is_std") {
    bool enabled = false;
    const AT21CS::Status st = gDevice.isStandardSpeed(enabled);
    ex::printStatus(st);
    Serial.printf("isStandardSpeed=%s\n", enabled ? "true" : "false");
  } else if (line == "part") {
    Serial.printf("detectedPart=%s\n", ex::partToStr(gDevice.detectedPart()));
  } else if (line == "health") {
    ex::printHealth(gDevice);
  } else {
    Serial.printf("Unknown command: %s\n", line.c_str());
  }

  Serial.print("> ");
}
