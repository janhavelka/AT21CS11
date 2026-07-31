# AT21CS production-readiness finding registry

This is the master ownership map for the staged refactor. Each finding has one
implementation owner. Later stages verify earlier work but must not create a
parallel fix.

Status at creation: all findings are `OPEN`. Only the stage in `Owner` may
implement and close the root fix. `Downstream verification` is an independent
acceptance gate, not shared implementation ownership.

## Protocol and functional correctness

| ID | Finding | Evidence in current tree | Owner | Required owner evidence | Status | Downstream verification |
|---|---|---|---:|---|---|---|
| P-01 | `waitReady()` drives SI/O low during `tWR`, risking EEPROM/ROM/Lock/Freeze corruption | `src/AT21CS.cpp:464` | 1 | Bus-global high-only deadline | OPEN | Stage 3 tests every write class; Stage 8 scopes the released waveform |
| P-02 | Critical section is per byte, so preemption can create an unintended Stop between bytes | `src/platform/esp32/AT21CSEsp32Backend.cpp:243` | 1 | One frame callback and no byte callbacks | OPEN | Stage 4 physical proof; Stage 8 release/load captures |
| P-03 | `_activateDevice()` resets/discovers before ordinary operations | `src/AT21CS.cpp:1118` | 2 | Ordinary-operation reset count remains zero | OPEN | Stage 5 event oracle |
| P-04 | `readCurrentAddress()` resets away the pointer it needs | `src/AT21CS.cpp:519` | 2 | Public API removed; all reads are address-explicit | OPEN | Stage 5 API/symbol checks |
| P-05 | Core commits/uses the wrong timing policy across a speed transition | `src/AT21CS.cpp:258` | 2 | Command uses current speed, cache commits after ACK, and requests 650 us post-command high | OPEN | Stage 5 trace oracle; Stage 8 waveform |
| P-06 | Discovery emits a second host-low pulse not present in DS20005857I | `src/platform/esp32/AT21CSEsp32Backend.cpp:327` | 4 | One request pulse and correctly referenced sample instant | OPEN | Stage 8 S2/S3 capture |
| P-07 | Standard read sample is about 20 us; HS has no timing margin | `include/AT21CS/AT21CS.h:307`, backend `rxBit()` | 4 | Absolute-from-falling-edge sample targets | OPEN | Stage 8 S2/S3 HS/Standard captures |
| P-08 | `writeSecurityUser()` narrows `size_t` before range validation | `src/AT21CS.cpp:701` | 3 | `SIZE_MAX` and `0x10000` return before any I/O | OPEN | Stage 5 boundary oracle |
| P-09 | AT21CS11 Standard request touches the bus and damages health before rejection | `src/AT21CS.cpp:971` | 2 | Known AT21CS11 rejects before transport/health activity | OPEN | Stage 5 zero-I/O oracle |
| P-10 | Transport `bool` ACK/raw byte cannot represent timeout, line-stuck, or I/O failure | `include/AT21CS/Transport.h:64` | 1 | Typed result with distinct `NACK`, `TIMEOUT`, `LINE_STUCK`, `IO_ERROR` | OPEN | Stage 5 fault matrix |
| P-11 | Discovery failures are collapsed to generic absence | `src/AT21CS.cpp:221` | 2 | Exact transport result and detail survive initialization | OPEN | Stage 5 lifecycle/fault matrix |
| P-12 | Check Lock sends opcode `2h` with `R/W=1` and omits `0x60` | `src/AT21CS.cpp:744` | 3 | Exact `2h/W -> 0x60` trace with ACK/NACK on memory address | OPEN | Stage 5 trace oracle; Stage 8 sacrificial verification |
| P-13 | Freeze status invents an undocumented opcode `1h/R` query | `src/AT21CS.cpp:901` | 3 | Public query removed; no `1h/R` frame remains | OPEN | Stage 5 symbol/trace oracle |
| P-14 | CRC tests calculate their expectation with the function under test | `test/test_basic.cpp` | 5 | Independent CRC-8/Maxim vectors | OPEN | Stage 8 serial-number reads |
| P-15 | Arbitrary delay callbacks and cached CPU frequency can invalidate bit timing | ESP32 backend delay path | 4 | Timing owned by backend; no user delay inside bit slots; explicit DFS policy | OPEN | Stage 8 CPU/DFS/load captures |
| P-16 | Speed-query APIs reset/set the state before querying it | `src/AT21CS.cpp:948` | 2 | Destructive query APIs removed; cached speed plus explicit recovery | OPEN | Stage 5 event oracle |
| P-17 | Physical Reset, Discovery, Standard sampling, and post-frame timing do not meet the current datasheet contract | current timing tables/backend waveform | 4 | Universal 600 us Reset, one-pulse Discovery, absolute read sample, qualified post-frame high | OPEN | Stage 8 timing matrix |
| P-18 | Non-protected local reference documents stale CRC/timing/Lock/Freeze behavior | local datasheet reference | 7 | Reference corrected against verified DS20005857I without touching protected report | OPEN | Stage 8 documentation audit |
| P-19 | Exact 24-bit Manufacturer-ID equality rejects otherwise valid future silicon revisions in D2:D0 | exact `0x00D200`/`0x00D380` comparisons | 2 | Mask only D2:D0 for part classification; retain raw ID and revision | OPEN | Stage 5 tests revisions 0..7 for both parts |
| P-20 | A low-only Discovery sample can report a held-low or missing-pull-up line as a present device | Reset/Discovery backend | 4 | Separate 4 us presence sample and 25 us release-high check with `DISCOVERY_RELEASE` diagnostics | OPEN | Stage 5 malformed/fault oracle; Stage 8 held-low capture |
| P-21 | The datasheet's 5 ms `tWR` maximum is stated at 25 C, so a fixed wider-temperature production claim is unsupported without qualification | DS20005857I Table 3-3 | 8 | Qualify the 10 ms policy at exact released part/electrical/temperature profiles or block/narrow the claim | OPEN | Reviewed HIL-07/HIL-08 evidence and release documentation |

