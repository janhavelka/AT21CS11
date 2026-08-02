# Prompt 04 — ESP32-S2/S3 PHY for Arduino

## Outcome

Implement one explicit `Esp32Transport` for ESP32-S2/S3 that satisfies the
frozen whole-frame transport contract under Arduino-ESP32. This stage proves
the software implementation and declared Arduino build matrix; Prompt 08 alone
owns physical qualification.

This is a PHY software stage. Do not change Driver feature semantics to hide a
PHY failure, and do not interpret "PHY" as authorization for physical HIL.

## Required working method

Read all shared contracts, completed Stages 1–3, DS20005857I timing tables, and
the selected Arduino-ESP32 low-level GPIO/timing API documentation supplied by
the pinned PioArduino platform. Preserve unrelated changes.

Spawn subagents for:

1. S2 GPIO/timing implementation review;
2. S3 GPIO/timing implementation review;
3. Arduino/core build-boundary review;
4. host timing-oracle and deferred-HIL mapping review.

Keep one integrator for shared backend code. Require a final cross-target review.
Refactor one backend at the root, reuse one frame/timing implementation for S2
and S3 under Arduino, delete the old byte callbacks, and do not band-aid target
differences with duplicate PHYs. Simplify every timing path that cannot be
verified by the software oracle. Do not run HIL or use irreversible chip
commands. Do not modify the protected report. Follow the packet README's saga
checkpoint policy; do not tag, release, publish, or upload.

## Sole owned findings

Close:

- P-06;
- P-07;
- P-15;
- P-17;
- P-20;
- Q-01.

This stage supplies software/backend evidence for P-02 and P-05 without
reopening their transport/core contracts. Physical downstream proof is
`HIL_ONLY` and belongs exclusively to Prompt 08; its absence does not block the
Stage 04 software checkpoint.

## Support matrix

Target:

- ESP32-S2 and ESP32-S3;
- Arduino-ESP32 3.3.11 through the maintainer-authorized PioArduino pin
  `https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip`;
- `framework = arduino` only.

Do not install/select a standalone ESP-IDF SDK, download PlatformIO's
`framework-espidf` package, invoke `idf.py`, or add a `framework = espidf`
environment. Arduino-ESP32 may internally use its bundled ESP-IDF-derived
libraries; that does not authorize a native-IDF build or support claim.

Remove any native-IDF Stage 04 fixture and native-IDF-only root build path from
the current worktree. Remove `espidf` from `library.json.frameworks`; remove
`idf_component.yml` and an IDF-only root `CMakeLists.txt` rather than retaining
unsupported compatibility metadata. Do not replace them with a second adapter.
Prompt 06 removes the obsolete native-IDF example, and Prompt 07 removes or
archives the remaining stale IDF documentation/checkers and verifies package
contents.

Keep core, Bus, Driver, and the Backend interface framework-independent, but do
not implement or advertise another framework now. Prompt 07 must keep
`library.json`, README, package contents, and CI limited to the Arduino support
matrix actually built.

## Exact platform API

Implement the class declared by the shared contract:

```cpp
struct Esp32TransportConfig {
  int sioPin = -1;
  int presencePin = -1;
  bool presenceActiveHigh = true;
};

class Esp32Transport {
 public:
  Status begin(const Esp32TransportConfig& config);
  void end();
  bool isInitialized() const;
  SingleWireTransport descriptor();
  // deleted copy/move
};
```

`begin()` requires `GPIO_IS_VALID_OUTPUT_GPIO(sioPin)`, requires
`presencePin==-1` or an Arduino-ESP32/SoC-valid input GPIO, and rejects
`presencePin==sioPin` before configuring hardware. It leaves SI/O released
open-drain high. `end()` releases only this instance's SI/O and clears fixed
state. No heap allocation, logging, task, queue, or mutex.

