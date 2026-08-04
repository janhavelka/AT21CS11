# Prompt 02 — completed Driver lifecycle, reads, identity, and speed

## Status

Completed and checkpointed. This file records the regression boundary.

## Established result

- Driver binding is zero-I/O and address claims are scoped to one Bus.
- `begin()` is `bind()` plus one initialization attempt.
- Initialization absence keeps the Driver bound and recoverable.
- `recover()` is the explicit hot-plug/power-up path and performs one
  Reset/Discovery/initialization sequence with no hidden retry.
- When its Bus has a presence callback, `initialize()`/`recover()` use one raw
  logical sample only as a preflight. False returns `NOT_PRESENT` before Reset;
  true still requires the real protocol and identity checks.
- `probe()` is liveness-only and performs no reconnect Reset.
- Ordinary reads are address-explicit and transactional.
- Manufacturer ID retains the raw value while part classification masks only
  the documented revision bits.
- AT21CS11 rejects Standard Speed before transport activity.
- Bus Reset generation keeps Drivers sharing one wire synchronized without a
  reset loop.
- State, health, and cached getters remain deterministic and externally
  observable; validation failures are health-neutral.
- Obsolete v1 methods, including `tick()`, current-address reads, hidden
  activation, destructive speed queries, and compatibility aliases are absent.

Current declarations are in `include/AT21CS/AT21CS.h`. Lifecycle and hot-plug
regressions are covered by `test/test_lifecycle.cpp`,
`test/test_reads_identity_speed.cpp`, and `test/test_health_state.cpp`.

## Regression rule

Hot-plug remains synchronous: firmware decides when to call `recover()` and may
read/compare the serial afterward. Caller-owned bounded polling is allowed;
polling, retry/backoff, attachment-generation, and RTOS policy must not enter
Driver.
