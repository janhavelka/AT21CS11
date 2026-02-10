# Prompt for AI coder — implement AT21CS01/AT21CS11 driver (Arduino/PlatformIO) using template repo

You are implementing a **complete driver** for Microchip **AT21CS01 + AT21CS11** single-wire, I/O-powered EEPROMs.

Template repo (starting point):
- https://github.com/janhavelka/AT21CS11

You must adapt the template to support **both parts** and to implement **ALL features** described in:
- `docs/AT21CS01_AT21CS11_complete_driver_report.md` (provided in this repo)

The user will integrate this library into an ESP32 project (Arduino framework, PlatformIO) and will have **multiple devices**, each on its **own SI/O GPIO line**. Writes are rare; reads happen often when connected. Presence detection will use an **additional presence pin** (optional per instance).

---

## Hard requirements

### Output
- Implement the library code in this repo, matching the existing repo’s structure/style.
- Add the datasheet digest report to **`docs/AT21CS01_AT21CS11_complete_driver_report.md`**.
- Provide examples that demonstrate **every** chip feature, all the features are available using serial monitor with descriptive help.
- Update README + API docs so a user can use the library without reading the datasheet.

### Platform
- PlatformIO + Arduino
- Target board family: ESP32 (focus on deterministic microsecond timing)

---

## Core constraints from datasheet (must respect)
- Single wire, timed bit frames, MSb-first.
- After every Reset/power-up, **Discovery Response** must be performed before commands.
- No unused clock cycles: do not insert breaks inside a byte + ACK/NACK slot.
- During internal write cycle (t_WR up to 5 ms), device blocks commands and must remain powered (line high).
- AT21CS11: **High-Speed only** (Standard Speed opcode Dh must NACK).
- AT21CS01: supports both **Standard** and **High-Speed**.

---

## What you must implement (feature checklist)

### A) Timing-accurate PHY
Implement low-level primitives with correct timing:
- `driveLow(us)`, `releaseLine()`, `readLine()`
- `txBit0()`, `txBit1()` using t_LOW0/t_LOW1
- `rxBit()` using read strobe t_RD and sampling window t_MRS
- `txByte()` and `rxByte()` (MSb first)
- ACK/NACK handling in the 9th bit slot
- Start/Stop handling (t_HTSS)

**Implementation note (ESP32):** avoid slow GPIO functions in the tight path. Use fast GPIO writes (register-level or optimized API) and critical sections around byte transfers to avoid timing jitter.

### B) Reset + Discovery (presence handshake)
- Implement `resetAndDiscover()` per report §9.
- Provide `isPresent()`:
  - If `presencePin` configured: check it first (fast path)
  - Else: run `resetAndDiscover()` and return pass/fail

### C) EEPROM (128×8)
- Current Address Read
- Random Read + Sequential Read
- Byte Write
- Page Write (8 bytes) with correct page-wrap rules
- Busy polling helper: `waitReady(timeout)` that probes ACK until ready

### D) Security Register (32 bytes)
- Read (random/sequential)
- Write only user area 0x10–0x1F (enforce range)
- Security Lock (opcode 2h) + Check Lock (read form)
- After locked: writes must NACK on data byte as per report

### E) Serial number + CRC
- Implement `readSerialNumber()` that reads security bytes 0x00–0x07:
  - product ID must be 0xA0
  - CRC polynomial: X^8 + X^5 + X^4 + 1 (CRC-8 poly 0x31)
  - return `crc_ok`

### F) Manufacturer ID
- Implement opcode Ch read of 3 bytes
- Detect part:
  - AT21CS01 returns 00D200h
  - AT21CS11 returns 00D380h

### G) ROM Zones + Freeze
- Read ROM Zone registers (per-zone)
- Set a zone ROM by writing 0xFF to its ROM Zone register address
- Implement Freeze ROM Zones (opcode 1h, address 0x55, data 0xAA) + check frozen
- Correctly expose behavior:
  - writes into ROM zones NACK on data byte (and do not start t_WR)

### H) Speed modes
- `setHighSpeed()` / `isHighSpeed()` (opcode Eh)
- `setStandardSpeed()` / `isStandardSpeed()` (opcode Dh; AT21CS01 only)
- On AT21CS11, `setStandardSpeed()` must fail gracefully (NACK expected)

### I) Error model
Provide a clear error/status model that distinguishes:
- not present / discovery failure
- NACK at device address vs memory address vs data byte
- busy timeout
- invalid args (out of range, forbidden writes)
- unsupported command (Standard Speed on AT21CS11)
- has all health features

---

## Multi-device support
Each driver instance is bound to:
- one SI/O GPIO
- optional presence GPIO
- preprogrammed A2:A0 value (must be configurable even if you use one device per wire)

---

## Examples (must compile in PlatformIO)

Create examples that demonstrate all features:

1. `presence_and_discovery`
2. `eeprom_read_write`
3. `security_user_data`
4. `security_lock`
5. `serial_number_and_crc`
6. `manufacturer_id`
7. `rom_zones`
8. `freeze_rom_zones`
9. `multi_device_demo`

Each example must print ACK/NACK outcomes and key values to serial for debugging.

---

## Repo edits you must do starting from template
Update the template repo to fit **AT21CS01 + AT21CS11**:
- Rename library identifiers / headers / README references accordingly
- Add device-variant handling (IDs, speed commands)
- Add docs entry: `docs/AT21CS01_AT21CS11_complete_driver_report.md`
- If the template hardcodes timings or assumes one part, refactor to parameterize by part + chosen speed mode.

---

## Start now
1. Inspect template structure and identify where the PHY sits.
2. Implement PHY + reset/discovery first.
3. Implement read/write surfaces and all special commands.
4. Add examples and docs.
5. Ensure PlatformIO build passes for ESP32 Arduino.

