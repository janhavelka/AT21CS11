# AT21CS01 / AT21CS11 Driver (ESP32, Arduino, PlatformIO)

Production-grade single-wire EEPROM driver for Microchip **AT21CS01** and **AT21CS11**.

Copyright (c) 2026 Jan Havelka, Thymos Solution s.r.o (www.thymos.cz, info@thymos.cz)

## Features

- Timing-accurate single-wire PHY with explicit ACK/NACK slot handling.
- Reset + discovery handshake support.
- EEPROM operations: current/random/sequential reads, byte/page writes, ready polling.
- Security register operations: reads, user-area writes, lock/check lock.
- Factory serial number read with CRC-8 (`poly 0x31`).
- Manufacturer ID read and automatic part detection.
- ROM zone set/read and ROM-zone freeze commands.
- Speed mode APIs (Standard Speed rejected on AT21CS11).
- Explicit driver state machine and health counters.

## Install (PlatformIO)

```ini
lib_deps =
  https://github.com/janhavelka/AT21CS11.git
```

## Quick Start

```cpp
#include <Arduino.h>
#include "AT21CS/AT21CS.h"

AT21CS::Driver dev;

void setup() {
  Serial.begin(115200);

  AT21CS::Config cfg;
  cfg.sioPin = 8;
  cfg.presencePin = 7;      // optional, set -1 if not used
  cfg.addressBits = 0;      // A2:A0

  AT21CS::Status st = dev.begin(cfg);
  if (!st.ok()) {
    Serial.printf("begin failed: %s\n", st.msg);
    return;
  }

  uint8_t value = 0;
  st = dev.readEeprom(0x00, &value, 1);
  Serial.printf("status=%u value=0x%02X\n", static_cast<unsigned>(st.code), value);
}

void loop() {
  dev.tick(millis());
}
```

## API Summary

### Lifecycle
- `Status begin(const Config& config)`
- `void tick(uint32_t nowMs)`
- `void end()`

### Presence / Recovery
- `Status probe()`
- `Status resetAndDiscover()`
- `Status isPresent(bool& present)`
- `Status recover()`

### EEPROM / Security
- `Status readCurrentAddress(uint8_t& value)`
- `Status readEeprom(uint8_t address, uint8_t* data, size_t len)`
- `Status writeEepromByte(uint8_t address, uint8_t value)`
- `Status writeEepromPage(uint8_t address, const uint8_t* data, size_t len)`
- `Status readSecurity(uint8_t address, uint8_t* data, size_t len)`
- `Status writeSecurityUserByte(uint8_t address, uint8_t value)`
- `Status writeSecurityUserPage(uint8_t address, const uint8_t* data, size_t len)`
- `Status lockSecurityRegister()`
- `Status isSecurityLocked(bool& locked)`
- `Status waitReady(uint32_t timeoutMs)`

### IDs / Zones / Speed
- `Status readSerialNumber(SerialNumberInfo& serial)`
- `Status readManufacturerId(uint32_t& manufacturerId)`
- `Status detectPart(PartType& part)`
- `Status readRomZoneRegister(uint8_t zoneIndex, uint8_t& value)`
- `Status isZoneRom(uint8_t zoneIndex, bool& isRom)`
- `Status setZoneRom(uint8_t zoneIndex)`
- `Status freezeRomZones()`
- `Status areRomZonesFrozen(bool& frozen)`
- `Status setHighSpeed()` / `Status isHighSpeed(bool& enabled)`
- `Status setStandardSpeed()` / `Status isStandardSpeed(bool& enabled)`

### State / Health
- `DriverState state() const`
- `bool isOnline() const`
- `Status lastError() const`
- `uint8_t consecutiveFailures() const`
- `uint32_t totalFailures() const`
- `uint32_t totalSuccess() const`

## Condensed Examples

1. `examples/01_presence_control_cli`
2. `examples/02_memory_security_cli`
3. `examples/03_rom_freeze_cli`
4. `examples/04_multi_device_demo`

## Static Reference

The chip reference remains in:

- `docs/AT21CS01_AT21CS11_complete_driver_report.md`

## License

MIT, see `LICENSE`.
