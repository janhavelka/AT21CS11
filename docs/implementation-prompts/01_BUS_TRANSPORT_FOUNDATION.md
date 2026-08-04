# Prompt 01 — completed Bus and transport foundation

## Status

Completed and checkpointed. This file is a historical regression boundary, not
an active implementation prompt.

## Established result

- Public core is framework-neutral and synchronous.
- One externally owned Backend represents one physical SI/O wire.
- One noncopyable Bus owns binding, address claims, Reset generation,
  diagnostics, and the Bus-wide post-write released-high hold.
- Whole-frame typed callbacks replace byte callbacks and distinguish NACK,
  timeout, line-stuck, stalled clock, and I/O failure.
- Validation occurs before callbacks and malformed callback evidence fails
  closed.
- Deadline arithmetic is checked; post-acceptance ambiguity is preserved and is
  never replayed.
- A Driver never owns or destroys its Backend or Bus.

The exact current declarations are in `include/AT21CS/Transport.h` and
`include/AT21CS/Bus.h`; current behavior is covered by `test/test_bus.cpp` and
the shared scripted transport.

## Regression rule

Later stages may fix an actual regression but must not reintroduce v1 transport,
per-byte callbacks, hidden retries, ACK polling during `tWR`, or library-owned
concurrency. If a protocol change is required, verify it against the authorized
datasheet and update the shared contract explicitly.
