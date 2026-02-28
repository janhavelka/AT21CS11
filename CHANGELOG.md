# Changelog

All notable changes to this project are documented here.

## [Unreleased]

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

[Unreleased]: https://github.com/janhavelka/AT21CS11/compare/v1.1.1...HEAD
[1.1.1]: https://github.com/janhavelka/AT21CS11/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/janhavelka/AT21CS11/releases/tag/v1.1.0
[1.0.0]: https://github.com/janhavelka/AT21CS11/releases/tag/v1.0.0
