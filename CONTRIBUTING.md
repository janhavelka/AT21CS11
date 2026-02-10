# Contributing

Thanks for contributing.

## Quick Start

1. Create a branch.
2. Implement focused changes.
3. Build examples before PR:
   - `python -m platformio run -e ex_presence_control_s3 -e ex_memory_security_s3 -e ex_rom_freeze_s3 -e ex_multi_device_s3 -e ex_presence_control_s2`
4. Update docs/changelog if behavior changed.
5. Open PR.

## Guidelines

- Keep public API changes intentional and documented.
- Preserve deterministic behavior and bounded waits.
- Avoid heap allocation in library steady-state paths.
- Keep protocol-level error granularity (`NACK_DEVICE_ADDRESS`, `NACK_MEMORY_ADDRESS`, `NACK_DATA`).

## Commit Style

Use Conventional Commits (`feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`).
