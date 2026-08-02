# Prompt 08 — independent final audit and HIL release gate

## Outcome

Independently verify that the completed v2 library satisfies the shared
contract, close every finding, remove residual complexity, and collect the
hardware evidence required before a production-ready claim.

This is not a rubber-stamp review. Assume earlier stages contain mistakes.
HIL runs only against an immutable, maintainer-approved `2.0.0-rc.1` source
commit and exact firmware artifacts built from it.

This stage owns closure of P-21, P-22, Q-03, and Q-15. It independently
verifies every other finding without taking over its design ownership.

Prompt 08 is the sole physical-HIL stage. Prompts 01–07 may record
`HIL_ONLY` mappings and create structure/checkers, but they must not energize
hardware, require captures or measurements, or block their software checkpoint
solely because physical evidence is pending. All deferred Stage 04 waveform,
`tPUP`, CPU/DFS/load, interrupt-mask, page-duration, and fault-isolation
qualification is executed here against the immutable release candidate.

Every firmware build and physical HIL row in this stage uses Arduino through
the exact PioArduino pin frozen by the shared contract. Keep core
framework-independent, but do not install/select standalone ESP-IDF or add a
native-IDF build, example, component, package, CI, or validation path.

## Required working method

Read `AGENTS.md`, every file in this prompt pack, the complete final diff, and
DS20005857I. Inspect `git status`.

Before energizing hardware, require:

- a clean checkout at the maintainer-approved RC commit;
- the commit ID, source/package digest, and ELF/bin SHA-256;
- exact build configuration and toolchain lock;
- a run-record copy created from the Stage 7 schema;
- maintainer approval for any mutable or irreversible device operation.

If the candidate is dirty, moving, or cannot be reproduced, stop and report
`BLOCKED`. Evidence-only commits may be added later, but no code, public header,
build file, manifest, packaged documentation/example, or package-content change
may occur without invalidating affected HIL until the maintainer-authorized
exact-field stable-finalization exception defined in Prompt 07.

Spawn independent subagents that did not own the corresponding implementation:

1. protocol/datasheet and frame reviewer;
2. Bus ownership/concurrency/separate-wire topology reviewer;
3. lifecycle/state/health reviewer;
4. tests/fault-injection reviewer;
5. ESP32 S2/S3 timing reviewer;
6. Arduino/package/reproducibility reviewer;
7. simplification/dead-code reviewer.

If concurrency limits prevent all at once, run them in waves. Require concrete
file/line evidence. One lead integrator reconciles conflicting reviews.

For every proven software defect, stop the affected HIL rows, fix the root,
refactor it coherently, add regression tests, create a new maintainer-approved
RC commit/artifact set, and rerun the entire affected software/HIL matrix. Do
not patch an immutable RC in place and do not add shims or band-aids. Reuse the
established helpers, delete obsolete/parallel paths, and simplify before
accepting the new RC. Do not modify the protected complete-driver report. Do
not tag, release, publish, upload, or execute irreversible HIL without separate
maintainer authorization. Normal saga checkpoint/evidence commits and pushes
follow the packet README policy.

## Software audit checklist

### Contract and simplicity

- Exact public types/methods match `00_SHARED_V2_CONTRACT.md`.
- No v1 alias, legacy transport, compatibility wrapper, or duplicate backend.
- No `Core.h`, current-address API, byte callbacks, `waitReady()`,
  `_activateDevice()`, or opcode `1h/R`.
- Core has no framework/platform dependency.
- No dynamic allocation, exception, log, hidden retry, task, queue, mutex, or
  unbounded loop in library code.
- Driver/Bus/backend are fixed-size and non-copyable/non-movable.
- Shared helpers are actually reused; no duplicate paging/address/status/parser
  implementation remains.
- No I2C-style address scan command or scan-only Reset/Discovery path exists.
- `AGENTS.md` contains the exact Q-16 no-ACK-poll `tWR` rule and Q-17
  three-example rule; no unrelated governing constraint was weakened.

### Protocol

