# Prompt 03 — safe writes, Security Lock, ROM zones, and Freeze

## Outcome

Implement all mutable and irreversible chip operations using Prompt 01's
bus-global high-only reservation and Prompt 02's lifecycle/health rules.

This stage owns write evidence and exact mutation protocols. It must not alter
frame transport or state vocabulary.

## Required working method

Read all shared contracts and completed Prompt 01/02 changes. Re-open
DS20005857I sections 7 and 9. Preserve unrelated changes.

Spawn subagents for:

1. EEPROM/Security page and partial-effect audit;
2. Lock/ROM/Freeze protocol audit;
3. evidence/fault-injection review.

Keep one integrator for production write helpers. Do not enable irreversible HIL
commands. Refactor to the single page-splitting/write-evidence path named below,
reuse it for EEPROM and Security, delete superseded write paths, and do not
band-aid ambiguous outcomes with retries or caches. Simplify before adding any
helper. Do not modify the protected report. Do not commit or publish.

## Sole owned findings

Close:

- P-08;
- P-12;
- P-13;
- A-05.

This stage supplies downstream command-level verification for P-01 but does not
redesign Prompt 01's Bus hold mechanism.

## File scope

```text
include/AT21CS/AT21CS.h
src/AT21CS.cpp
test/test_driver_writes.cpp
test/test_driver_mutations.cpp
```

Bus may be changed only if a test demonstrates its high-hold mechanism violates
Prompt 01.

## Exact internal helpers

Implement and reuse:

```cpp
Status _writePageRaw(
    uint8_t opcode,
    uint8_t address,
    const uint8_t* data,
    size_t length,
    WriteResult& result);

Status _writeRange(
    uint8_t opcode,
    uint8_t firstWritableAddress,
    uint8_t lastWritableAddress,
    uint8_t address,
    const uint8_t* data,
    size_t length,
    WriteResult& result);

Status _readSecurityLockStateRaw(bool& locked);
Status _readRomZoneStateRaw(uint8_t zoneIndex, bool& enabled);
Status _observeFreezeStateRaw(bool& frozen);
```

Only `_writePageRaw()` constructs a normal write `SingleWireTransfer` and it
always submits that descriptor through `Bus::_executeWrite()`.
`_writeRange()` owns the single page-splitting algorithm used by EEPROM and
Security convenience writes.

Use a `size_t` subtraction-based range helper:

```cpp
bool rangeFits(size_t start, size_t length, size_t capacity);
```

Never narrow length or compute `start + length` before validation.

## WriteResult semantics

Every public write initializes:

```cpp
result = WriteResult{};
```

Before any validation or state check.

For each physical page:

- device/memory NACK before a data ACK:
  - `lastPageEffect=NOT_ATTEMPTED`;
  - `lastPageBytesAccepted=0`;
- data NACK after zero accepted bytes:
  - `NOT_ATTEMPTED`;
- non-NACK failure while sampling the first or a later data-byte ACK, with
  `currentWriteByteMayBeAccepted=true`:
  - arm/retain the Bus write-high hold even when
    `dataBytesTransferred==0`;
  - `MAY_HAVE_COMMITTED`;
  - `lastPageBytesAccepted` remains the proven
    `dataBytesTransferred` count; never add the uncertain current byte;
- data NACK/transport/Stop uncertainty after one or more accepted bytes:
  - arm/retain Bus write-high hold conservatively;
  - `MAY_HAVE_COMMITTED`;
  - record accepted payload count;
- full frame accepted but high hold fails/clock stalls:
  - `MAY_HAVE_COMMITTED`;
- full frame plus proven high hold:
  - `COMMITTED`;
  - add full page length to `bytesCommitted`.

For a multi-page write, `bytesCommitted` is the proven contiguous prefix from
the caller's start. Never replay a MAY_HAVE_COMMITTED page.

`BUSY` begins only when at least one data byte may trigger programming and ends
after the Bus hold result is mapped.

## EEPROM writes

`writeEepromPage()`:

