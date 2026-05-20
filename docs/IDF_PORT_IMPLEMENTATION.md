# AT21CS11 ESP-IDF Port Implementation Notes

Date: 2026-05-19.

## Implemented

- Replaced the previous ESP-IDF entry point with a native `app_main` CLI.
- Updated the IDF contract check to reject Arduino headers, Arduino CLI source
  inclusion, Arduino serial/string types, and Arduino-style facades in the IDF
  example.
- Preserved command coverage through a repo-local native command contract.

## Validation Status

- Static IDF contract: required before commit.
- Core timing guard: required before commit.
- PlatformIO native tests and Arduino example builds: required before commit.
- Native `idf.py` build: pending unless an ESP-IDF shell is available.
- Hardware: pending real single-wire validation for timing, pull-up, presence,
  speed switching, write polling, and disconnected-device behavior.
