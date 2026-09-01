# Follow-up: Standard-Speed reservation defects and one documentation gap

**Task for an AI coder.** Independently verify each finding below, then fix the
two confirmed defects and close the documentation gap. Do not re-litigate the
work already accepted in [`CODE_AUDIT_RESOLUTION_2026-08-31.md`](CODE_AUDIT_RESOLUTION_2026-08-31.md)
— it was re-checked and is correct except where this document says otherwise.

Baseline for this review: `main` at `b154e78` ("fix: complete audit
follow-up"), clean and level with `origin/main`; native suite 131/131 green.

## 1. What is already correct — do not redo

Verified against the code and the hash-pinned DS20005857I datasheet:

| Item | State |
|---|---|
| F1 datasheet timing rows (`tRD` max `8 - tPUP` / `2 - tPUP`; `tMRS` = `tRD + tPUP` .. `8` / `2`) | Correct |
| F2 30 us discovery release check | Correct |
| F3 checkout-independent IRAM object glob | Correct |
| F4 package-link normalization + `..`-only escape test | Correct |
| P2 continuous Standard-Speed protocol segments, repeated-Start boundary retained | Correct, and documented in README |
| P3 speed-aware deadlines (9 ms HS / 24 ms SS, write preflight included, presence left at 9 ms) | Correct |
| P4 rejecting the original proposal | **Correct, and better than the proposal.** Recording a bus-silent no-op as a success would clear real failure counters. Keep as-is. |
| P1 exclusivity in `_claimAddress()` (both claim orders) and in `_setSpeedModeRaw()` | Correct as far as it goes — see section 2 |

## 2. Defect A — same-address rebind leaks a stale Standard reservation

**Confirmed by execution, not inspection.**

`Driver::bind()` ([src/AT21CS.cpp:81-96](../src/AT21CS.cpp#L81)) handles the
same-Bus/same-address rebind path as:

```cpp
if (keepsExistingClaim && config.startupSpeed == SpeedMode::STANDARD_SPEED) {
  ... bus._reserveStandardSpeed(...)          // sets the reservation
} else if (!keepsExistingClaim) {
  ... bus._claimAddress(...)
}
```

When `keepsExistingClaim` is true and the new config is `HIGH_SPEED`, **neither
branch runs**, so a reservation taken by the previous Standard configuration is
never cleared. The Bus then permanently refuses every other address.

Reproduction (bind addr0 Standard, rebind addr0 High-Speed, bind addr1):

```text
1) bind addr0 STANDARD     : OK             stdMask=0x01 claims=0x01
2) rebind addr0 HIGH_SPEED : OK             stdMask=0x01 claims=0x01   <-- stale
3) bind addr1 HIGH_SPEED   : INVALID_CONFIG                            <-- wrong
```

Effect: a legal multi-device High-Speed configuration is rejected until the
first Driver calls `end()`. It fails closed, so this is a false rejection
rather than an unsafe admission — but it is silent and hard to diagnose.

## 3. Defect B — a Bus reset drops the reservation and admits an illegal Driver

**Confirmed by execution.**

`Bus::_resetAndDiscover()` clears the reservation on success
([src/Bus.cpp:461](../src/Bus.cpp#L461)) because a reset returns every device
to High Speed. But the reservation also encodes *which Driver is entitled to
Standard Speed*, and that entitlement is unchanged by a reset. Between the
reset and the owning Driver's next operation, a second Driver can bind:

```text
1) driver A bound STANDARD addr0 : stdMask=0x01
2) Bus reset/discovery           : OK present=1  stdMask=0x00   <-- dropped
3) driver B bind HIGH addr1      : OK                           <-- should be rejected
```

Reachable in the field: `Driver::initialize()` resets first and can then fail
(absent chip, NACK, part mismatch), leaving A bound and Standard-configured
with the reservation gone.

Exclusivity itself still holds — A's later Standard restore fails because it is
no longer the sole claimant — so no unsafe mixed-speed traffic occurs. The
damage is that **A is permanently unusable** (every operation fails
`UNSUPPORTED_COMMAND`) until B releases, with no diagnostic pointing at the
cause.

### Root cause common to A and B

`_standardSpeedAddressMask` currently conflates two different facts:

1. **entitlement** — which claimed Driver is allowed to be the exclusive
   Standard-Speed owner (a configuration invariant), and
2. **device state** — whether the chip is presently in Standard Speed
   (already tracked per-Driver by `_activeSpeed` and the Bus generation).

A reset changes (2) and so clears the mask, dropping (1). A rebind changes (1)
and does not touch the mask at all. Fix the conflation rather than patching
each symptom.

### Required fix

Make the mask mean **entitlement only**:

1. **`Bus::_resetAndDiscover()`** — delete `_standardSpeedAddressMask = 0;` at
   [src/Bus.cpp:461](../src/Bus.cpp#L461). A reset must not alter entitlement.
2. **`Driver::_synchronizeBusState()`** — in the new-generation branch (after
   `_activeSpeed = SpeedMode::HIGH_SPEED; _speedKnown = true;` near
   [src/AT21CS.cpp:1067](../src/AT21CS.cpp#L1067)) and at the matching point in
   `_runInitializationSequence()` ([src/AT21CS.cpp:1436](../src/AT21CS.cpp#L1436)),
   release the reservation **only when this Driver is not Standard-configured**:

   ```cpp
   if (_config.startupSpeed != SpeedMode::STANDARD_SPEED) {
     _bus->_releaseStandardSpeed(_config.addressBits);
   }
   ```

   This is the release point for a reservation held conservatively after an
   ambiguous failed transition: the reset provably returned the device to High
   Speed, and the owning Driver — the only object that knows its own
   configuration — observes that reset here.
3. **`Driver::bind()`** — give the `keepsExistingClaim` path both directions:

   ```cpp
   if (keepsExistingClaim) {
     if (config.startupSpeed == SpeedMode::STANDARD_SPEED) {
       const Status reserveStatus = bus._reserveStandardSpeed(config.addressBits);
       if (!reserveStatus.ok()) {
         return Status::Error(Err::INVALID_CONFIG,
                              static_cast<int32_t>(config.addressBits));
       }
     } else {
       bus._releaseStandardSpeed(config.addressBits);
     }
   } else {
     ... existing _claimAddress() path ...
   }
   ```

   Releasing here is safe even if the device is physically still in Standard
   Speed: `bind()` calls `_resetLocalState()`, which clears `_speedKnown`, so
   the Driver must complete a Reset/Discovery — which returns the device to
   High Speed — before any I/O.

Keep the existing release-on-High-Speed-success and conservative-retention
logic in `_setSpeedModeRaw()` unchanged; it is correct.

### Required regression tests

Add to `test/test_lifecycle.cpp` (the existing reservation test around
lines 630-680 is the right neighbour) and register them in `test/test_main.cpp`:

1. Bind addr0 Standard, rebind addr0 High-Speed, assert
   `standardSpeedAddressMask == 0x00`, assert a second Driver binds addr1
   successfully.
2. Bind addr0 Standard, successful `TestAccess::resetAndDiscover()`, assert
   the mask is still `0x01`, assert a second Driver's bind is rejected with
   `INVALID_CONFIG`.
3. Reserve conservatively via an ambiguously failed `setSpeedMode(STANDARD)`
   on a High-Speed-configured Driver, reset, drive one operation through
   `_synchronizeBusState()`, assert the mask is released and a second Driver
   can then bind.

Each test must fail before the corresponding fix and pass after it.

## 4. Documentation gap — the Backend's tPUP envelope is unpublished

Not a code defect; a missing integration constraint that matters for a library
meant to drop into other firmware on boards this project has not seen.

The Backend uses fixed read sample points, so the SI/O rise time `tPUP` must
fit between releasing the line and sampling it:

| Mode | host low (`tRD`) | sample point | available for rise |
|---|---:|---:|---:|
| High Speed | 1.2 us | 1.8 us | **0.60 us** |
| Standard | 6.0 us | 7.0 us | **1.00 us** |

With `tPUP ~ 1.04 * R_PUP * C_BUS` (0.5 V to 0.7*V_PUP at V_PUP = 3.3 V), the
High-Speed budget implies:

| C_BUS | max R_PUP for High Speed |
|---:|---:|
| 100 pF | ~5.8 kOhm |
| 330 pF | ~1.7 kOhm |
| 1000 pF | ~0.58 kOhm |

The datasheet permits C_BUS up to 1000 pF and R_PUP up to 1.8-5.4 kOhm, so a
**datasheet-legal board can still fall outside this Backend's envelope** — for
example 1 kOhm with 500 pF of cable and multi-device loading. The failure mode
is a device `1` bit sampled before the line has risen: intermittent read
corruption and CRC/part-mismatch errors, with nothing pointing at pull-up
sizing.

Note the datasheet's own AC characterization conditions (100 pF, 1 kOhm, 2.7 V)
give `tPUP ~ 0.1 us` and sit comfortably inside the envelope. This is an
edge-of-range constraint, not a defect in the shipped timing.

**Required:** document the envelope where an integrator will find it — a short
subsection in `README.md` near the existing Standard-Speed interrupt-latency
paragraph, stating the 0.60 us / 1.00 us rise budgets, the
`R_PUP * C_BUS` rule of thumb with the table above, and that boards near the
datasheet's C_BUS or R_PUP maxima need a stronger pull-up than the datasheet
alone would suggest. Cross-reference it from `docs/HARDWARE_VALIDATION.md`.
Do not change the timing constants.

## 5. Definition of done

- The two reproductions in sections 2 and 3 produce the corrected result.
- The three regression tests in section 3 are added, and each fails before its
  fix.
- `native` and `native_sanitize` both green (expect 134/134).
- `python tools/check_docs.py`, `check_cli_contract.py`,
  `check_core_timing_guard.py`, `check_no_production_placeholders.py`,
  `check_package.py --inspect`, `scripts/generate_version.py --check` all pass.
- README and CHANGELOG updated under `[Unreleased]`; `library.json` stays at
  2.0.0.
- `docs/CODE_AUDIT_RESOLUTION_2026-08-31.md` gains a short addendum recording
  that its P1 reservation design needed these two corrections. Do not rewrite
  its history.