`descriptor()` and stale-callback behavior must match Prompt 01 exactly:
empty before begin/after end; result-returning callbacks use
`IO_ERROR/NONE/BACKEND_NOT_INITIALIZED_DETAIL`, `nowUs` returns zero, and every
stale path performs zero GPIO/timer access.

Use supported/explicit GPIO or LL interfaces. Do not rely on a transitive header
for `GPIO_OUT_W1TS_REG`, `GPIO_OUT_W1TC_REG`, `GPIO_IN_REG`, or bank-1
equivalents. Guard S2/S3 differences explicitly and compile both.

## Timing representation

Do not reuse integer-microsecond `SingleWireTimingProfile`; it cannot represent
High-Speed requirements. Use private backend timing in nanoseconds or CPU
cycles, with absolute sample instants measured from the falling edge.

Initial physical-pin targets:

| Constant | Standard | High-Speed |
|---|---:|---:|
| bit frame | 64.0 us | 16.0 us |
| low 0 | 32.0 us | 10.0 us |
| low 1 | 6.0 us | 1.50 us |
| read drive-low `tRD` | 6.0 us | 1.20 us |
| read sample instant `tMRS` from falling edge | 7.0 us | 1.80 us |
| normal post-frame high | 650 us | 160 us |

Reset/Discovery:

```text
RESET_LOW_US                 = 600
RESET_RECOVERY_US            = 10
DISCOVERY_REQUEST_NS         = 1200
DISCOVERY_SAMPLE_FROM_FALL_NS= 4000
DISCOVERY_RESPONSE_MAX_US    = 24
DISCOVERY_RELEASE_CHECK_US   = 25
POST_DISCOVERY_HIGH_US       = 160
```

Prompt 08 electrical qualification requires measured `tPUP <= 0.40 us`. Stage
04 records this limit but makes no board-level validity claim.

### Electrical interface safety contract

Do not infer that every voltage accepted by an AT21CS part is safe on a direct
ESP32 GPIO. Releasing an open-drain output does not make an overvoltage or
under-VIH connection safe.

The one directly connected v2 qualification profile is deliberately narrow:

```text
ESP32 GPIO supply: board-qualified nominal 3.3 V
AT21CS VPUP:        the same nominal 3.3 V rail
external RPUP:      1.0 kOhm
measured CBUS:      <= 100 pF
measured tPUP:      <= 0.40 us at the ESP32 input threshold
ESP32 internal pull-up/pull-down: disabled
```

This profile is valid for AT21CS01 High-Speed, AT21CS01 Standard Speed, and
AT21CS11 High-Speed only after the actual rail tolerance is shown to remain
inside both devices' recommended operating/input ranges.

Use this exact matrix for other voltage corners:

| Part/mode | DS20005857I VPUP range | Direct ESP32 GPIO v2 profile | Requirement outside the direct profile |
|---|---:|---|---|
| AT21CS01 High-Speed | 1.7..3.6 V | nominal 3.3 V only | qualified bidirectional open-drain level shifter |
| AT21CS01 Standard | 2.7..3.6 V | nominal 3.3 V only | qualified bidirectional open-drain level shifter |
| AT21CS11 High-Speed | 2.7..4.5 V | nominal 3.3 V only | qualified bidirectional open-drain level shifter |

In particular, never connect AT21CS01 VPUP=1.7 V directly and claim a
guaranteed ESP32 logic high, and never apply 3.6 V or 4.5 V directly to an
ESP32 GPIO. A level shifter must never actively drive SI/O high; include both
side pull-ups, propagation delay, leakage, power-off behavior, and added
capacitance in `tPUP`, `tRD`, `tMRS`, `tRCV`, and HIL qualification. Testing an
out-of-profile voltage without the documented interface is a hardware-safety
failure, not a skipped test.

For a chip embedded in a removable load-cell or other remote peripheral, apply
the same electrical contract to the complete harness rather than only the
development-board trace. Every independent SI/O wire needs its own
controller-side external pull-up so the empty connector has a deterministic
high idle. Cable length/type, connector, protection/TVS/filtering, level
shifter, leakage, and accumulated capacitance are application hardware, not
library configuration; include all of them in measured `CBUS`/`tPUP` and
Prompt 08 qualification. Do not claim arbitrary cable length or reuse one
line's measurement for another line.