- Every event trace matches the shared frame contract.
- ACK/NACK phase is exact.
- transport errors remain distinct.
- bytes are MSb-first.
- every received frame host-NACKs its final byte.
- Reset/Discovery occurs only in initialize/recover.
- Check Lock is `2h/W + 0x60`.
- Freeze is `1h/W + 0x55 + 0xAA`; no status query is invented.
- CRC-8/Maxim known vectors pass.
- AT21CS11 Standard request performs zero I/O.

### Bus-global effects

- one Bus per SI/O wire;
- exactly one live Driver claim per address on a Bus, while the same address is
  valid on any number of independent Buses;
- write high deadline blocks all Drivers;
- no Reset/transfer during the deadline;
- Reset generation affects all Drivers;
- generation mismatch never causes a Reset loop;
- unknown physical mode requires explicit recovery;
- two independent buses share no mutable transport, descriptor, pin, timing,
  write-hold, Reset-generation, result, diagnostic, or callback-target state;
- the supported contract is correct interleaving through one owner, not
  unqualified simultaneous timing-critical execution from separate tasks.

### Lifecycle/state/health

- bind is zero-I/O and replacement-safe;
- initialization failure retains binding/error;
- absent-at-boot recover works;
- failed OFFLINE recovery stays OFFLINE;
- normal I/O admission is exact;
- one health update per public operation;
- validation errors are health-neutral;
- counters saturate;
- lastError persists;
- SLEEPING remains documented unreachable.

### Writes/mutations

- complete validation precedes I/O;
- every ambiguous phase has conservative evidence;
- no MAY_HAVE_COMMITTED operation is replayed;
- multi-page committed prefix is exact;
- Lock and ROM-zone changes verify;
- Freeze begins with conservative `ACCEPTED` evidence and is promoted to
  `VERIFIED` only by the Stage 3-defined private documented `1h/W` observation
  plus device-liveness proof; no invented `1h/R` query or cached assumption may
  verify it;
- irreversible examples require exact confirmation.

### Integration/release

- one static owner can wrap a fixed array of complete Backend/Bus/Driver
  channels;
- page write fits documented owner callback budget;
- examples are bounded and non-duplicative;
- the generic fixture under `test/consumer/firmware_owner/` is not shipped as
  an example and builds with two complete tuples on distinct SI/O pins, both
  using `addressBits=0`;
- only its owner context accesses live library objects/snapshots; its exact
  static FreeRTOS mailbox reserves terminal-result capacity, distinguishes
  owner errors from library errors, never drops an accepted result, and
  synchronizes published status before other tasks read it;
- reconnect uses bounded explicit `recover()` and serial reconciliation;
  presence is only a connector hint; attachment/replacement generations prevent
  stale queued EEPROM work and invalidate application calibration/association;
- operation budgets, checked deadline/backoff arithmetic, channel-only stop,
  and stop-all behavior match Prompt 06 exactly;
- shared-wire and separate-wire ownership, per-channel shutdown, serialization
  limits, and the application/harness responsibility boundary are documented;
- clean package consumers build;
- Arduino support claim equals the actual tested S2/S3 matrix, the core remains
  framework-independent, and no native-IDF support claim/artifact remains;
- generated metadata is deterministic;
- all documentation/API links are current.

## Full software commands

Run every command from Prompt 07 plus:

```text
rg -n "_activateDevice|waitReady|readCurrentAddress|writeByteReadAck|readByteSendAck|areRomZonesFrozen|OPCODE_FREEZE_ROM.*true" include src examples test
python tools/check_hil_evidence.py --structure-only
git diff --check
git status --short
```

Any production hit for a removed symbol is a release blocker.

## HIL evidence structure

Use and fill the Stage 7-created structure; do not replace its checker or
schema:

```text
docs/validation/HIL_MATRIX.md
docs/validation/RUN_RECORD_SCHEMA.md
docs/validation/runs/YYYY-MM-DD_<board>_<part>_<framework>.md
docs/validation/captures/
```

Each run record contains:

- immutable tested RC Git commit;
- source-tree/package digest and ELF/bin SHA-256;
- later evidence-only commit ID, if applicable;
- exact PlatformIO/PioArduino/Arduino/compiler versions;
- board/module revision;
- ESP32 CPU frequency and DFS setting;
- AT21CS part, package, and whether sacrificial;
- device label/serial, mutable-test authorization, and cumulative accepted-write
  count for this qualification;
