# Prompt 05 — completed host tests and fault injection

## Status

Completed and checkpointed at the current baseline. This file records the test
boundary; it is not an instruction to redesign the API.

## Established result

The native suite exercises production Backend/Bus/Driver paths with independent
expected frames and injected faults, including:

- all address and data NACK phases;
- timeout, line-stuck, I/O failure, stalled clocks, cleanup failure, and malformed
  callback evidence;
- exact Reset/Discovery, reads, speed changes, CRC, Lock, ROM, and Freeze frames;
- `SIZE_MAX`, range, page, state, and deadline boundaries;
- transactional reads and conservative partial/ambiguous writes;
- address claims, stale bindings, shared Reset generation, and retained holds;
- two Drivers on one Bus and two independent address-zero Buses;
- absent-at-boot binding retention and later explicit hot-plug recovery;
- backend pin/descriptor/timing isolation under serialized interleaving;
- sanitizers and strict warnings.

The suite uses `test/support/ScriptedTransport.*` and current production code. It
does not claim physical waveform success; those rows remain Prompt-08 HIL.

## Regression rule

Stages 06-07 run the native suite after affected changes. Add focused tests for
new example/parser/package behavior, but do not create a second transport, fake
protocol implementation, RTOS owner fixture, or application scheduler model.
