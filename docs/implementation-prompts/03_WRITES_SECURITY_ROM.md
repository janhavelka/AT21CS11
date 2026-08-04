# Prompt 03 — completed writes, Security, Lock, ROM zones, and Freeze

## Status

Completed and checkpointed. This file records the regression boundary.

## Established result

- EEPROM and Security writes validate complete `size_t` ranges before I/O.
- Page writes do not cross an eight-byte page; range writes use one shared page
  splitter and stop at the first failure.
- Bus enforces the fixed 10 ms released-high interval after a write may have
  been accepted; no ACK polling occurs.
- `WriteResult` records committed prefix, accepted bytes, and ambiguous final
  page effect without replay.
- Lock, ROM-zone enable, and Freeze use only documented command frames and
  conservative `MutationResult` evidence.
- Check Lock uses opcode `2h/W` plus the documented `0x6X` address.
- No invented opcode `1h/R` Freeze-state query exists.
- Irreversible methods begin with `permanently` and never hide uncertainty.

Regression coverage lives in `test/test_eeprom_writes.cpp`,
`test/test_security.cpp`, and `test/test_rom_and_freeze.cpp`.

## Regression rule

Examples and automated tests may compile irreversible handlers but must not
perform a real irreversible mutation. Actual irreversible HIL requires Prompt
08 authorization and a sacrificial device.