- non-null;
- length 1..8;
- address/range inside 0x00..0x7F;
- cannot cross an 8-byte page;
- exactly one frame;
- no retry.

`writeEeprom()`:

- same complete upfront validation;
- length >0;
- split at page boundaries;
- stop at first failure;
- never calls the public page method, because nested public tracking would
  double-count; call raw helper;
- one final health update.

Document maximum convenience-call blocking time:

```text
16 pages * (one bounded frame + 10 ms high hold)
```

TunnelMonitor-style owners use `writeEepromPage()`.

## Security writes

`writeSecurityUserPage()` and `writeSecurityUser()` use opcode B and only
addresses 0x10..0x1F.

Mandatory zero-I/O validation cases:

```text
length 0
length 9 for page API
address below 0x10
address above 0x1F
range ending above 0x1F
length 0x10000
length SIZE_MAX
page crossing
null data
```

After Security is locked, the first data byte NACK is a normal
`NACK_DATA`; because zero data bytes were accepted, do not wait 10 ms.

## Security Lock

### Read state

`readSecurityLockState(bool& locked)` uses one tracked logical operation and the
exact raw frame:

```text
Start
D(2,0) -> must ACK
0x60   -> ACK means unlocked; NACK means locked
Stop
```

Special semantic rule:

- NACK at `DEVICE_ADDRESS_WRITE` is an error;
- NACK at `MEMORY_ADDRESS` is successful state observation `locked=true`.

### Permanently lock

`permanentlyLockSecurity(MutationResult&)`:

1. reset result;
2. precheck through raw helper;
3. if already locked: return OK, `VERIFIED`, `alreadyApplied=true`, no mutation
   frame;
4. transmit opcode 2 write, memory `0x60`, data `0x00`;
5. on ambiguous accepted-data/Stop/hold failure: return exact failure with
   `MAY_HAVE_COMMITTED`;
6. after a fully ACKed frame and proven high hold, set `effect=ACCEPTED` before
   attempting the recheck;
7. a recheck transport/NACK failure returns that exact failure while preserving
   `effect=ACCEPTED`;
8. only observed locked returns OK with `VERIFIED`;
9. observed unlocked returns `VERIFY_MISMATCH` with `effect=ACCEPTED`;
10. never retry the irreversible command.

The method name must retain `permanently`.

## ROM zones

Use the exact mapping in the shared contract.

`readRomZoneState()`:

- validates zone index 0..3 before I/O;
- random-reads one register byte using opcode 7;
- `0x00 -> enabled=false`;
- `0xFF -> enabled=true`;
- any other value returns `VERIFY_MISMATCH` with the observed byte in detail.

`permanentlyEnableRomZone()`:

1. pre-read;
2. if already enabled: OK/VERIFIED/alreadyApplied;
3. write `0xFF` to the zone register;
4. high-only hold;
5. after a fully ACKed frame and proven high hold, set `effect=ACCEPTED`;
6. post-read and require enabled;
7. a post-read transport/NACK failure returns that exact failure while
   preserving `effect=ACCEPTED`;
8. observed disabled returns `VERIFY_MISMATCH` with `effect=ACCEPTED`;
9. only observed enabled promotes the result to `VERIFIED`;
10. a failure before a proven high hold remains `MAY_HAVE_COMMITTED`;
11. no automatic replay.

## Freeze

`permanentlyFreezeRomZones()` sends exactly:

```text
D(1,0), 0x55, 0xAA, Stop, high-only hold
```

Rules:

- delete `areRomZonesFrozen()` and every opcode `1h/R` path;
- add no public Freeze-state query and no session Freeze cache;
- `_observeFreezeStateRaw()` is private, untracked, and uses the documented
  opcode `1h/W` address response without ever sending `1h/R`:
  1. send `D(1,0)` and then Stop;
  2. address ACK means the registers are not frozen; the early Stop aborts the
     incomplete Freeze sequence, so return `frozen=false`;
  3. address NACK nominally means frozen per DS20005857I section 9.2.3, but at
     the first byte it is electrically indistinguishable from an absent target;
  4. after that NACK, perform one raw Manufacturer-ID read at the same address;
     only a successful ID matching the already detected part confirms liveness
     and permits `frozen=true`;
  5. if liveness cannot be confirmed, return `INDETERMINATE`; retain the exact
     physical results in Bus diagnostics and do not claim already-applied state;
