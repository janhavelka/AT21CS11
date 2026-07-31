# AT21CS v2 implementation prompt pack

This directory turns the production-readiness audit into a sequence of narrow,
ordered implementation prompts. It is intentionally not a collection of
independent suggestions. The prompts form one migration and must be executed in
order against the same branch.

The target is a breaking `2.0.0` refactor. Stage 7 produces the
`2.0.0-rc.1` release candidate; stable `2.0.0` metadata is authorized only
after Stage 8 HIL passes and the maintainer approves finalization. The library
is not currently used by firmware, so preserving the unsafe `1.x` API is
explicitly out of scope.

## Authoritative inputs

Every implementation session must read, in this order:

1. Repository `AGENTS.md`.
2. `00_SHARED_V2_CONTRACT.md`.
3. `FINDINGS_REGISTRY.md`.
4. The prompt for the current stage.
5. The current official Microchip datasheet:
   [AT21CS01/AT21CS11 Data Sheet DS20005857I, May 2026](https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/AT21CS01-AT21CS11-1-Kbit-Serial-EEPROM-Data-Sheet-DS20005857.pdf).

Before relying on the datasheet, verify the downloaded file exactly:

```text
size:   2145807 bytes
SHA-256: 59E1C04C9C36DAC48058275BDB95B240DF8445DD166487AA3670601926B2FBB5
```

Do not substitute a search result, cached older revision, HTML error page, or
locally extracted text for this verified source. If the exact file cannot be
obtained and verified, stop protocol-affecting work and report the stage
`BLOCKED`.

The repository's older extracted/reference documents are secondary aids. Where
they conflict with DS20005857I, DS20005857I wins. In particular:

- the serial-number CRC is reflected CRC-8/Maxim;
- read sampling time `tMRS` is measured from the read-frame falling edge;
- Check Lock uses opcode `2h` with `R/W=0` plus a `0x6X` address;
- opcode `1h` has no documented `R/W=1` Freeze-status query.

Never modify
`docs/AT21CS01_AT21CS11_complete_driver_report.md`. It is protected by
`AGENTS.md`.

Two current `AGENTS.md` policy lines are themselves audited findings:

- Q-16: its `tWR` ready-polling wording conflicts with the required continuous
  released-high interval and is corrected exactly in Stage 1;
- Q-17: its two-example repository rule conflicts with its native ESP-IDF
  example/parity section and the existing tree; Stage 7 resolves that policy
  deliberately after the example implementation is complete.

Those two named edits do not authorize any other weakening or rewriting of
`AGENTS.md`.

## Sibling-project comparison boundary

The audit uses these sibling repositories as concrete integration references:

```text
../MB85RC
../PCA9555
../TCA9548A
../INA228
../TunnelMonitor-node
```

Carry forward the good firmware-facing patterns: externally supplied transport,
validated bind/begin/recover lifecycle, deterministic `Status`, scalar cached
diagnostics, fixed buffers, explicit owner scheduling, strict examples,
consumer builds, and package/CI checks. `MB85RC` is the closest EEPROM/API
comparison; `TunnelMonitor-node` is authoritative for ownership shape and
blocking budgets.

Do not copy I2C-only behavior into this single-wire protocol: no `TwoWire`,
address scanner, ACK-ready polling during `tWR`, current-address convenience
read, I2C buffer assumptions, or per-device storage of effects that belong to
the physical wire. Do not copy a sibling's asynchronous job surface merely for
API parity; v2 remains synchronous and single-owner because that is the smaller
contract required here.

## Required execution order

| Order | Prompt | Sole design ownership |
|---:|---|---|
| 1 | `01_BUS_TRANSPORT_FOUNDATION.md` | Public v2 types, shared physical bus, frame transport, bus-global reset/write effects |
| 2 | `02_DRIVER_LIFECYCLE_READ_ID_SPEED.md` | Driver lifecycle, state machine, health, reads, identity, speed |
| 3 | `03_WRITES_SECURITY_ROM.md` | Page writes, write evidence, Security Lock, ROM zones, Freeze |
| 4 | `04_ESP32_PHY_ARDUINO_IDF.md` | S2/S3 physical timing, GPIO, atomic frames, Arduino and native IDF |
| 5 | `05_NATIVE_TESTS_AND_FAULT_INJECTION.md` | Exhaustive host oracle, fault injection, sanitizers |
| 6 | `06_EXAMPLES_AND_TUNNELMONITOR_INTEGRATION.md` | Minimal safe examples and owner-style integration |
| 7 | `07_DOCS_PACKAGING_CI_RELEASE.md` | Documentation, clean consumers, deterministic versioning, CI and package |
| 8 | `08_FINAL_AUDIT_AND_HIL.md` | Independent final audit and hardware release gate |

