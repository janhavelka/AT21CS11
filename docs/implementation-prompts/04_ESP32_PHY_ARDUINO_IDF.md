# Prompt 04 — ESP32-S2/S3 PHY for Arduino and native ESP-IDF

## Outcome

Implement and qualify one explicit `Esp32Transport` for ESP32-S2/S3 that
satisfies the frozen whole-frame transport contract under Arduino-ESP32 and
native ESP-IDF.

This is a physical-layer stage. Do not change Driver feature semantics to hide a
PHY failure.

## Required working method

Read all shared contracts, completed Stages 1–3, DS20005857I timing tables, and
the selected ESP-IDF GPIO/low-level API documentation. Preserve unrelated
changes.

Spawn subagents for:

1. S2 GPIO/timing implementation review;
2. S3 GPIO/timing implementation review;
3. Arduino/IDF build-boundary review;
4. logic-analyzer test-plan review.

Keep one integrator for shared backend code. Require a final cross-target review.
Refactor one backend at the root, reuse one frame/timing implementation for
Arduino and native IDF, delete the old byte callbacks, and do not band-aid
target differences with duplicate PHYs. Simplify every timing path that cannot
be measured. Do not use irreversible chip commands. Do not modify the protected
report. Do not commit or publish.

## Sole owned findings

Close:

- P-06;
- P-07;
- P-15;
- P-17;
- P-20;
- Q-01.

This stage supplies physical downstream proof for P-02 and P-05 without
reopening their transport/core contracts.

## Support matrix

Target:

- ESP32-S2 and ESP32-S3;
- Arduino-ESP32 3.2.0 through the exact repository pin
  `https://github.com/pioarduino/platform-espressif32/releases/download/54.03.20/platform-espressif32.zip`;
- native ESP-IDF at the two exact release endpoints 5.4.1 and 6.0.1.

If this matrix cannot be implemented and tested, narrow `library.json`,
`idf_component.yml`, README, and CI honestly in Prompt 07. Do not retain a broad
untested claim.

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
`presencePin==-1` or an IDF-valid input GPIO, and rejects
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

Electrical qualification requires measured `tPUP <= 0.40 us`. Do not claim
these sample targets valid for a board that exceeds it.

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
but that does not silently change these library measurements. A placeholder,
unmeasured budget, or unexplained relaxation fails this stage.

The simplest candidate is a whole-frame critical section, but it is acceptable
only when its measured worst-case continuous interrupt masking is within 2 ms.
The 2 ms ceiling is a deliberate v2 ESP32 coexistence policy, not a datasheet
timing value and not a consumer-firmware-specific rule.
Keep deliberate pre-Start, repeated-Start, Stop, and post-frame released-high
waits outside the interrupt-masked region where possible while retaining
exclusive frame ownership. Measure both maximum 8-byte read and write shapes:

- High-Speed whole-frame masking is expected to approach the 2 ms ceiling and
  must be measured, not estimated;
- Standard whole-frame masking will normally exceed 2 ms and is therefore not
  a production solution under this contract.

A per-byte critical section may reduce interrupt masking, but by itself it is
not protocol-safe. It is acceptable only with a scheduler/exclusive-owner
mechanism and measured worst-case interrupt latency proving that every
falling-edge-to-falling-edge bit frame remains in the active `tBIT` window.
Otherwise use a hardware-assisted implementation. No implementation may pass
merely because an inter-byte high gap remains below `tHTSS`.

Exact waveform acceptance:

```text
Standard: every non-Start/Stop bit frame is 40..100 us.
High-Speed: every non-Start/Stop bit frame is
            >= measured(tLOW0 + tPUP + 2 us) and <= 25 us.
Intentional Start/Restart/Stop: released high for at least active tHTSS.
```

Measure inter-byte and interrupt-induced gaps under load. Both conditions must
hold: no unintended `tHTSS` Start/Stop, and no `tBIT` overrun. A gap below
`tHTSS` can still violate `tBIT` and corrupt the transaction.

Do not assume per-byte critical sections are sufficient. Do not rely on
undocumented function overhead for pulse width.

All timing-critical functions and data they touch must be safe under the chosen
cache/IRAM policy. No callback into application code occurs inside bit timing.

## CPU frequency and DFS

Choose and document one tested policy:

- calibrate CPU cycles immediately before each atomic frame while frequency is
  stable; or
- hold a supported power-management lock for backend lifetime/transfer.

No cached boot-time MHz assumption is allowed. Test 80/160/240 MHz where the
target supports them and DFS enabled/disabled.

## Multiple backend instances

The reference backend must contain no mutable file-static/global device owner,
pin, timing, result, callback target, or protocol/Bus evidence. Two instances
on distinct SI/O pins must coexist and support correctly interleaved calls with
completely independent descriptors and diagnostics. If the platform requires a
process-wide arbitration primitive for an explicitly qualified parallel mode,
it contains no pin/Bus/device pointer or protocol evidence, and its
serialization and latency are documented and measured.

This v2 support claim does not promise simultaneous timing-critical frame
execution from separate tasks. The default upper-firmware contract serializes
all AT21CS backend calls through one owner even when physical wires differ. If
the implementation chooses to claim cross-instance parallel execution, it must
define the ESP32 critical-section/power-management arbitration, add contention
tests, and qualify simultaneous S2/S3 HIL without violating either wire's
timing. Otherwise document parallel calls as unsupported.

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
- may cooperatively yield with `vTaskDelay`/native primitives, then finish
  against `esp_timer_get_time`;
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
to remain conservative. Native fault tests must freeze `nowUs()` before and
after the coarse yield and prove a finite terminal return. HIL must prove the
fresh and retained-hold page-call budgets above at 80/160/240 MHz and with DFS.

