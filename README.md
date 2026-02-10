# AT21CS01 / AT21CS11 Driver (ESP32, Arduino, PlatformIO)

Production-grade single-wire EEPROM driver for Microchip **AT21CS01** and **AT21CS11**.

The library targets ESP32-S2/ESP32-S3 with deterministic microsecond timing and supports all core device features from DS20005857D.

## Features

- Timing-accurate single-wire PHY:
  - `driveLow(us)`, `releaseLine()`, `readLine()`
  - bit TX/RX, byte TX/RX (MSb first), ACK/NACK slot handling
  - Start/Stop high-time framing (`t_HTSS`)
- Mandatory reset + discovery handshake (`resetAndDiscover()`)
- EEPROM (`128x8`):
  - current address read
  - random + sequential read
  - byte write + page write (8-byte page wrap behavior)
  - busy poll helper `waitReady(timeoutMs)`
- Security register (`32 bytes`):
  - random + sequential read
  - user-area write restriction (`0x10..0x1F`)
  - lock command + lock check
- Factory serial number read and CRC-8 verification (`poly=0x31`)
- Manufacturer ID read and part detection:
  - `AT21CS01`: `0x00D200`
  - `AT21CS11`: `0x00D380`
- ROM zone management and freeze control
- Speed control:
  - `setHighSpeed()` / `isHighSpeed()`
  - `setStandardSpeed()` / `isStandardSpeed()` (AT21CS01 only)
- Health model and diagnostics:
  - Explicit `DriverState` machine:
    - `UNINIT`, `PROBING`, `INIT_CONFIG`, `READY`, `BUSY`
    - `DEGRADED`, `OFFLINE`, `RECOVERING`
    - `SLEEPING` (reserved), `FAULT`
  - failure counters and last-error timestamps

## Install

### PlatformIO

```ini
lib_deps =
  https://github.com/janhavelka/AT21CS11.git
```

## Quick Start

```cpp
#include <Arduino.h>
#include "AT21CS/AT21CS.h"

AT21CS::Driver eeprom;

void setup() {
  Serial.begin(115200);

  AT21CS::Config cfg;
  cfg.sioPin = 8;         // SI/O pin
  cfg.presencePin = 7;    // optional, set -1 to disable
  cfg.addressBits = 0;    // A2:A0
  cfg.expectedPart = AT21CS::PartType::UNKNOWN;

  AT21CS::Status st = eeprom.begin(cfg);
  if (!st.ok()) {
    Serial.printf("begin failed: %s\n", st.msg);
    return;
  }

  uint8_t value = 0;
  st = eeprom.readEeprom(0x00, &value, 1);
  Serial.printf("read status=%d value=0x%02X\n", static_cast<int>(st.code), value);
}

void loop() {
  eeprom.tick(millis());
}
```

## API Overview

### Lifecycle

- `Status begin(const Config& config)`
- `void tick(uint32_t nowMs)`
- `void end()`

### Presence / Recovery

- `Status probe()`
- `Status recover()`
- `Status resetAndDiscover()`
- `Status isPresent(bool& present)`

### EEPROM

- `Status readCurrentAddress(uint8_t& value)`
- `Status readEeprom(uint8_t address, uint8_t* data, size_t len)`
- `Status writeEepromByte(uint8_t address, uint8_t value)`
- `Status writeEepromPage(uint8_t address, const uint8_t* data, size_t len)`
- `Status waitReady(uint32_t timeoutMs)`

### Security

- `Status readSecurity(uint8_t address, uint8_t* data, size_t len)`
- `Status writeSecurityUserByte(uint8_t address, uint8_t value)`
- `Status writeSecurityUserPage(uint8_t address, const uint8_t* data, size_t len)`
- `Status lockSecurityRegister()`
- `Status isSecurityLocked(bool& locked)`
- `Status readSerialNumber(SerialNumberInfo& serial)`

### IDs / Part Detection

- `Status readManufacturerId(uint32_t& manufacturerId)`
- `Status detectPart(PartType& part)`

### ROM Zones / Freeze

- `Status readRomZoneRegister(uint8_t zoneIndex, uint8_t& value)`
- `Status isZoneRom(uint8_t zoneIndex, bool& isRom)`
- `Status setZoneRom(uint8_t zoneIndex)`
- `Status freezeRomZones()`
- `Status areRomZonesFrozen(bool& frozen)`

### Speed Control

- `Status setHighSpeed()`
- `Status isHighSpeed(bool& enabled)`
- `Status setStandardSpeed()`
- `Status isStandardSpeed(bool& enabled)`

### Health

- `DriverState state() const`
- `uint8_t consecutiveFailures() const`
- `uint32_t totalFailures() const`
- `uint32_t totalSuccess() const`
- `Status lastError() const`

## Error Model

`Status` returns one of `Err` codes, including:

- presence/discovery: `NOT_PRESENT`, `DISCOVERY_FAILED`
- ACK stage errors: `NACK_DEVICE_ADDRESS`, `NACK_MEMORY_ADDRESS`, `NACK_DATA`
- write busy timeout: `BUSY_TIMEOUT`
- invalid usage: `INVALID_CONFIG`, `INVALID_PARAM`, `NOT_INITIALIZED`
- unsupported command: `UNSUPPORTED_COMMAND`

## Examples

All examples are interactive via Serial monitor and print command outcomes:

1. `examples/presence_and_discovery`
2. `examples/eeprom_read_write`
3. `examples/security_user_data`
4. `examples/security_lock`
5. `examples/serial_number_and_crc`
6. `examples/manufacturer_id`
7. `examples/rom_zones`
8. `examples/freeze_rom_zones`
9. `examples/multi_device_demo`

## Datasheet Digest

See `docs/AT21CS01_AT21CS11_complete_driver_report.md`.

## License

MIT, see `LICENSE`.