- exact temperature and stabilization method;
- measured SI/O pull-up supply voltage;
- pull-up resistance;
- measured bus capacitance or wiring description;
- measured `tPUP`;
- presence wiring/polarity;
- exact cable part/type, conductor arrangement, length, and connector part(s);
- pull-up placement and value on every independent SI/O wire;
- protection/TVS/filter parts and their specified/measured capacitance,
  leakage, and clamp/interface topology;
- controller-end and far-device-end idle/rise measurements, including the
  disconnected-connector case;
- direct connection or level-shifter part/topology and both-side voltage limits;
- logic analyzer/oscilloscope model, firmware, sample rate, bandwidth, threshold,
  probe setup, and ground arrangement;
- calibrated timing-uncertainty budget including timebase, skew, threshold,
  loading, marker overhead, and quantization;
- instrumentation build flag, marker-pin mapping, and measured marker overhead;
- workload/interrupt/Wi-Fi/flash conditions;
- measured minimum and maximum for every timing item;
- functional test results;
- capture filenames or immutable artifact URLs, byte sizes, and SHA-256;
- pass/fail and unresolved anomalies.

Compact committed CSV/PNG/capture files must each be at most 5 MiB and all
committed HIL captures together at most 50 MiB. Larger raw analyzer/scope files
go to an immutable external artifact store; commit a manifest containing the
stable URL, exact byte size, SHA-256, capture-tool version, and export settings.
Every PASS row must retain raw evidence, not only screenshots or prose.

## Minimum HIL matrix

Copy these explicit rows into `HIL_MATRIX.md`. They are the minimum release
rows, not dimensions to expand into an unspecified Cartesian product:

Use these exact SI/O-powered electrical profiles:

- `E-DIRECT-3V3`: SI/O pull-up supply measured at 3.3 V, directly connected to
  an electrically compatible ESP32 open-drain GPIO; this is the only profile
  called nominal/direct.
- `E-AT01-HS-1V7-LS`: AT21CS01 device-side SI/O pull-up supply measured at
  1.7 V, High-Speed only, through a qualified open-drain bidirectional level
  shifter.
- `E-AT01-SS-2V7-LS`: AT21CS01 device-side SI/O pull-up supply measured at
  2.7 V, Standard Speed only, as a separate subrun through a qualified
  open-drain bidirectional level shifter.
- `E-AT11-HS-4V5-LS`: AT21CS11 device-side SI/O pull-up supply measured at
  4.5 V, High-Speed only, through a qualified open-drain bidirectional level
  shifter.
- `E-RISE-WORST-3V3`: 3.3 V direct profile with the exact qualified pull-up and
  capacitance corner selected before the run to produce the slowest supported
  rise while still measuring `tPUP <= 0.40 us`.
- `E-REMOVABLE-2CH-3V3`: two independent 3.3 V SI/O lines from one controller
  to two removable peripheral/load-cell connectors. Each line has its own
  controller-side pull-up so an empty connector remains deterministically high.
  Before the run, select and record the exact cable and length, connector,
  pull-up, protection/filtering, grounding/shield arrangement, and the measured
  capacitance/rise time at both controller and far-device ends. This profile
  qualifies only that recorded harness assembly; a different cable, length,
  connector, protection part, pull-up, or topology requires a new profile.