## Presence callback

If configured:

- use input-only read;
- return typed I/O result plus boolean;
- do not alter SI/O or perform protocol traffic;
- validate presence pin differs from SI/O.

## Native IDF and Arduino boundary

- Core headers/source contain no framework headers.
- The explicit platform header/source may use guarded ESP-IDF/FreeRTOS APIs.
- Arduino examples use Arduino APIs only outside library code.
- Native IDF example uses `app_main`, native headers, `esp_timer`, `vTaskDelay`,
  and fixed C buffers.
- No Arduino facade is introduced into IDF.

Build backend-enabled and transport-only/backend-disabled component forms. In
backend-disabled form, Bus/Driver compile without GPIO/FreeRTOS dependencies.

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
test/consumer/phy_smoke/idf/
  CMakeLists.txt
  sdkconfig.defaults
  main/CMakeLists.txt
  main/main.cpp
```

Both consumers use the exact v2 `Esp32Transport -> Bus -> Driver` construction,
fixed buffers, and a non-destructive initialize/Manufacturer-ID/read path.
They contain no private source copy and resolve the library through normal
consumer semantics.

The IDF fixture exposes one CMake option with this exact name:

```text
AT21CS_ENABLE_ESP32_BACKEND
```

With `ON`, compile `Esp32Transport.cpp` and declare the exact GPIO, timer,
FreeRTOS, and low-level CPU/power-management requirements used by that source.
With `OFF`, compile and link only `Bus.cpp` and `AT21CS.cpp`; no Arduino,
ESP-IDF GPIO, timer, FreeRTOS, or ESP32 dependency may leak into core. Stage 7
reuses these fixtures for package verification. Stage 6 alone owns migration
of shipped examples.

## Required build commands

Arduino:

```text
python -m platformio run -d test/consumer/phy_smoke/arduino -e phy_smoke_s2
python -m platformio run -d test/consumer/phy_smoke/arduino -e phy_smoke_s3
```

Native IDF, in a separately activated 5.4.1 environment and again in a
separately activated 6.0.1 environment, using new build directories for every
target/mode:

```text
idf.py -C test/consumer/phy_smoke/idf -B build-s2-on  -DAT21CS_ENABLE_ESP32_BACKEND=ON  set-target esp32s2 build
idf.py -C test/consumer/phy_smoke/idf -B build-s3-on  -DAT21CS_ENABLE_ESP32_BACKEND=ON  set-target esp32s3 build
idf.py -C test/consumer/phy_smoke/idf -B build-s2-off -DAT21CS_ENABLE_ESP32_BACKEND=OFF set-target esp32s2 build
idf.py -C test/consumer/phy_smoke/idf -B build-s3-off -DAT21CS_ENABLE_ESP32_BACKEND=OFF set-target esp32s3 build
```

The exact option spelling and ON/OFF source/dependency behavior are acceptance
criteria; do not emulate OFF with a macro while still compiling the platform
source. Report an unavailable endpoint rather than claiming it passed.

Core regression:

```text
python tools/check_core_timing_guard.py
python -m platformio test -e native
git diff --check
```

## Non-destructive HIL gate for this stage

Before any EEPROM write:

1. capture Reset/Discovery on S2 and S3;
2. capture High-Speed 0, 1, read, ACK, Start, repeated Start, Stop;
3. capture Standard equivalents on AT21CS01;
4. measure `tPUP`;
5. repeat under CPU/DFS and interrupt/Wi-Fi/flash load;
6. record min/max, not one screenshot.

Sample instants are not visible on SI/O alone. Use a HIL-only, private
instrumentation build that brackets each Discovery/data/ACK GPIO-read
instruction with direct IRAM-safe writes to a separate marker pin. Record the
marker-to-read error bound and include it in the timing margin. The marker must
not be added to the public transport API or production build. Capture an
uninstrumented production waveform as well and prove the marker build did not
change bit-frame timing outside that bounded error.

Measure `tPUP` with an analog oscilloscope at the actual ESP32 input thresholds;
a digital logic analyzer trace alone is insufficient. Record oscilloscope and
logic-analyzer bandwidth/sample rate, threshold, probe loading, marker pin, raw
captures, and measurement uncertainty. The total uncertainty must be smaller
than the claimed HS `tRD`/`tMRS` margin.

If a limit fails, fix PHY timing. Do not compensate in Driver protocol.

## Exit criteria

- Arduino and declared IDF consumers compile on S2/S3.
- Logic traces satisfy all non-destructive timing limits.
- Discovery has one request pulse.
- Discovery is low at the 4 us presence sample and high at the 25 us release
  check; held-low returns `LINE_STUCK` and missing-pull-up never returns present.
- Read sampling is inside the absolute `tMRS` window with measured margin.
- Every ordinary bit frame satisfies `tBIT`, including under worst-case
  scheduler/interrupt load.
- No frame has an unintended inter-byte Stop.
- Continuous interrupt masking and page-call duration remain within the exact
  budgets above.
- Every tested VPUP uses the direct 3.3 V profile or a documented, qualified
  open-drain level shifter.
- Platform support metadata can be stated precisely in Prompt 07.
