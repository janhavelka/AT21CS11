# Prompt 08 — independent final audit and scoped HIL release gate

## Outcome

Independently audit the completed synchronous library and, when the required
hardware and authorization are available, qualify the actual supported
board/chip configurations. Keep software completion separate from physical
qualification and never invent evidence.

This stage does not add an RTOS owner framework. HIL calls library instances
synchronously from one test task/loop.

## Authorization boundary

Read the packet authority hierarchy and verify the exact datasheet artifact.
Inspect the immutable candidate commit, clean worktree, code, tests, examples,
package, CI, documentation, and finding registry before hardware use.

Non-persistent HIL may run only on explicitly identified hardware. Reset,
Discovery, and speed changes may still drive the protocol while leaving EEPROM
contents and irreversible configuration unchanged.
EEPROM writes require maintainer confirmation that the selected address range is
sacrificial or backed up. Permanent Security Lock, ROM-zone enable, and ROM
Freeze require a separately identified sacrificial device and explicit
per-operation authorization. Never infer that authorization from this prompt.

If hardware, instruments, or authorization are unavailable, finish and report
the software audit and mark physical rows `HIL_PENDING`. Do not call a
structure-only build hardware success.

## Software audit

Verify from current production paths and independent tests:

### Simplicity and public boundary

- exact public declarations match installed headers and docs;
- library calls are synchronous and bounded;
- no v1 facade, duplicate transport, current-address API, raw byte callback,
  hidden activation, or ACK-polling path remains;
- core has no Arduino/ESP-IDF/FreeRTOS/product dependency;
- no library task, queue, scheduler, application-facing mutex, retry/backoff,
  logger, persistence, attachment-generation, or owner DTO exists; private
  Backend timing-critical facilities remain allowed;
- ownership objects are fixed-size and noncopyable/nonmovable; value structs
  remain copyable;
- only two shipped Arduino examples exist;
- no native-IDF claim or artifact remains.

### Protocol and evidence

- framing, address construction, MSb order, ACK/NACK phases, host read NACK,
  Reset/Discovery, CRC, speed changes, and opcodes match DS20005857I;
- AT21CS11 Standard Speed rejection is callback-free;
- reads are transactional;
- write committed-prefix/accepted-byte/ambiguity evidence is conservative;
- no possibly committed mutation is replayed;
- Lock, ROM zones, and Freeze use documented commands only;
- complete input validation, including `SIZE_MAX`, occurs before I/O.

### Ownership and lifecycle

- exactly one Backend/Bus exists per physical wire;
- addresses are unique within a Bus and reusable on independent Buses;
- write hold and Reset generation affect Drivers sharing a Bus only;
- independent Buses share no mutable state;
- startup and shutdown order match the shared contract;
- absent-at-boot keeps binding and later explicit `recover()` succeeds;
- `probe()` remains liveness-only;
- one firmware task/loop can safely serialize multiple synchronous wire
  instances;
- simultaneous separate-task ESP32 calls are not advertised as qualified.

### Examples, docs, package, and CI

- both examples build for S2/S3 and use production code;
- malformed CLI input reaches zero Driver calls;
- destructive page write needs exact confirmation;
- irreversible commands are absent from shipped examples;
- docs explain synchronous RTOS wrapping and explicit hot-plug recovery;
- clean consumers build from package content without checkout access;
- metadata is deterministic and remains `2.0.0-rc.1` until final approval.

Run the complete Stage-07 software command set and `git diff --check` before
starting HIL.

## HIL evidence record

For each physical run record:

- immutable candidate commit and firmware hash;
- board/module revision and framework/toolchain versions;
- exact ordered EEPROM part and device marking;
- SI/O pin, pull-up voltage/resistance, wiring topology, and presence-pin use;
- supply voltage and ambient/device temperature actually observed;
- instrument model, sample rate/resolution, thresholds, and probe loading;
- exact test command, expected result, actual status/detail/effect, and raw
  capture filename/hash;
- PASS, FAIL, or HIL_PENDING with a concise reason.