## Architecture, lifecycle, and diagnostic correctness

| ID | Finding | Evidence | Owner | Required owner evidence | Status | Downstream verification |
|---|---|---|---:|---|---|---|
| A-01 | Physical bus effects are stored per Driver, so two addressed devices can violate one another's write/reset constraints | Current class layout | 1 | Shared non-copyable `Bus` | OPEN | Stage 5 multi-device oracle; Stage 8 two-device HIL |
| A-02 | Failed `begin()` erases diagnostics and cannot later recover without the original config | `src/AT21CS.cpp:122` | 2 | Binding survives initialization failure; later `recover()` succeeds | OPEN | Stage 5 lifecycle oracle; Stage 8 reconnect HIL |
| A-03 | `begin()` does not unwind/retain backend ownership coherently | `src/AT21CS.cpp:100` | 1 | Backend externally owned; bind/end bus-silent; no double lifecycle | OPEN | Stage 5 lifecycle oracle |
| A-04 | `begin(driver.getConfig())` destroys its aliased input | `src/AT21CS.cpp:100`, public `getConfig()` | 2 | `getConfig()` removed; replacement validated before mutation | OPEN | Stage 5 API oracle |
| A-05 | Write failures do not expose partial/indeterminate effects | All multi-page and irreversible writes | 3 | `WriteResult`/`MutationResult` populated at every failure phase | OPEN | Stage 5 failure-phase oracle |
| A-06 | Driver is copyable despite hardware state and non-owning pointers | `include/AT21CS/AT21CS.h` | 1 | Compile-time non-copyable/non-movable Bus, Driver, backend | OPEN | Stage 5 compile-time contract |
| A-07 | Default-constructed Status is indeterminate; `inProgress()` is dead | `include/AT21CS/Status.h:29` | 1 | Deterministic defaults; dead method removed | OPEN | Stage 5 status oracle |
| A-08 | State guards and `isOnline()` permit or report transient states incorrectly | public state helpers and `_checkInitialized()` | 2 | Central transition/admission helpers | OPEN | Stage 5 table-driven state oracle |
| A-09 | Failed recovery can promote OFFLINE to DEGRADED | `src/AT21CS.cpp:351` | 2 | Explicit recovery outcome mapping | OPEN | Stage 5 lifecycle oracle |
| A-10 | `tick()` and `_lastTickMs` have no behavior | `src/AT21CS.cpp:284` | 2 | Removed | OPEN | Stage 5 symbol/API check |
| A-11 | Composite operations record helper success before returning semantic failure | serial/part detection | 2 | Raw helpers untracked; one final logical result tracked | OPEN | Stage 5 health oracle |
| A-12 | Settings snapshots expose callback/context pointers through copied Config | current `SettingsSnapshot` | 1 | Scalar-only snapshots | OPEN | Stage 5 public contract test |
| A-13 | Hidden discovery retries impose product policy and repeated destructive resets | `Config::discoveryRetries` | 2 | Field removed; one attempt per explicit API | OPEN | Stage 5 event-count oracle |
| A-14 | Bus has no shared Reset generation or physical-mode knowledge | Multi-device protocol behavior | 1 | Bus generation and successful-Reset HS flag | OPEN | Stage 5 multi-device oracle; Stage 8 two-device HIL |
| A-15 | Health counters wrap and latest failure is cleared by success | current health helpers | 2 | Saturating counters; separate `lastStatus` and persistent `lastError` | OPEN | Stage 5 saturation/health oracle |
| A-16 | Driver does not resynchronize safely after another device resets the shared Bus | Multi-device protocol behavior | 2 | Lazy generation adoption/Standard restoration with no reset loop | OPEN | Stage 5 multi-device oracle; Stage 8 two-device HIL |
| A-17 | A backend can claim nominal success with short data, missing ACK evidence, or no completed Stop unless Bus validates returned evidence | target whole-frame callback boundary | 1 | Exact legal result shapes; malformed results map to `IO_ERROR` while raw evidence is retained | OPEN | Stage 5 malformed-result matrix |
| A-18 | Rebinding/ending a physical Bus can strand existing Drivers or erase a retained write-high deadline | target shared-Bus lifecycle | 1 | Monotonic binding epoch; stale-Driver rejection; fallible hold-preserving `Bus::end()` | OPEN | Stage 5 rebind/end/epoch oracle; Stage 6 owner shutdown fixture |
| A-19 | Absolute-deadline addition can overflow and a stalled monotonic clock can leave an ostensibly bounded wait unbounded | current/target timing loops | 1 | Checked deadline arithmetic and fail-closed post-acceptance hold overflow | OPEN | Stage 4 independent wait guard; Stage 5 `UINT64_MAX`/stalled-clock tests |
| A-20 | Failure while sampling a write data-byte ACK can leave a fully delivered byte possibly accepted even when the proven ACKed-byte count is zero | target whole-frame result contract | 1 | Explicit `currentWriteByteMayBeAccepted` evidence, fail-closed high hold, and no replay | OPEN | Stage 3 effect mapping; Stage 4 backend emission; Stage 5 every-index fault matrix |