| Row | Board/framework | SDK endpoint | Device/mode/count | Required runtime conditions | Electrical/temperature profile |
|---|---|---|---|---|---|
| HIL-01 | ESP32-S2 Arduino | PioArduino 55.03.311 / Arduino-ESP32 3.3.11 | AT21CS11 HS, one | 80/160/240 MHz; DFS off/on; idle and Wi-Fi/interrupt/flash contention | `E-DIRECT-3V3`, 25 C |
| HIL-02 | ESP32-S3 Arduino | PioArduino 55.03.311 / Arduino-ESP32 3.3.11 | AT21CS01 HS and Standard, one | 80/160/240 MHz; DFS off/on; idle and Wi-Fi/interrupt/flash contention | separate `E-DIRECT-3V3` and `E-RISE-WORST-3V3` subruns, 25 C |
| HIL-03 | ESP32-S2 Arduino | PioArduino 55.03.311 / Arduino-ESP32 3.3.11 | AT21CS01 HS and Standard, one | 80/160/240 MHz; DFS off/on; contention | separate HS `E-AT01-HS-1V7-LS` and Standard `E-AT01-SS-2V7-LS` subruns, 25 C |
| HIL-04 | ESP32-S3 Arduino | PioArduino 55.03.311 / Arduino-ESP32 3.3.11 | AT21CS11 HS, one | 80/160/240 MHz; DFS off/on; contention | `E-AT11-HS-4V5-LS`, 25 C |
| HIL-05 | ESP32-S2 Arduino | PioArduino 55.03.311 / Arduino-ESP32 3.3.11 | AT21CS11 HS, one | 80/160/240 MHz; DFS off/on; contention | `E-RISE-WORST-3V3`, 25 C |
| HIL-06 | ESP32-S3 Arduino | PioArduino 55.03.311 / Arduino-ESP32 3.3.11 | two differently addressed devices on one SI/O Bus, HS | 80/160/240 MHz; DFS off/on; contention; cross-device write hold/reset generation | separate `E-DIRECT-3V3` and `E-RISE-WORST-3V3` subruns, 25 C |
| HIL-07 | ESP32-S3 Arduino | PioArduino 55.03.311 / Arduino-ESP32 3.3.11 | AT21CS01 HS and Standard, mutable device | fixed worst timing CPU/DFS/load condition selected from HIL-02/03 | separate HS `E-AT01-HS-1V7-LS` and Standard `E-AT01-SS-2V7-LS` subruns at exact ordered-part rated minimum, 25 C, and rated maximum temperature; exactly 100 accepted EEPROM page writes per mode per temperature |
| HIL-08 | ESP32-S3 Arduino | PioArduino 55.03.311 / Arduino-ESP32 3.3.11 | AT21CS11 HS, mutable device | fixed worst timing CPU/DFS/load condition selected from HIL-01/04/05 | `E-AT11-HS-4V5-LS` at exact ordered-part rated minimum, 25 C, and rated maximum temperature; exactly 100 accepted EEPROM page writes per temperature |
| HIL-09 | ESP32-S3 Arduino firmware-owner fixture | PioArduino 55.03.311 / Arduino-ESP32 3.3.11 | two AT21CS11 HS devices, both address zero, one per independent SI/O wire; channel A mutable | one firmware owner services both channels sequentially; observe B quiet while A's synchronous page write/hold runs, then read B immediately after return; disconnect/reconnect and held-low fault on A while B continues; independent Reset/generation/diagnostics/shutdown | `E-REMOVABLE-2CH-3V3`, 25 C |

Before running, replace qualitative electrical profile names in each run record
with measured SI/O pull-up supply voltage, pull-up resistance, capacitance, and
predicted/measured rise time derived from DS20005857I and the exact board/module
limits. The checker rejects
`TBD`, ranges without selected values, or an absent subrun.

For HIL-09, also replace the generic removable-profile description with one
exact built assembly and immutable bill-of-material identifiers. Prove each
line independently at both ends. With connector A unplugged, line A must remain
stably high and channel B must remain operational; a pull-up located only in
the removable load-cell body does not satisfy this profile.

For every qualified profile:

- measure `tPUP <= 0.40 us`, including probe and level-shifter loading;
- never expose an ESP32 GPIO or analyzer input to voltage above its documented
  absolute maximum;
- if the SI/O pull-up voltage is not directly compatible, use a qualified
  open-drain
  bidirectional level shifter, document its part/topology/propagation delay and
  added capacitance, and scope both sides simultaneously;
- reject push-pull high drive, unsafe clamp-current assumptions, and ad-hoc
  resistor dividers that break open-drain timing;
- use short ground connections and record analyzer/scope thresholds so a
  capture proves electrical levels as well as decoded bits.

## Mandatory waveform measurements