Stages may add focused tests for the code they own. Stage 5 is the exhaustive
coverage pass; it must not silently redesign contracts owned by earlier stages.
If a test proves an earlier contract wrong, stop, document the contradiction,
and fix the owning stage rather than adding an adapter.

Stage 4 must not build the still-unmigrated shipped examples after Stage 1
removes the v1 API. It creates dedicated v2 physical-layer smoke consumers
under `test/consumer/phy_smoke/arduino/` and
`test/consumer/phy_smoke/idf/`, including backend-enabled and
backend-disabled forms where applicable. Stage 6 alone migrates the shipped
examples. Stage 7 reuses the smoke fixtures for clean package verification
instead of creating another implementation.

## Mandatory working method for every prompt

Each implementation prompt repeats the following rules; they are collected here
to make their intent explicit:

- Start by inspecting `git status` and preserve unrelated/user changes.
- Spawn subagents for independent inspection, test design, and final review.
  Keep one integrator responsible for shared-file edits.
- Use subagents read-only until the integrator freezes the stage design.
- Prefer deletion and coherent refactoring over wrappers, aliases, compatibility
  shims, or a second implementation.
- Reuse correct existing opcode, validation, CRC, page-splitting, status, and
  example helper code instead of copying it.
- Do not introduce dynamic allocation, exceptions, logging, hidden retries,
  tasks, queues, mutexes, or recovery policy into library code.
- All waits and callback contracts are bounded. All buffers are caller-owned or
  fixed-size.
- Add a regression test for every bug fixed in the stage.
- Run the stage's exact verification commands and report exact results.
- Do not commit, tag, publish, upload, or run irreversible hardware commands
  unless the maintainer explicitly requests it.

## Coherence invariants

The following invariants apply across every stage:

1. There is exactly one `AT21CS::Bus` object per physical SI/O wire.
2. One or more `AT21CS::Driver` objects may reference that bus, one per A2:A0
   address. Devices on different SI/O pins use different buses.
3. `Bus` owns bus-global Reset generation, frame serialization contract, and
   the post-write high-only deadline. `Driver` owns one device's address,
   identity, desired speed, lifecycle, and health.
4. The library is single-owner/non-thread-safe. Firmware serializes access.
   The library does not create a mutex.
5. A physical Reset affects every device on the bus. A write high-only interval
   blocks every device on the bus.
6. All ordinary reads are random reads. The public current-address API is
   removed.
7. A backend executes one complete uninterrupted frame. Per-byte callbacks are
   removed.
8. Protocol NACK is distinct from transport failure and carries an exact phase.
9. No command is issued while the bus write-high deadline is active.
10. No normal Driver operation performs hidden Reset, Discovery, retry, or bus
    recovery.
11. The core remains independent of Arduino, ESP-IDF, FreeRTOS, ESP32 GPIO
    headers, and board pin choices.
12. A failed initialization retains a valid binding so explicit recovery can
    succeed after an absent-at-boot device appears.
13. Validation and precondition errors perform zero bus I/O and do not change
    transport health.
14. Every public logical operation updates health at most once.
15. Irreversible APIs have names beginning with `permanently`, conservative
    effect evidence, and safe example-level confirmation.
16. There is no I2C-style address scan command. Selecting or changing A2:A0 is
    explicit; scanning must not be simulated with destructive Reset/Discovery.
17. The result is not called production-ready until Stage 8 HIL evidence passes.
18. Stage 7 metadata remains `2.0.0-rc.1`. After Stage 8 passes, the maintainer
    may authorize the stable `2.0.0` metadata/changelog update and full gate
    rerun; no prompt commits, tags, publishes, or uploads automatically.

## Stage completion record

After each stage, append a short record to the pull request or work log:

```text
Stage:
Commit/worktree:
Prompt contract deviations:
Files changed:
Tests added:
Commands passed:
Commands unavailable/failed:
Remaining release blockers:
Reviewer findings:
```

Do not mark the overall migration complete until every finding in
`FINDINGS_REGISTRY.md` is `CLOSED` or explicitly accepted by the maintainer.