## Build, examples, packaging, and release quality

| ID | Finding | Evidence | Owner | Required owner evidence | Status | Downstream verification |
|---|---|---|---:|---|---|---|
| Q-01 | Advertised ESP-IDF consumer build fails on undefined GPIO register macros | ESP32 backend lines 77-84 | 4 | Clean S2/S3 native IDF builds at exact support endpoints | OPEN | Stage 7 packaged IDF consumers; Stage 8 release audit |
| Q-02 | Native tests cover only 25 cases and encode unsafe behavior | `test/test_basic.cpp` | 5 | Public-API matrix and event/timing oracle | OPEN | Stage 7 CI; Stage 8 independent test review |
| Q-03 | Hardware timing is unvalidated despite production claims | README and IDF port docs | 8 | Explicit HIL rows and raw capture hashes | OPEN | Maintainer review before stable finalization |
| Q-04 | Example helpers use repository-root includes and do not compile as consumers | `examples/common/Log.h:13` | 6 | Local includes and independent example builds | OPEN | Stage 7 packaged consumer builds |
| Q-05 | ESP-IDF command “parity” check accepts placeholders/no-ops | CLI checker and IDF example | 6 | Semantic manifest/handler checks | OPEN | Stage 7 static-contract CI |
| Q-06 | Version generation is stale and non-reproducible | generated `Version.h`, script | 7 | Deterministic `--check`; two runs byte-identical | OPEN | Stage 8 full-gate rerun |
| Q-07 | CLI parsing is unbounded/accepts invalid values; scan loses binding; destructive commands lack confirmation | examples/common and CLI | 6 | Bounded strict parser; address scan removed; exact confirmation gates | OPEN | Stage 7 semantic/package checks |
| Q-08 | Load-cell helper duplicates paging and falsely claims journaling/wear leveling | `LoadCellMap.h` | 6 | Helper removed or replaced with honest, tested serialization design | OPEN | Stage 7 docs/package checks |
| Q-09 | Required multi-device example is absent | examples tree vs `AGENTS.md` | 6 | Small shared-code multi-device CLI | OPEN | Stage 7 S2/S3 package builds |
| Q-10 | README, contributing, security, plans, and Doxygen inputs disagree with code | documentation tree | 7 | Link/API/version/doc checks | OPEN | Stage 8 final audit |
| Q-11 | Package contents are uncurated and omit referenced docs | `pio pkg pack` result | 7 | Explicit archive allowlist and unpacked builds | OPEN | Stage 8 clean-consumer rerun |
| Q-12 | CI omits IDF, consumers, sanitizers, docs, formatting, version, and package gates | workflow | 7 | Pinned, separated CI jobs | OPEN | Stage 8 full-gate audit |
| Q-13 | Example commands can invoke destructive/irreversible operations without a strong interlock | current example behavior | 6 | Exact confirmation tokens and automated rejection-path tests | OPEN | Stage 7 semantic checks; Stage 8 HIL authorization audit |
| Q-14 | Non-protected docs repeat false load-cell/CRC/timing/command claims | README and docs tree | 7 | Corrected docs and automated consistency checks | OPEN | Stage 8 documentation audit |
| Q-15 | HIL could destroy non-sacrificial parts or claim success without raw evidence | no current HIL gate | 8 | Mutable/sacrificial procedures, explicit matrix, captures and hashes | OPEN | Maintainer evidence review before stable finalization |
| Q-16 | `AGENTS.md` says to ready-poll during `tWR`, contradicting its continuous-high requirement and the safe fixed high-only Bus policy | `AGENTS.md` Protocol Rules | 1 | Replace only that line with the exact no-ACK-poll 10 ms Bus policy | OPEN | Stage 3 trace tests; Stage 7 docs audit; Stage 8 waveform |
| Q-17 | `AGENTS.md` demands exactly two CLIs while its ESP-IDF section and current tree also require a native IDF example | `AGENTS.md` Repository Model vs Framework Boundary | 7 | Deliberate exact three-example policy text matching shipped artifacts | OPEN | Stage 8 example-count and framework-boundary audit |

## Closure rule

A finding is closed only when:

1. its owning stage implements the root fix;
2. a named regression test or build/HIL artifact proves it;
3. downstream public documentation matches the final behavior; and
4. the named downstream verification passes;
5. no compatibility path retains the defective behavior; and
6. its registry `Status` is changed from `OPEN` to `CLOSED`, or to
   `ACCEPTED_RISK` only with an explicit maintainer record.

Passing the old 25-test suite or the token-only CLI checker is not closure
evidence.