Use a HIL-only compile-time instrumentation build from the immutable RC to
toggle a spare marker GPIO with direct, bounded register writes at frame entry,
SI/O release, scheduled sample strobe, Stop completion, and critical-section
exit. Do not route marker generation through logging, callbacks, a separate
task, or the SI/O pin. For an owner-phase marker, emit the direct register
write inline in the owner task; never dispatch it to another task. Capture the
actual SI/O voltage and marker GPIO on a logic analyzer and an oscilloscope;
for level-shifted profiles capture both SI/O sides.
For HIL-09, add distinct marker states immediately before the owner request's
second deadline check and immediately after its terminal `ChannelResult` is
constructed; keep result-queue publication outside that interval.

Instrumentation floors are mandatory: oscilloscope bandwidth at least 100 MHz
and sampling at least 1 GS/s; logic-analyzer sampling at least 100 MS/s. Include
probe loading, threshold placement, timebase accuracy, channel skew, marker
overhead, and sampling quantization in an uncertainty budget. Combined timing
uncertainty must be at most 0.10 us for every reported protocol interval; a
decoded trace without that budget is not release evidence.

Record both instrumented and normal-production firmware hashes. Quantify marker
overhead, prove it does not move a timing edge outside the qualified margin,
then repeat the worst-case waveform using the non-instrumented production
configuration. If the RC lacks the required observability, fix it before HIL,
create a new RC, and restart affected rows rather than guessing from decoded
bytes.

Record min/max:

```text
Reset low
Reset recovery
Discovery request
Discovery sample instant from request falling edge
Discovery response/release
host SI/O release to device response
release-marker and sample-strobe offset/error
low-0
low-1
read drive-low tRD
read sample instant tMRS from falling edge
bit frame tBIT
recovery tRCV
Start/Stop high tHTSS
ACK sample
repeated Start
maximum inter-byte high gap
speed-change post-ACK high
write Stop to first allowed next low
firmware-owner page phase from second deadline check to terminal-result construction
```

Must prove:

- no second Discovery pulse;
- HS and Standard samples remain inside absolute windows with margin;
- no unintended Stop within a frame;
- SI/O continuously high for at least 10 ms after every accepted write;
- a second addressed Driver emits no traffic during that interval;
- before HIL-09's A write, B's `SettingsSnapshot` is initialized/`READY` with
  `consecutiveFailures==0`, `totalSuccess<UINT32_MAX`, and
  `lastOkUs<UINT64_MAX`; capture both B `SettingsSnapshot` and `BusSnapshot`;
- during a normal synchronous accepted write/hold on independent Bus A, the
  single owner issues no B call and both wires have the expected waveform;
  immediately after A returns and before any B call, both captured B snapshots
  are field-for-field unchanged;
- the subsequent B read succeeds: B `SettingsSnapshot.totalSuccess` increments
  by one, `lastOkUs` advances, `lastStatusCode==OK`, `totalFailures` is
  unchanged, and `consecutiveFailures` remains zero; B
  `BusSnapshot.lastTransfer` matches the commanded frame,
  `previousTransfer` equals the pre-read `lastTransfer`, and
  `lastWriteCycle` is unchanged; B binding epoch, Reset generation,
  configured/active speed, and persistent lastError code/detail remain
  unchanged;
- Stage 5's injected failed-wait oracle, not production HIL or a second task,
  proves that B remains callable after A returns with a retained high deadline;
- HIL-09's firmware-owner page phase, measured from immediately before the
  second deadline check through construction of the terminal result (excluding
  later result-queue backpressure), is at most 24 ms;
- marker/scope evidence agrees with direct SI/O edges and analyzer decode;
- GPIO release is genuinely high impedance/open-drain release, never
  push-pull-high drive.

Treat 10 ms as the library's fixed high-only scheduling policy, not as a
universal device guarantee beyond 25 C. Qualification applies only to the exact
board, ordered part, SI/O electrical profile, and temperature rows tested here,
unless a current verified Microchip document explicitly guarantees the same
bound over a broader range. A 25 C pass must not be extrapolated to temperature
corners. If any qualified row is not ready at 10 ms, change the software
contract, create a new RC, and rerun affected software/HIL gates.

## Functional HIL sequence

Before any EEPROM or Security write, the maintainer must identify a dedicated,
unprovisioned mutable test device and provide:

```text
CONFIRM_MUTABLE_TEST_DEVICE
```

