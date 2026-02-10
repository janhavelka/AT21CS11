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
  cfg.presencePin = 7;      // optional, set -1 if not used; authoritative when set
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

## Example Use: Load Cell Data Layout

This is a practical layout for a production load-cell module where some fields must be immutable and some must change over time.

| Region | Address range | Example content | Mutable |
|---|---|---|---|
| Security (factory) | `0x00..0x07` | Chip serial payload (product ID + UID + CRC from factory) | No |
| Security (user) | `0x10..0x1F` | Module serial, model ID, HW revision, manufacturing batch, schema version, CRC | Program once, then lock |
| EEPROM Zone 0 | `0x00..0x1F` | Calibration master block: nominal capacity, zero balance, span/gain coefficients, temp compensation coefficients, CRC32 | No after ROM set |
| EEPROM Zone 1 | `0x20..0x3F` | Calibration mirror block and backup static metadata (factory date code, fixture ID), CRC32 | No after ROM set |
| EEPROM Zone 2 | `0x40..0x5F` | Mutable operating state: installation tare, customer offset trim, filter profile, last service flags, rolling sequence + CRC | Yes |
| EEPROM Zone 3 | `0x60..0x7F` | Mutable lifecycle counters: overload events, overtemperature events, power cycles, service cycles (wear-leveled journal) | Yes |

### Implemented helper: `LoadCellMap.h`

A full, production-style map helper is provided in:

- `examples/common/LoadCellMap.h`

It includes:
- Fixed addresses and field offsets for identity/calibration/runtime/counter records.
- Versioned typed structs with CRC validation.
- Master+mirror calibration helpers with fallback read.
- Safe EEPROM/security writes split by 8-byte page boundaries.
- Typed POD read/write helpers (`float` supported via `readFloat32` / `writeFloat32`).

Quick usage:

```cpp
#include "examples/common/LoadCellMap.h"

lcmap::CalibrationBlockV1 cal = {};
cal.capacityGrams = 50000;
cal.zeroBalanceRaw = -17320;
cal.spanRawAtCapacity = 947112;
cal.sensitivityNvPerV = 2000000;
cal.tempCoeffPpmPerC = -35;
cal.linearityPpm = 120;
AT21CS::Status st = lcmap::writeCalibrationBoth(dev, cal);

lcmap::RuntimeBlockV1 runtime = {};
bool valid = false;
st = lcmap::readRuntime(dev, runtime, valid);
if (st.ok() && valid) {
  runtime.seq += 1;
  runtime.installTareRaw = -250;
  st = lcmap::writeRuntime(dev, runtime);
}
```

### Suggested record model

- Keep immutable records self-contained with `magic`, `version`, `payload`, and `crc`.
- Keep mutable records as append/journal entries with sequence counters to survive brown-outs.
- Update counters in batches when possible to reduce wear and write latency (`t_WR`).

Example immutable calibration payload (stored in Zone 0 / Zone 1 mirror):

```cpp
struct CalibrationBlockV1 {
  uint32_t magic;              // 'LCAL'
  uint16_t version;            // = 1
  uint16_t capacityGramsDiv10; // 500000 = 50 kg
  int32_t zeroBalanceRaw;      // ADC code at no-load
  int32_t spanRawAtCapacity;   // ADC code at rated load
  int16_t tempCoeffPpmPerC;    // optional compensation
  uint16_t reserved;
  uint32_t crc32;              // of all previous bytes
}; // 24 B, room left for future fields
```

Example mutable state entry (Zone 2/3, journal style):

```cpp
struct RuntimeEntryV1 {
  uint32_t seq;             // monotonic
  int32_t installTareRaw;   // field tare
  uint32_t overloadCount;   // accumulated events
  uint32_t powerCycleCount; // optional
  uint16_t flags;           // service/calibration flags
  uint16_t crc16;
}; // 20 B
```

### Recommended production lock sequence

1. Program Security user bytes (`0x10..0x1F`) with module identity + schema + CRC.
2. Verify readback, then call `lockSecurityRegister()` (irreversible).
3. Program Zone 0 (calibration master) and Zone 1 (mirror), verify CRC.
4. Call `setZoneRom(0)` and `setZoneRom(1)` to make calibration immutable.
5. After final EOL validation, call `freezeRomZones()` so ROM-zone configuration cannot change.
6. During normal operation, write only to Zone 2/3.

Notes:
- No password/unlock mechanism exists for security lock, ROM zone setting, or ROM zone freeze.
- If you need post-sale recalibration, keep at least one zone writable for a signed calibration update record.

## Condensed Examples

1. `examples/01_general_control_cli` (all single-device functionality in one CLI)
2. `examples/02_multi_device_demo` (basic controls for each indexed device instance)

## Static Reference

The chip reference remains in:

- `docs/AT21CS01_AT21CS11_complete_driver_report.md`

## License

MIT, see `LICENSE`.