Write and speed:

```text
WRITE_HIGH_HOLD_US = 10000  // core Bus contract
SPEED_CHANGE_HOLD_US = 650
```

The read sample must be scheduled from the initial falling edge, not as
`sleep(readLow) + sleep(readSample)`.

## Reset/Discovery waveform

Implement exactly:

1. SI/O low for 600 us;
2. release;
3. wait at least 10 us;
4. drive one Discovery request low for 1.2 us;
5. release;
6. sample at 4.0 us from request falling edge;
7. never generate a second host-low pulse;
8. remain released through request-start +24 us;
9. at request-start +25 us, sample SI/O again and require it to be high;
10. if that release check is low, return `LINE_STUCK` with `present=false`;
11. after the successful release check, remain released/high for at least
    another 160 us before returning.

`present=true` only when the 4 us response sample was low and the 25 us release
check was high. This distinguishes a valid 8..24 us Discovery response from a
line that remains held low. A missing-pull-up/floating-line test must never be
accepted as presence; classify it as `LINE_STUCK` when low at the release check
or as absent when both samples are high. A physical timeout/line fault remains
typed transport failure.

## Whole-frame atomicity

One `transfer()` callback owns Start through Stop. There are no public
per-byte callbacks.

Record these versioned v2 reference-adapter bounds in the Stage 4 validation
record:

```text
ESP32_MAX_CONTIGUOUS_IRQ_MASK_US      = 2000
ESP32_MAX_SUCCESS_FRESH_PAGE_CALL_US  = 20000
ESP32_MAX_FAULT_FRESH_PAGE_CALL_US    = 22000
ESP32_MAX_RETAINED_HOLD_PAGE_CALL_US  = 32000
```

The page-call limits follow the fixed 9 ms transfer timeout, 10 ms write-high
hold, and the independent final-wait guard below. The 32 ms case permits
completing one retained prior hold before the new page transfer and hold. The
20 ms normal-success limit is the explicit reference-adapter responsiveness
objective. It is not derived from an I2C transport or any one consuming
firmware. Prompt 06 shows how an upper scheduler can avoid combining an old
retained hold with new traffic in one owner command; Bus correctness does not
depend on that optimization. A product may impose a stricter admission budget,
but that does not silently change these library objectives. Stage 04 must prove
the arithmetic and bounded software paths; Prompt 08 measures the physical
durations and is the only HIL acceptance gate.

The simplest candidate is a whole-frame critical section. Its software design
must make the expected masking interval finite and auditable; Prompt 08 must
measure and prove worst-case continuous interrupt masking within 2 ms before a
production-qualified claim.
The 2 ms ceiling is a deliberate v2 ESP32 coexistence policy, not a datasheet
timing value and not a consumer-firmware-specific rule.
Keep deliberate pre-Start, repeated-Start, Stop, and post-frame released-high
waits outside the interrupt-masked region where possible while retaining
exclusive frame ownership. Add host/static coverage for both maximum 8-byte
read and write shapes and defer physical measurements to Prompt 08:

- High-Speed whole-frame masking is expected to approach the 2 ms ceiling and
  must be measured in Prompt 08, not estimated as release evidence;
- Standard whole-frame masking will normally exceed 2 ms and is therefore not
  a production solution under this contract.

A per-byte critical section may reduce interrupt masking, but by itself it is
not protocol-safe. It is acceptable only with a scheduler/exclusive-owner
mechanism whose bounded software behavior can later be measured. Prompt 08
must prove that every falling-edge-to-falling-edge bit frame remains in the
active `tBIT` window. No implementation may pass final qualification merely
because an inter-byte high gap remains below `tHTSS`.

Exact waveform acceptance:

```text
Standard: every non-Start/Stop bit frame is 40..100 us.
High-Speed: every non-Start/Stop bit frame is
            >= measured(tLOW0 + tPUP + 2 us) and <= 25 us.
Intentional Start/Restart/Stop: released high for at least active tHTSS.
```

Prompt 08 measures inter-byte and interrupt-induced gaps under load. Both
conditions must hold there: no unintended `tHTSS` Start/Stop, and no `tBIT`
overrun. A gap below `tHTSS` can still violate `tBIT` and corrupt the
transaction.

Do not assume per-byte critical sections are sufficient. Do not rely on
undocumented function overhead for pulse width.

All timing-critical functions and data they touch must be safe under the chosen
cache/IRAM policy. No callback into application code occurs inside bit timing.

## CPU frequency and DFS

Choose and document one tested policy:

- calibrate CPU cycles immediately before each atomic frame while frequency is
  stable; or
- hold a supported power-management lock for backend lifetime/transfer.

No cached boot-time MHz assumption is allowed. Stage 04 compiles and tests the
chosen policy without hardware claims; Prompt 08 tests 80/160/240 MHz where the
target supports them and DFS enabled/disabled.

## Multiple backend instances

The reference backend must contain no mutable file-static/global device owner,
pin, timing, result, callback target, or protocol/Bus evidence. Two instances
on distinct SI/O pins must coexist and support correctly interleaved calls with
completely independent descriptors and diagnostics. If the platform requires a
process-wide arbitration primitive for an explicitly qualified parallel mode,
it contains no pin/Bus/device pointer or protocol evidence, and its
serialization and latency are documented and later measured in Prompt 08.

This v2 support claim does not promise simultaneous timing-critical frame
execution from separate tasks. The default upper-firmware contract serializes
all AT21CS backend calls through one owner even when physical wires differ. If
the implementation chooses to claim cross-instance parallel execution, it must
define the ESP32 critical-section/power-management arbitration, add contention
    tests, and qualify simultaneous S2/S3 execution in Prompt 08 without
    violating either wire's timing. Otherwise document parallel calls as
    unsupported.

`Esp32Transport::begin()` validates its own SI/O/presence pins only. A
multi-channel upper owner must reject duplicate SI/O pins and any presence pin
that collides with any enabled channel's SI/O pin before starting hardware; do
not add a mutable global pin registry to the library.

## Frame behavior

`transfer()` must:

1. validate descriptor invariants before Start;
2. enforce pre-Start high time;
3. send every byte MSb-first;
4. sample device ACK as the ninth frame;
5. implement repeated Start without Stop;
6. host-ACK every read byte except final host-NACK;
7. on NACK/error, attempt safe Stop when possible;
8. release SI/O on every exit;
9. report exact phase/index and `stopCompleted`;
10. after delivering all eight bits of a write payload byte, set
    `currentWriteByteMayBeAccepted=true` if a timeout, line fault, or I/O error
    prevents a definite ACK/NACK result; keep that byte out of
    `dataBytesTransferred`;
11. clear that flag for every definite ACK/NACK and every non-write result;
12. honor absolute deadline without a retry.

`waitUntilUs()`:

- runs outside timing critical sections;
- guarantees SI/O remains released;
- may use bounded Arduino-ESP32/PioArduino-supplied timing primitives, then
  finish against its monotonic microsecond source;
- must not return OK before deadline;
- accepts only intervals needed by this contract, at most 10 ms;
- has a second termination guard independent of `esp_timer_get_time`: after the
  coarse yield, a compile-time finite poll limit of 100000 iterations and a
  calibrated CPU-cycle ceiling of 2 ms terminate the final wait even if the
  protocol clock stops advancing;
- returns a typed timeout with a stable frozen-clock detail if either
  independent guard expires before the absolute deadline;
- contains no loop whose only termination condition is the protocol clock.