Record the device label and exact allowed EEPROM/Security ranges. Back up those
ranges, verify the backup, avoid serial/identity/manufacturing data, and restore
mutable contents after testing where restoration is still permitted. Security
user writes require an explicitly unlocked test part and a separately named
scratch range. Never infer that a field device is safe to overwrite.

Maintain a per-device accepted page-write counter across all Stage 8 runs.
Qualification, stress, backup restoration, and diagnostic writes together must
not exceed 2,000 accepted EEPROM page writes per device. Stop before the budget,
not after it; report attempted/accepted counts and the datasheet endurance
margin.

Run in this order:

1. presence and Reset/Discovery;
2. repeated Manufacturer ID and serial/CRC reads;
3. EEPROM reads;
4. non-destructive speed transitions/reads;
5. EEPROM byte/partial/full/cross-page writes with readback;
6. Security user writes/readback on an unlocked part;
7. disconnect/reconnect, held-low, missing pull-up, complete SI/O pull-up-power
   removal/restoration, and recovery;
8. two-address shared-Bus reset generation and write isolation;
9. HIL-09 two-independent-Bus sequence: initialize both address-zero devices;
   require B initialized/READY with zero consecutive failures and capture both
   B `SettingsSnapshot` and `BusSnapshot`; write an authorized scratch page on
   A while observing that the one owner starts no B call; before any B call,
   prove both B snapshots unchanged; read B immediately and prove the exact
   Settings/Bus snapshot changes listed above; Reset/recover A and prove B is
   still unchanged apart from those expected B-local read fields; unplug A and
   repeatedly read B; reconnect A, call `recover()`, reread its serial, and
   publish replacement status; inject held-low on A; then terminally stop only
   channel A without ending or disturbing B;
10. temperature-qualified `tWR` hold/readback;
11. exact bounded stress under workload with health/counter review;
12. restore authorized mutable ranges and verify the restored bytes.

Do not proceed to the next class if waveform gates fail.

For HIL-07 and HIL-08, stabilize the part at the exact ordered-device rated
minimum, 25 C, and rated maximum temperature. HIL-07 performs exactly 100
accepted page writes per required HS/Standard electrical subprofile at each
temperature; HIL-08 performs exactly 100 at each temperature. Use the authorized
scratch pages. After every accepted Stop, prove SI/O stays high for at least
10 ms, issue the first subsequent read immediately after the library deadline,
and require exact readback. Record the minimum observed Stop-to-next-low
interval and all failures. Do not introduce ACK polling during `tWR`.

Run exactly two long-stress subruns: one on the HIL-07 AT21CS01 device and one
on the HIL-08 AT21CS11 device. For each device, select and record the single
worst qualified mode/electrical/temperature/CPU/DFS/load combination from its
earlier waveform and `tWR` results. Each subrun runs for at least 60 minutes and
includes at least 10,000 random/identity reads and exactly 1,000 accepted
alternating-pattern EEPROM page writes across the authorized scratch pages,
while Wi-Fi, interrupt/task contention, and flash/cache activity are active.
Thus the mandated qualification-plus-stress write counts are 1,600 for HIL-07
and 1,300 for HIL-08 before backup/restoration or diagnostics; the 2,000-write
per-device ceiling remains absolute. Each subrun passes only with:

- zero unexpected transport/protocol errors;
- zero readback mismatches;
- zero unintended Reset/Discovery or traffic during a write-high hold;
- no missed deadline or unbounded call;
- health counters matching the commanded operations and deliberately injected
  faults exactly.

If the first 60 minutes cannot reach the required counts, continue until both
the duration and counts are satisfied without exceeding the 2,000-write device
budget. Record the exact elapsed time. Any unexpected failure blocks release;
do not average it away.

## Irreversible HIL

Use only explicitly labeled, unprovisioned sacrificial parts after the
maintainer confirms:

```text
CONFIRM_SACRIFICIAL_AT21CS
```

Record a separate physical device label and pre-operation dump for each
destructive scenario. Do not reuse a device whose prior irreversible state
would make the result ambiguous. Then, one operation at a time:

1. On sacrificial device A, permanently enable each ROM zone. Verify readback
   and protected-write NACK, perform protocol Reset/Discovery, then remove
   the SI/O pull-up source long enough to discharge the SI/O-powered device and
   verify the enabled state and protection again after SI/O power restoration.