- precheck with `_observeFreezeStateRaw()`:
  - confirmed frozen -> OK, `VERIFIED`, `alreadyApplied=true`, no mutation
    frame;
  - confirmed not frozen -> proceed;
  - indeterminate/error -> return before mutation with `NOT_ATTEMPTED`;
- transmit the complete Freeze frame exactly once;
- a device-address NACK after a confirmed-not-frozen precheck is still
  `INDETERMINATE`; physical removal/state change between frames cannot be
  distinguished and must not be reported as success;
- accepted data followed by Stop/high-hold uncertainty returns the exact error
  with `MAY_HAVE_COMMITTED`;
- after a fully ACKed frame and proven high hold, set `effect=ACCEPTED` before
  postchecking;
- postcheck once through `_observeFreezeStateRaw()`:
  - confirmed frozen -> OK and promote to `VERIFIED`;
  - confirmed not frozen -> `VERIFY_MISMATCH`, preserving `ACCEPTED`;
  - indeterminate/transport failure -> return that failure, preserving
    `ACCEPTED`;
- never demote an accepted mutation merely because verification failed;
- never retry automatically.

## Required tests

Add exact event-trace tests for:

1. every EEPROM page position/length 1..8;
2. 0/1/7/8/9/127/128-byte range edges;
3. page splits at addresses 0,1,7,8,120,127;
4. NACK at device, memory, and every data index;
5. transport failure at every phase;
6. full frame followed by wait failure/early clock/stalled clock;
7. zero Bus events during the entire 10 ms hold;
8. second Driver blocked during first Driver hold;
9. every Security overflow/invalid case above with zero I/O;
10. locked Security first-data NACK causes no hold;
11. exact Check Lock frame and semantic memory-address NACK;
12. Lock already applied, successful verified, verify mismatch, ambiguous hold;
13. all ROM zone mappings and invalid returned byte;
14. zone already applied, successful verified, ambiguous failure;
15. exact private Freeze observation frame and early-Stop abort on address ACK;
16. observation NACK plus matching Manufacturer ID -> confirmed frozen;
17. observation NACK plus failed/mismatched Manufacturer ID -> INDETERMINATE;
18. confirmed already-frozen precheck performs no mutation frame;
19. exact complete Freeze mutation frame;
20. Freeze mutation address NACK after precheck -> INDETERMINATE;
21. accepted Freeze plus confirmed postcheck -> VERIFIED;
22. accepted Freeze plus postcheck mismatch/failure preserves ACCEPTED;
23. no event trace can contain opcode `1h` with R/W=1;
24. no Freeze session cache remains;
25. no ambiguous write/mutation is replayed;
26. Lock/ROM postcheck failure and mismatch preserve ACCEPTED;
27. one logical health update per multi-page/mutation public call;
28. timeout/line-stuck/I/O during each data-byte ACK sample reports the proven
    prefix plus `currentWriteByteMayBeAccepted=true`; the first-byte case has
    zero proven bytes but still holds, returns `MAY_HAVE_COMMITTED`, and is
    never replayed.

## Verification

```text
python tools/check_core_timing_guard.py
python -m platformio test -e native
git diff --check
git status --short
```

Additionally:

```text
rg -n "waitReady|ACK.?poll|areRomZonesFrozen|OPCODE_FREEZE_ROM.*true" include src test
```

Expected: no production hit for any removed/unsafe behavior.

## Exit criteria

- Every mutable API has conservative machine-readable evidence.
- SI/O is never driven low during a write hold.
- Lock/ROM operations verify where the datasheet provides readback.
- Freeze verification uses only the documented `1h/W` ACK/NACK semantic plus a
  same-address liveness read; it never uses an invented `1h/R` query or cache.
- All validation occurs before any frame.