Acquire whatever power-management protection is required for the cycle ceiling
to remain conservative. Host fault tests must freeze `nowUs()` before and after
the coarse yield and prove a finite terminal return. Prompt 08 alone proves the
fresh and retained-hold page-call budgets at 80/160/240 MHz and with DFS.

## Presence callback

If configured:

- use input-only read;
- return typed I/O result plus boolean;
- do not alter SI/O or perform protocol traffic;
- validate presence pin differs from SI/O.

## Arduino and framework-neutral-core boundary

- Core headers/source contain no framework or platform headers.
- The explicit ESP32 transport may use guarded Arduino-ESP32 low-level SoC and
  FreeRTOS facilities supplied by PioArduino, only under
  `ARDUINO_ARCH_ESP32`/`framework = arduino`.
- Arduino examples use Arduino APIs only outside library code.
- Do not add a native-IDF example, component, CMake build, `ESP_PLATFORM`
  support claim, or `framework = espidf` environment.
- Prove framework independence through host/core builds and boundary scans, not
  by implementing a second framework adapter in this stage.

## Host/backend tests

Where hardware cannot be simulated, add compile/static tests for:

- both GPIO banks and S2/S3 guards;
- pin validation;
- descriptor callback completeness;
- descriptor empty before begin/after end; every stale result callback returns
  exact `IO_ERROR/NONE/-1`, stale `nowUs` returns zero, and GPIO/timer fakes
  record zero access;
- line release on all return paths;
- no user delay callback;
- no legacy GPIO fields in Driver;
- two backend instances retain distinct descriptors/pins and no mutable global
  state; interleaved begin/transfer/end on one instance cannot change the
  other's descriptor or line state.

## Stage-local v2 smoke consumers

Stage 1 has already removed the v1 API, while Stage 6 has not yet migrated the
shipped examples. Do not build or partially patch those examples in this
stage. Create only:

```text
test/consumer/phy_smoke/arduino/
  platformio.ini
  src/main.cpp
```

The consumer uses the exact v2 `Esp32Transport -> Bus -> Driver` construction,
fixed buffers, and a non-destructive initialize/Manufacturer-ID/read path. It
contains no private source copy and resolves the library through normal
consumer semantics. Stage 7 reuses it for package verification. Stage 6 alone
owns migration of shipped examples.

## Required build commands

```text
python -m platformio run -d test/consumer/phy_smoke/arduino -e phy_smoke_s2
python -m platformio run -d test/consumer/phy_smoke/arduino -e phy_smoke_s3
```

Both environments must resolve the exact PioArduino pin and
`framework = arduino`. Report an unavailable environment rather than claiming
it passed. No native-IDF package may be provisioned as part of these commands.

Core regression:

```text
python tools/check_core_timing_guard.py
python -m platformio test -e native
git diff --check
```

## Deferred physical qualification

Do not run physical HIL in Stage 04 and do not block its software checkpoint on
missing boards, parts, pins, pull-ups, instruments, captures, or measurements.
Mark physical timing/electrical acceptance `HIL_ONLY` and map it to Prompt 08,
which exclusively owns waveform capture, `tPUP`, CPU/DFS/load, interrupt-mask,
page-call-duration, and hardware fault qualification. Structure-only records
are not hardware evidence.

## Exit criteria

- Exact PioArduino 55.03.311 Arduino consumers compile on S2/S3.
- Host tests prove one Discovery request, exact modeled sample/release instants,
  held-low typing, MSb order, ACK/NACK phases, read termination, transactional
  outputs, checked deadlines, line release, and independent instances.
- Static/compile checks cover both GPIO banks and S2/S3 guards and verify every
  emitted timing-critical helper uses the intended IRAM/cache policy.
- Core boundary checks prove Bus/Driver remain framework-independent.
- No native-IDF fixture, component metadata, IDF-only root build, package/CI
  gate, or support claim remains in the Stage 04 build surface.
- Every physical-only criterion is recorded for Prompt 08 and does not block
  the Stage 04 software checkpoint.
- Platform support metadata can be stated precisely in Prompt 07.