Qualification applies only to the recorded electrical setup. Do not generalize
to untested cables, level shifters, voltages, temperatures, or harnesses.

## Minimum practical HIL matrix

Use the smallest available matrix that covers every advertised capability:

| Row | Required coverage |
|---|---|
| HIL-01 | ESP32-S2 Arduino, one AT21CS11 at High Speed: startup, identity, random read, page write if authorized, and write-high waveform |
| HIL-02 | ESP32-S3 Arduino, one AT21CS01 at High and Standard Speed: startup, identity, random read, speed change, page write if authorized, and waveforms |
| HIL-03 | two differently addressed devices sharing one SI/O wire: independent Driver identity, shared Reset generation, and Bus-wide write hold |
| HIL-04 | two independent SI/O wires, both address zero: serialized calls, failure/hot-plug on A, continued operation on B, and independent shutdown |

If a row cannot run, record `HIL_PENDING`; do not substitute a host test. A
release may describe software support while clearly stating which physical rows
remain unqualified, but it must not claim those rows as production-qualified.

## Required waveform observations

Use instrumentation capable of resolving the datasheet windows with visible
margin. Capture and measure, where applicable:

- Reset low/recovery and the single Discovery request/sample/release sequence;
- Start/Stop high timing;
- low-0, low-1, read-low, read sample, bit period, and ACK sample;
- repeated Start and final host NACK on reads;
- speed-change post-command high interval;
- continuous released-high interval after an accepted write;
- absence of traffic from another Driver on the same Bus during that hold;
- high-impedance/open-drain release rather than push-pull high.

Do not require a firmware-owner request/result marker or queue timing. Measure
the actual synchronous library call and SI/O waveform.

The library uses a fixed 10 ms released-high software policy. Document the exact
part/temperature conditions under which it was observed. Do not claim that HIL
extends a datasheet limit beyond the tested conditions.

## Functional hot-plug sequence

For the independent-wire row:

1. Start A and B and read both serial numbers.
2. Remove or power down A using the approved setup.
3. Confirm A reports the exact absence/failure while B still reads correctly.
4. Confirm no automatic retry or hidden background traffic occurs.
5. Reattach/power A and call `recover()` explicitly.
6. Read A serial again and let the harness compare it with the previous value.
7. Confirm B state and diagnostics changed only for B calls.
8. Shut down A in Driver -> Bus -> Backend order and confirm B remains usable;
   then shut down B.

For the shared-wire row, remember that recovery Reset is Bus-wide. Verify the
other Driver observes the shared generation change and resynchronizes without a
Reset loop.

## Mutable and irreversible HIL

When EEPROM writes are authorized:

- save the original bytes;
- write one page with a documented pattern;
- verify readback and the continuous released-high interval;
- restore the original bytes only if restoration is itself authorized;
- preserve `WriteResult` evidence on every failure and never blindly replay an
  ambiguous write.

Permanent Security/ROM/Freeze operations are optional release evidence unless
the maintainer explicitly requires physical qualification. If authorized, use
only a named sacrificial device, record pre-state, invoke exactly once, record
effect evidence, and verify through documented read/observation paths. Never run
them on normal development or production hardware.

## Finding reconciliation and finalization

Reconcile the registry with current code and evidence:

- completed software findings become `CLOSED` with named tests/checks;
- hardware-only findings become `CLOSED` only for the exact qualified scope or
  remain `HIL_PENDING`/`OPEN` with honest limitations;
- product/harness behavior outside the library remains explicitly out of scope,
  not silently treated as library success.

After all required software gates and the maintainer-required HIL rows pass, the
maintainer may authorize stable `2.0.0`, a fresh deterministic metadata build,
and the final checkpoint. This prompt alone does not authorize tagging,
publishing, or uploading.

## Exit criteria

- Independent software audit is complete and reproducible.
- The library remains a simple synchronous component suitable for an external
  firmware task.
- Explicit hot-plug recovery works without internal tasking or retries.
- HIL claims match only raw recorded evidence and clearly list pending rows.
- No irreversible action occurred without exact authorization.
- Registry, documentation, package, examples, and metadata agree with the final
  qualified scope.
