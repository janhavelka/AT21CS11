# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Complete AT21CS01/AT21CS11 single-wire driver API and implementation.
- Timing-accurate PHY with bit/byte TX/RX and ACK/NACK slot handling.
- Reset + discovery handshake and presence helpers.
- EEPROM, Security register, lock, serial CRC, manufacturer ID, ROM zone, and freeze command support.
- Speed mode control for High-Speed and Standard Speed (AT21CS01-only support enforced).
- Driver health model (`READY/DEGRADED/OFFLINE`) with counters and timestamps.
- Nine interactive PlatformIO examples covering all chip features.
- Datasheet digest report in `docs/AT21CS01_AT21CS11_complete_driver_report.md`.

### Changed
- Renamed library surface to `AT21CS/*` headers and updated metadata/tooling.
- Updated PlatformIO environments and CI matrix to build new examples.

## [1.0.0] - 2026-02-10

### Added
- Initial production-ready release for AT21CS01 + AT21CS11.

[Unreleased]: https://github.com/janhavelka/AT21CS11/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/janhavelka/AT21CS11/releases/tag/v1.0.0
