# Changelog

All notable changes to this project are documented here.

## [Unreleased]

## [2.0.0] - 2026-08-05

- Made multi-frame EEPROM and Security reads whole-call transactional: a later
  frame failure leaves the caller buffer unchanged.
- Added explicit permanent Security Lock, ROM-zone and ROM Freeze provisioning
  guidance to public/Doxygen documentation and installed API comments.
- Passed an authorized 85-check destructive functional HIL run on one
  ESP32-S2/AT21CS11 setup. Waveform/electrical and other physical topologies
  remain unqualified and are not claimed.
- Removed completed implementation prompts, superseded stage records, raw HIL
  transcripts and the consumed one-shot destructive fixture; retained a concise
  hardware-validation record.

## [2.0.0-rc.1] - 2026-08-04

### Changed

- Replaced the v1 monolithic/pin-owned API with externally owned synchronous
  Backend, Bus and Driver objects.
- Made one Bus own one wire's address claims, Reset generation, diagnostics and
  fixed 10 ms released-high post-write hold.
- Added exact NACK-phase, transport-fault and conservative mutation evidence.
- Made AT21CS11 High-Speed-only admission explicit.
- Added explicit hot-plug recovery with an optional polarity-selectable detect
  input or firmware-owned bounded polling.
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
- Removed obsolete migration plans and duplicate stale reference extracts.

### Qualification

- Host and pinned Arduino build gates are software evidence only.
- No physical HIL, release publication, stable `2.0.0` tag or hardware
  qualification is claimed by this release-candidate stage.

## Earlier releases

The detailed 1.x history remains available in the repository's earlier tags.
Version 2 is intentionally API-breaking and provides no v1 compatibility
wrapper; see [migration notes](docs/MIGRATION.md).
