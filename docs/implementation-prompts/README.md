# AT21CS v2 implementation prompt pack

This packet continues from the current `feature/at21cs-v2-production` branch.
Stages 01-05 are completed history. Stages 06-08 finish examples, packaging,
documentation, CI, and physical qualification without adding an RTOS subsystem
to the library.

## Authority and conflict handling

Use these sources in this order:

1. `AGENTS.md` and an explicit maintainer decision in the current conversation;
2. the verified Microchip DS20005857I datasheet for protocol/electrical facts;
3. `00_SHARED_V2_CONTRACT.md` for current public behavior;
4. the one numbered prompt being executed;
5. `FINDINGS_REGISTRY.md` for traceability.

Prompts 01-05 describe completed checkpoints and are not additional frozen
contracts for later work. A current prompt may remove stale tooling or wording
left by a completed stage. Record that cleanup; do not stop merely because an
older prompt predicted a different cleanup stage.

Stop and ask the maintainer only when a conflict would change protocol behavior,
the public API, electrical safety, irreversible-operation authorization, or
would require discarding unrelated work. Resolve ordinary file ownership,
tooling, test, documentation, and stage-boundary ambiguity in favor of the
simpler current contract and record the decision.

## Authoritative datasheet

The authorized local file is:

```text
docs/AT21CS01-AT21CS11-1-Kbit-Serial-EEPROM-Data-Sheet-DS20005857.pdf
size: 2247216 bytes
SHA-256: 704577264C3B6C60B2D14BE83A229F34C86433CC8951516641FB1DE9EC5DB1A5
```

The hash and size, not the filename, establish authority. Never modify
`docs/AT21CS01_AT21CS11_complete_driver_report.md`.

## Simple library model

The library is synchronous. A call validates its complete input, performs its
documented bounded synchronous work, and returns the exact status and result
evidence. It creates no task, queue, scheduler, application-facing mutex,
internal retry/poll loop, persistence, logging, or product policy. A Backend
may use private bounded critical facilities for physical timing.

One physical SI/O wire uses:

```text
one Backend -> one Bus -> one or more Drivers with unique A2:A0 addresses
```

Devices on different SI/O pins use independent tuples:

```text
pin A -> Backend A -> Bus A -> Driver A (addressBits may be 0)
pin B -> Backend B -> Bus B -> Driver B (addressBits may also be 0)
```

“Instance” or “wire instance” means such a tuple. There is no public Channel,
owner, mailbox, request DTO, or asynchronous result API.

## RTOS integration

The safe default is one firmware task or cooperative loop owning every AT21CS
instance and invoking them sequentially. Application tasks may send commands to
that firmware-owned task, but its queue, deadlines, priorities, retries, result
routing, and shutdown policy are application code.

Separate tasks may each own one separate-wire tuple only after the selected
Backend has qualified simultaneous cross-instance timing. The current reference
contract does not promise concurrent timing-critical ESP32 transfers. Drivers
sharing one Bus must always be serialized by the same firmware owner.

A synchronous page write may occupy the owner for its frame and fixed 10 ms
released-high hold. Firmware needing bounded scheduling should use
`writeEepromPage()` and schedule the next request after it returns. Bus enforces
the protocol hold; firmware does not ACK-poll it.

## Hot-plug contract

Hot-plug is supported without an internal task:

1. Configure Backend, Bus, and Driver once.
2. If `begin()`/`initialize()` reports absence, keep the valid bindings and
   address claim.
3. If a separate detect signal exists, set `presencePin >= 0` and select its
   logical polarity with `presenceActiveHigh`. Firmware samples it through
   `Bus::readPresenceIndicator()` and owns any debounce.
4. If no signal exists, keep `presencePin == -1`. At each bounded polling event
   firmware may call `probe()` once while initialized/online or `recover()` once
   while uninitialized/offline. Prompt 06 uses a configurable 1,000 ms example
   period.
5. A successful `recover()` performs the required Reset/Discovery and restores
   normal Driver operation. Firmware may then read and compare the serial.

The detect input is one raw, externally biased, Bus-wide GPIO sample. It does
not identify a chip or address, and logical present only permits the real
protocol checks. `probe()` checks liveness only; it is not reconnect after
power-up. The library never wakes itself, debounces, schedules retries, tracks
attachment generations, or owns replacement policy.

Shipped example wiring/part choices are reproducible `AT21CS_EXAMPLE_*`
build-time overrides. Their committed detect-pin default is `-1`; no optional
pin number is invented as a board guarantee.

On a shared wire, detect and Reset are Bus-wide. One detect input cannot say
which addressed chip is present, and a recovery that reaches Reset changes the
shared generation. Separate Backend/Bus tuples have independent detect inputs
and state.

## Stage sequence from the current branch

| Stage | State | Purpose |
|---|---|---|
| 01 | completed | synchronous transport and Bus foundation |
| 02 | completed | Driver lifecycle, reads, identity, speed, and hot-plug recovery |
| 03 | completed | writes, Security, Lock, ROM zones, and Freeze evidence |
| 04 | completed | Arduino ESP32-S2/S3 Backend |
| 05 | completed | host tests and fault injection |
| 06 | completed | two bounded synchronous Arduino examples and RTOS guidance |
| 07 | completed | docs, clean packaging, CI, and RC metadata |
| 08 | remaining | independent final audit and scoped HIL |

Do not implement an RTOS owner fixture in any stage. Do not add
`test/consumer/firmware_owner/`, an owner task, mailbox, request/result DTOs, or
attachment-generation machinery to core, examples, tests, or package content.
Prompt 06 may contain a small fixed-state example polling helper; it is ordinary
firmware code called from Arduino `loop()`, not a library service or RTOS layer.

## Checkpoints

Use the branch `feature/at21cs-v2-production`. An implementation turn does not
commit. After a separate audit turn proves a stage complete, create one
non-empty commit whose message begins `stage NN:` and push it to the same branch.
Do not checkpoint blocked work. Do not amend, force-push, tag, release, publish,
or modify downstream repositories without separate authorization.

Prompts 01-07 do not run physical HIL. Prompt 08 alone may run physical or
irreversible tests under its explicit authorization rules. Software completion
and hardware qualification are reported separately; lack of hardware evidence
must not be presented as a software failure or as hardware success.
