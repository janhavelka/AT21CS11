# Changelog

All notable changes to this project are documented here.

## [Unreleased]

### Added
- Framework-neutral `AT21CS/Transport.h` and `AT21CS/Core.h` headers defining the single-wire backend contract and clean core include surface.
- Core timing guard checks now reject Arduino, ESP-IDF, FreeRTOS, ESP32 platform macro, and direct GPIO framework tokens in clean public core headers.
- Host fake-transport tests for injected reset/discovery, byte read/write
  sequencing, ACK/NACK phase errors, reset error propagation, and serial CRC
  validation.
- Arduino-ESP32 S2/S3 physical-smoke consumers and host timing-oracle coverage
  for the externally owned `Esp32Transport`.
- `SettingsSnapshot`, `getSettings()`, `isInitialized()`, `getConfig()`, and `driverState()` for cache-only runtime/health inspection.
- Bring-up CLI `cfg` / `settings` output now reports the cached settings snapshot, including initialization state and `offlineThreshold`.

### Changed
- Consolidated ESP32-S2/S3 Arduino GPIO/timing in the explicit externally owned
  `src/platform/esp32/Esp32Transport.cpp`; protocol/core sources remain
  framework-independent and contain no ESP32 timing/GPIO headers.
- Core timing guard now allows framework timing/GPIO tokens only in private
  platform/backend sources and examples, not public headers or core source.
- Native tests now cover the no-hardware begin failure path through an injected
  `SingleWireTransport` fake instead of Arduino/Wire stubs.
- `Config::sioPin`, `presencePin`, and `presenceActiveHigh` are documented as
  built-in backend compatibility config retained for this major version.
  Injected transports must leave those fields unset and use
  `SingleWireTransport::presencePresent` for presence policy.
- `AT21CS.h` no longer includes ESP32, FreeRTOS, SoC GPIO, CPU, or IRAM headers; ESP32 timing internals are kept in private platform source.
- `Config` can optionally accept a `SingleWireTransport` backend while preserving the existing built-in pin-based backend path.
- Doxyfile project metadata now matches `library.json` and references the
  maintained docs tree instead of removed template files.
- Reference documentation now separates compact chip notes from full PDF extraction under `docs/extracted-md/` and `docs/pdf-extracted-md/`.
- `begin()` now validates `expectedPart` and `startupSpeed` enum values before any GPIO/protocol activity.
- `Config::offlineThreshold = 0` now normalizes to one, failed `begin()` clears stale runtime state, and `end()` clears cached configuration.
- ESP32 PlatformIO builds now pin pioarduino `platform-espressif32` 55.03.311
  (Arduino-ESP32 3.3.11) and explicitly use C++17.
- Multi-page write helpers now report `NOT_INITIALIZED` before argument validation when called before a successful `begin()`.
- README write-ready documentation now matches the enforced `1..250 ms` timeout range and stalled-clock guard behavior.
- The explicit ESP32 transport compiles only under Arduino-ESP32; the obsolete
  root IDF component, component manifest, native-IDF smoke fixture, metadata,
  and `ESP_PLATFORM` implementation branch were removed.
- `library.json` now advertises only the supported Arduino framework.
- README documentation links and validation commands now match the files currently present in the repository.

### Fixed
- ESP32-S2 uploads now use esptool 5's `no-reset-stub` spelling for the
  post-upload reset mode.
- Normal operations while `OFFLINE` now return `INVALID_STATE` without protocol traffic while `probe()` and `recover()` remain available.
- `waitReady()` now has a finite stalled-clock poll guard when an injected millisecond source stops advancing.
- ESP32 GPIO cleanup after failed initialization now avoids uncached direct-register pointer dereferences.
- ESP32 cycle-counter usage now relies on the current `esp_cpu_get_cycle_count()` API instead of a removed `esp_cpu_get_ccount()` fallback.
- `begin()` now rejects mixed injected-transport plus compatibility pin config
  with `INVALID_CONFIG`, avoiding silently ignored pin or presence policy.
- Injected transport tests now cover `presencePresent` preflight failure before
  protocol I/O and bounded `waitReady()` timeout health updates.

## [1.3.0] - 2026-04-08

### Added
- **`writeEeprom(addr, data, len)`** — multi-page EEPROM write with automatic page-boundary splitting and `waitReady` between pages.
- **`writeSecurityUser(addr, data, len)`** — multi-page security register write for the user area (0x10–0x1F).
- **CLI: `e_dump <addr> <len>`** — hex+ASCII memory dump (16 bytes/line with `|ASCII|` column).
- **CLI: `e_text <addr> <len>`** — escaped text view of EEPROM contents.
- **CLI: `e_fill <addr> <value> <len>`** — fill EEPROM region with a byte value.
- **CLI: `e_verify <addr> <v0> [..vN]`** — verify EEPROM contents against expected bytes with mismatch reporting.
- **CLI: `e_crc <addr> <len>`** — CRC-32 (IEEE 802.3) over an EEPROM region.
- **CLI: `e_strings [addr] [len] [min]`** — scan for printable ASCII strings in EEPROM.
- **CLI: `s_dump <addr> <len>`** — hex+ASCII dump for the security register.
- **CLI: `stress_rw [N]`** — write-verify stress test (write → waitReady → read → compare).
- **CLI: `speed [N]`** — per-operation speed benchmark with min/max/avg µs table.
- Character literal support in CLI byte arguments (e.g. `e_write 0 'A'`).