2. On sacrificial device B, permanently lock Security. Verify Check Lock and
   data-write NACK, repeat after protocol Reset/Discovery, then repeat after a
   complete SI/O pull-up-power removal/restoration.
3. On a fresh sacrificial device C with Freeze not previously issued and at
   least one ROM zone deliberately unset, record the initial zone map, set only
   the intended subset, and permanently Freeze. Verify that the deliberately
   unset zone remains unset and cannot subsequently be enabled, and confirm the
   documented subsequent Freeze-command address-NACK behavior without exposing
   it as a general query API. Repeat the zone/protection checks after
   Reset/Discovery and after complete SI/O pull-up-power removal/restoration.

For every SI/O power-cycle proof, record SI/O discharge/rise waveforms, measured
off time, the new startup/Discovery trace, and post-restoration readback. A
cached session value is not permanence evidence.

No automated CI or default example may execute these steps.

## Evidence checker

Use the Stage 7-created `tools/check_hil_evidence.py`:

- `--structure-only` verifies required fields/links/hashes;
- `--require-release-matrix` verifies every explicit HIL row and named subrun
  has a reviewed PASS record;
- it never fabricates or edits measurement data;
- it verifies all local/external capture byte sizes and SHA-256 values;
- it accepts later commits that change only the evidence paths allowed by the
  schema;
- it rejects evidence for another source/firmware/package digest and rejects
  any intervening code, build-surface, packaged example/doc, manifest, or
  package-content change;
- after authorized finalization, it applies Prompt 07's parsed exact-field
  version/qualification allowlist and rejects any other diff.

Run:

```text
python tools/check_hil_evidence.py --require-release-matrix
```

The checker passing proves evidence completeness and freshness, not the truth of
measurements; an independent maintainer still reviews traces and signs each row.

## Final finding closure

Update every `FINDINGS_REGISTRY.md` `Status`; use a separate detailed closure
table only as supporting evidence, not as a substitute:

```text
ID
root fix
regression test
build/HIL evidence
reviewer
status
```

Every open item blocks the production-ready claim unless the maintainer records
an explicit risk acceptance.

## Post-HIL rerun and stable finalization

After all explicit HIL rows and irreversible proofs pass:

1. rerun every Prompt 07 software command from clean consumers;
2. rerun the exact PioArduino 55.03.311 Arduino S2/S3 package-consumer matrix
   and the framework-neutral core-only consumer;
3. rerun docs, protected-report hash, format, native/sanitizer, examples,
   version consistency, package allowlist, and
   `check_hil_evidence.py --require-release-matrix`;
4. have the maintainer review and sign every HIL row and authorize stable
   finalization;
5. perform the Prompt 07 authorized finalization from `2.0.0-rc.1` to `2.0.0`;
6. rerun all gates again against the final package and record its digest.

README may cite production qualification only after these reruns. If the README,
changelog, or finalization changes package contents, the final archive and clean
consumers must be rebuilt. Any code/build/example change creates a new RC and
invalidates affected hardware evidence; the only exception is Prompt 07's
parsed exact-field stable-finalization allowlist.

## Exit criteria

- All software gates pass from clean consumers.
- Independent reviewers report no unresolved P0/P1 defect.
- All explicit HIL rows pass with reviewed raw evidence and verified hashes.
- HIL-09 proves independent address-zero load-cell channels, per-line
  controller-side pull-ups, disconnected-idle behavior, and fault/recovery
  isolation through one owner; claims remain limited to its exact harness.
- No irreversible operation ran on a non-sacrificial part.
- Mutable testing stayed within authorized ranges and the recorded 2,000-write
  per-device budget.
- `tWR`/10 ms high-only behavior passes both ordered parts at rated minimum,
  ambient, and maximum temperatures.
- ROM, Lock, and Freeze permanence is proven across Reset and complete SI/O
  pull-up-power removal; Freeze uses a fresh device with at least one
  deliberately unset zone.
- README may be changed from “release candidate / hardware qualification
  pending” to “production-qualified” only after this gate, maintainer review,
  and the post-HIL full rerun.
- Stable `2.0.0` metadata is consistent and the final package digest is
  recorded.
- No tag/publication is performed automatically.
