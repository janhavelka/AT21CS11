# Changelog

All notable changes to this project are documented here.

## [Unreleased]

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

[Unreleased]: https://github.com/janhavelka/AT21CS11/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/janhavelka/AT21CS11/releases/tag/v1.1.0
[1.0.0]: https://github.com/janhavelka/AT21CS11/releases/tag/v1.0.0