### Changed
- `printStatus()` output format unified with other I2C libraries: two-line `Status: OK (code=0, detail=0)` + `Message:` (message line suppressed on success).
- `printHealth()` output reformatted to labeled multi-line style matching other I2C libraries.
- `stress` and `stress_mix` summary output unified to `Target/Attempts/Success/Errors/Duration/Rate` format.
- Help text reorganized into five sections: Common, Device, EEPROM And Security, Diagnostics, Load Cell Map.
- `e_read` max length increased from 32 to 128 bytes (full EEPROM).
- `e_fill` now uses the new `writeEeprom()` driver method instead of manual page splitting.

## [1.2.1] - 2026-04-05

### Changed
- README now explicitly marks `examples/common/LoadCellMap.h` as example-only scaffolding instead of a stable installed header.
- The load-cell mapping example now tells downstream users to copy or adapt the helper into their own project rather than depending on the example path as public API.

## [1.2.0] - 2026-03-01

### Changed
- Updated `docs/IDF_PORT.md` to match the current portability-by-design implementation.
- Synchronized timing abstraction usage and configuration behavior in core driver code.

### Fixed
- Core timing guard compliance by removing direct timing API use from guarded paths.

## [1.1.2] - 2026-02-28

### Added
- Unified bringup support files in `examples/common/*` (`BusDiag`, `CliShell`, `HealthView`, `TransportAdapter`).
- Canonical docs for unification and ESP-IDF portability (`docs/UNIFICATION_STANDARD.md`, `docs/IDF_PORT.md`).
- Repository guard tools for CLI/timing contracts.

### Changed
- Replaced split demo layout (`01_general_control_cli`, `02_multi_device_demo`) with a single comprehensive `examples/01_basic_bringup_cli`.
- Normalized CI/test structure to the shared library profile.

## [1.1.1] - 2026-02-22

### Added
- `Err::INVALID_STATE` error code for state-machine violation reporting.

### Fixed
- Driver state guards: bus operations from `FAULT` or `SLEEPING` states now return `INVALID_STATE` instead of proceeding with unpredictable behavior.
- `begin()` GPIO pin leak on re-entry: a previously failed `begin()` that configured the pin but didn't complete initialization now properly releases the line before reconfiguring.
- `recover()` now re-applies configured startup speed mode after reset+discovery (device always resets to High-Speed; Standard Speed was silently lost).
- `probe()` now tracks IO results through `_trackIo()` so health counters stay consistent with all other operations.
- Error messages for `writeTimeoutMs` upper bound now correctly say "250" instead of the incorrect "1000" (matching `MAX_READY_TIMEOUT_MS = 250`).
- Config comment for `writeTimeoutMs` range corrected from "1..1000 ms" to "1..250 ms".
- `crc8_31()` guard order: early-return for `len == 0` before null-pointer check, preventing silent masking of caller bugs.

## [1.1.0] - 2026-02-10

### Added
- `examples/common/LoadCellMap.h` with full typed load-cell memory map helpers:
  - Fixed block/field addresses
  - CRC-sealed versioned records
  - Page-safe write splitting
  - Calibration master/mirror handling
  - Typed POD helpers including float read/write

### Changed
- Condensed examples into two practical CLI demos (`01_general_control_cli`, `02_multi_device_demo`).
- Removed obsolete template compatibility files and legacy placeholders.
- Replaced generic template metadata with project-specific maintainer and ownership data.
- Updated AGENTS.md to AT21CS01/AT21CS11 project guidance.

### Fixed
- Restored `docs/AT21CS01_AT21CS11_complete_driver_report.md` to static reference content.
- Hardened driver API bounds and timeout behavior for production use:
  - read/write range checks now reject out-of-bounds address+length requests
  - page writes now reject cross-page writes that would wrap and overwrite data unexpectedly
  - retry loop counter overflow removed for `discoveryRetries=255`
  - `waitReady()` now validates timeout input and handles presence-pin disconnect during polling
  - write-ready timeout is now explicitly bounded to 1..1000 ms
  - `NOT_PRESENT` now always transitions state to `OFFLINE`
  - `readSerialNumber()` now initializes output structure before use

## [1.0.0] - 2026-02-10

### Added
- Initial production-ready release for AT21CS01 + AT21CS11.

[Unreleased]: https://github.com/janhavelka/AT21CS11/compare/v1.3.0...HEAD
[1.3.0]: https://github.com/janhavelka/AT21CS11/compare/v1.2.1...v1.3.0
[1.2.1]: https://github.com/janhavelka/AT21CS11/compare/v1.2.0...v1.2.1
[1.2.0]: https://github.com/janhavelka/AT21CS11/compare/v1.1.2...v1.2.0
[1.1.2]: https://github.com/janhavelka/AT21CS11/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/janhavelka/AT21CS11/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/janhavelka/AT21CS11/releases/tag/v1.1.0
[1.0.0]: https://github.com/janhavelka/AT21CS11/releases/tag/v1.0.0
