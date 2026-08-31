# Changelog

All notable changes to this project are documented here.

## [Unreleased]

### Fixed

- Made AT21CS01 Standard Speed exclusive to one physical-wire Bus claim so
  mixed-speed multi-drop operation is rejected before device traffic.
- Kept each Standard-Speed protocol segment uninterrupted so an inter-byte
  idle gap cannot abort or reinterpret a read or command, or commit a partial
  page write.
- Added a 24 ms Standard-Speed transfer deadline while retaining the 9 ms
  High-Speed and presence deadlines.
- Corrected the chip reference's `t_RD` and `t_MRS` rise-time relations and
  made package parent-link escape detection exact.

## [2.0.0] - 2026-08-05

### Changed

- Replaced the v1 monolithic/pin-owned API with externally owned synchronous
  Backend, Bus and Driver objects.
- Made one Bus own one wire's address claims, Reset generation, diagnostics and
  fixed 10 ms released-high post-write hold.
- Added exact NACK-phase, transport-fault and conservative mutation evidence.
- Made AT21CS11 High-Speed-only admission explicit.
- Added explicit hot-plug recovery with an optional polarity-selectable detect
  input or firmware-owned bounded polling.
- Made multi-frame EEPROM and Security reads whole-call transactional: a later
  frame failure leaves the caller buffer unchanged.
- Added explicit permanent Security Lock, ROM-zone and ROM Freeze provisioning
  guidance to public/Doxygen documentation and installed API comments.
- Reduced the examples to one bounded single-device CLI and one concise
  two-wire/two-device CLI.
- Curated package contents and added clean package consumers for the core and
  both examples on ESP32-S2/S3.
- Made version generation deterministic and added documentation, packaging and
  CI checks.

### Removed

- Removed v1 compatibility APIs, current-address reads, hidden recovery/retry,
  product-specific example data models and the superseded alternative
  framework path.
- Removed obsolete migration plans, duplicate stale reference extracts,
  completed implementation prompts, superseded stage records, raw HIL
  transcripts and the consumed one-shot destructive fixture; retained a concise
  hardware-validation record.

### Qualification

- Passed an authorized 85-check destructive functional HIL run on one
  ESP32-S2/AT21CS11 setup. Waveform/electrical and other physical topologies
  remain unqualified and are not claimed.

## Earlier releases

The detailed 1.x history remains available in the repository's earlier tags.
Version 2 is intentionally API-breaking and provides no v1 compatibility
wrapper; see [migration notes](docs/MIGRATION.md).
