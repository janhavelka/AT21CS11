# AT21CS01/AT21CS11 library review — 2026-08-26

Scope: full review of the core library (`include/AT21CS/`, `src/`), the ESP32
Backend, examples, test support and tooling, verified line-by-line against the
authoritative datasheet **DS20005857I** (the hash-pinned PDF in `docs/`).

This file is a development record. It is not exported in the package and is not
scanned by `tools/check_docs.py`.

## Verdict

The library is in very good shape. Every opcode, command frame, ID value,
memory map constant, CRC algorithm and AC timing constant in the core and the
ESP32 Backend was checked against DS20005857I and found correct, including the
subtle cases (Check Lock via `2h/W` + `0x6X` with NACK-on-memory-address
meaning locked; Freeze observation via the `1h/W` device-address ACK/NACK with
a liveness cross-check; conservative `MAY_HAVE_COMMITTED` evidence; the
Bus-wide post-write hold gating all traffic). No functional bug was found in
the Bus or Driver state machines.

Findings F1–F4 were simple and unambiguous and are already fixed in the tree.
P1–P4 involve a policy or margin choice, so they are written up with a concrete
proposed patch and left for a decision.

All of P1–P4 are confined to **Standard Speed**, which only the AT21CS01
supports and which no shipped example or validated setup uses (the qualified
hardware run was an AT21CS11, High-Speed only). Nothing here affects an
AT21CS11 or a High-Speed AT21CS01 deployment.

---

## Fixed during this review

### F1. Stale rev-D protocol claims in the chip reference (docs)

`docs/AT21CS01_AT21CS11_complete_driver_report.md` was written against
datasheet revision **D** (2020). Three claims were wrong or obsolete relative
to the pinned revision **I**:

- **Check Lock** was described as opcode `2h` with `R/W=1`. Rev I §7.5.2
  defines it as the Lock sequence (`2h/W` + memory address `0110_XXXXb`)
  truncated after the memory address byte; the memory-address ACK/NACK carries
  the lock state. The code already implemented the rev-I form
  ([AT21CS.cpp:1221](../src/AT21CS.cpp#L1221)); only the doc was wrong.
- A **`1h/R` "check frozen" query** was described. Rev I documents no such
  read form; frozen state is observed from the `1h/W` device-address ACK/NACK.
  Again the code was already correct ([AT21CS.cpp:1268](../src/AT21CS.cpp#L1268)).
- The **serial-number CRC** was described only by polynomial. Datasheet rev H
  explicitly replaced that "inadequate CRC formula" with **CRC-8/Maxim**
  (poly 0x31, reflected 0x8C, init 0). `Driver::crc8Maxim()` implements
  CRC-8/Maxim correctly.

Also removed: §14 "Recommended driver architecture notes" (recommended
`waitReady()` ACK-polling and a nonblocking wrapper, both contradicting the
shipped no-ACK-poll synchronous design and even listed as forbidden tokens in
`check_docs.py`) and §15, an unchecked pre-implementation to-do list. The
report is retitled as a chip protocol reference for DS20005857I.
`PROTECTED_SHA256` in `tools/check_docs.py` was updated; the docs gate passes.

The `CHANGELOG.md` `[2.0.0-rc.1]` stage entry (with its release-candidate
"Qualification" disclaimers) was folded into `[2.0.0]` so the released entry
is self-contained and the RC process metadata no longer ships.

### F2. Discovery release check had <1 µs margin over tDACK max

[Esp32Transport.cpp](../src/platform/esp32/Esp32Transport.cpp): the discovery
sequence verified that the device released SI/O **25 µs** after the discovery
fall. The device may legally drive low until **tDACK max = 24 µs**, and the
released line then needs tPUP to rise, leaving under 1 µs of margin. On an
in-spec but slower-rising bus (higher C_BUS / weaker pull-up) a present,
healthy device could be misreported as `LINE_STUCK`. The check now runs at
**30 µs** (still well before the 160 µs post-discovery high, so no protocol
impact); the cycle-exact assertion in
[test_esp32_transport.cpp:330](../test/test_esp32_transport.cpp#L330) was
updated (6000 → 7200 cycles). All 130 native tests pass, sanitized and plain.

### F3. CI IRAM gate silently depended on the checkout directory name

[.github/workflows/ci.yml:51](../.github/workflows/ci.yml#L51) and
[:60](../.github/workflows/ci.yml#L60) located the compiled Backend object with
`-path '*/AT21CS11/platform/esp32/Esp32Transport.cpp.o'`. PlatformIO
materializes the `symlink://../../../..` dependency under a directory named
after **the folder the repository is cloned into**, not the `library.json` name
(which is `AT21CS01_AT21CS11` and would never match this glob). The gate
therefore worked only while the checkout was named exactly `AT21CS11`; in a
fork, a rename, or any clone into another folder name, `find` returned nothing
and `test -n "$object"` failed the job with no explanation — a false CI failure
on a gate that is supposed to verify timing code stays in IRAM.

Both globs are now `-path '*/platform/esp32/Esp32Transport.cpp.o'`, which is
unique within that build tree (the phy_smoke consumer compiles only its own
`src/main.cpp` besides the library) and independent of directory naming.

### F4. Package link checker rejected valid parent-relative links

[tools/check_package.py:285](../tools/check_package.py#L285) resolved a
markdown link with `PurePosixPath`, which does **not** collapse `..`. A valid
link such as `../README.md` from `docs/MIGRATION.md` became
`docs/../README.md`, retained a `..` component, and was reported as a "broken
package link" even though `README.md` is in the package. The sibling gate
`tools/check_docs.py` resolves the same link correctly with `.resolve()`, so
the two checkers disagreed.

No packaged document uses `../` today, so this was latent — but the first one
to do so would have failed `package-inspect` spuriously. The path is now
normalized with `posixpath.normpath()` before the membership test, and only a
path whose normalized form still escapes the package (starts with `..`) is
rejected. Verified against valid parent links, escapes, and non-members.

---

## Findings with proposals (not applied)

### P1. Standard-Speed mode on a shared (multi-device) Bus is unsafe — add a sole-claimant guard

**Severity: medium (only affects AT21CS01 + Standard Speed + multi-drop).**

DS20005857I specifies per-device speed state, but the bit timings of the two
modes overlap destructively on a shared wire:

- A Standard-Speed device samples an input bit between tLOW1 max (8 µs) and
  tLOW0 min (24 µs). High-Speed traffic for *another* device (tLOW0 = 10 µs,
  tBIT = 16 µs) therefore lands inside the Standard device's sampling window
  and is decoded as a pseudo-random bit stream — which can in principle form a
  phantom command addressed to it.
- Symmetrically, Standard-Speed traffic (tLOW1 = 6 µs) falls inside a
  High-Speed device's tLOW0 range (6–16 µs), so High-Speed devices decode
  Standard frames as '0'-heavy garbage.

Nothing in `Bus`/`Driver` prevents one claimed Driver from running
`STANDARD_SPEED` while other Drivers share the Bus at `HIGH_SPEED`
(`Driver::setSpeedMode()`, the lazy restore in `_synchronizeBusState()`, and
`startupSpeed` in `_runInitializationSequence()` are all per-Driver).

**Proposal (single choke point):** all device speed changes flow through
`Driver::_setSpeedModeRaw()`. Refuse to leave High-Speed unless this Driver is
the only claimant on the Bus:

```cpp
// AT21CS.cpp, top of Driver::_setSpeedModeRaw(), after the isKnownSpeed check:
if (mode == SpeedMode::STANDARD_SPEED) {
  const uint8_t ownBit = static_cast<uint8_t>(1u << _config.addressBits);
  if (_bus->snapshot().claimedAddressMask != ownBit) {
    return Status::Error(Err::UNSUPPORTED_COMMAND);
  }
}
```

Optionally mirror the constraint at claim time so the failure surfaces early:
in `Driver::bind()`, reject `config.startupSpeed == STANDARD_SPEED` when
another address is already claimed on the target Bus. (The reverse direction —
rejecting a *second* claim because an existing driver is Standard-configured —
is not knowable from the Bus, which tracks claims, not speeds; the
`_setSpeedModeRaw` guard is therefore the authoritative one and the `bind()`
check is best-effort UX.) Document in the README: *a Bus shared by more than
one device is High-Speed only*.

This keeps single-device Standard-Speed (the only qualified arrangement)
working and closes the unsafe combination with four lines.

### P2. Standard-Speed writes reopen the interrupt window at the worst moment

**Severity: low (Standard Speed only, requires a >100 µs ISR landing in an
inter-byte gap while the scheduler is suspended).**

In [Esp32Transport.cpp](../src/platform/esp32/Esp32Transport.cpp) `_transfer`,
`finishStandardByte()` exits the critical section after **every** byte in
Standard-Speed mode to bound interrupt-off time. Between *data bytes of a
write frame* that is exactly the window DS20005857I §4.1.3.3 warns about: an
idle period longer than tBIT max (100 µs) immediately after a data-byte ACK is
interpreted as a Stop condition and **starts an internal write cycle with the
partial page**. A long ISR in that gap therefore can commit a truncated write.
The library's evidence model already degrades safely (the next byte fails, the
Bus performs the 10 ms hold, the result reports `MAY_HAVE_COMMITTED`), so no
false success is possible — but the partial commit itself is physically real.

**Proposal:** keep the per-byte release for frames that carry no write payload
(reads and address-only frames, where an interruption merely aborts the frame
harmlessly), and hold the critical section for the whole frame when there *is*
a write payload. That is one condition, not a phase test:

```cpp
const auto finishStandardByte = [&]() AT21CS_ESP32_IRAM_ATTR {
  // A write frame must not be interrupted between bytes: an idle gap after a
  // data ACK reads as a Stop and commits the partial page (DS20005857I 4.1.3.3).
  if (!highSpeed && transfer.txLength == 0) {
    closeTiming();
  }
};
```

Cost: the masked span is the byte transfers themselves (the Start/Stop high
periods already run outside the critical section), so a Standard-Speed 8-byte
page write would mask interrupts for 10 bytes × 9 bit frames × 64 µs ≈ 5.8 ms.
High-Speed already masks its whole frame, but that is only ≈ 1.4 ms, so this is
a real latency increase rather than merely the same policy applied
consistently. If ~6 ms of interrupt latency is unacceptable
for the product, the alternative is to leave the code alone and document the
constraint: today's behavior is *safe* (a truncated write is always reported as
`MAY_HAVE_COMMITTED`, never as success), just not corruption-free under
pathological ISR latency.

### P3. The 9 ms transfer deadline leaves only 8% margin at Standard Speed

**Severity: medium (Standard Speed only), and it compounds P2.**

`Bus::TRANSFER_TIMEOUT_US` is a single 9000 µs budget applied to every frame
regardless of speed ([Bus.h:70](../include/AT21CS/Bus.h#L70), used in
`_execute()` and `_executeWrite()`). Computed from the shipped timing
constants (9 bit frames per byte; Standard tBIT = 64 µs, tHTSS = 650 µs):

| Frame | High-Speed | Standard Speed |
|---|---:|---:|
| 8-byte random read (11 bytes on the wire, two Start/Restart highs) | 2064 µs (77% slack) | **8286 µs (7.9% slack)** |
| 8-byte page write | 1760 µs (80% slack) | 7060 µs (22% slack) |

The maximum Standard-Speed read — which `readEeprom()` issues routinely, since
`_readRandomRangeRaw()` chunks at the 8-byte frame limit — completes just
714 µs inside its deadline. Worse, this is exactly the mode in which
`finishStandardByte()` releases the critical section between bytes (P2), so
ISRs may legitimately run during the frame and every microsecond they consume
comes out of that 714 µs. A modest amount of unrelated interrupt activity
therefore aborts a perfectly healthy read with `TRANSPORT_TIMEOUT`, which the
Driver then counts toward `offlineThreshold` and can drive to `OFFLINE`.

Note this is not hypothetical margin-shaving: the frame budget was clearly
sized for High-Speed, where it is a 4.4× overshoot, and the same number was
inherited by a mode whose bit time is 4× longer.

**Proposal:** make the deadline speed-aware, keeping fast fault detection at
High-Speed and honest headroom at Standard Speed:

```cpp
// Bus.h, replacing TRANSFER_TIMEOUT_US:
static constexpr uint32_t HIGH_SPEED_TRANSFER_TIMEOUT_US = 9000;
static constexpr uint32_t STANDARD_SPEED_TRANSFER_TIMEOUT_US = 24000;
```

with one helper used by `_execute()`, `_executeWrite()` and `_readPresence()`:

```cpp
static constexpr uint32_t _transferTimeoutUs(SpeedMode speed) {
  return speed == SpeedMode::STANDARD_SPEED
             ? STANDARD_SPEED_TRANSFER_TIMEOUT_US
             : HIGH_SPEED_TRANSFER_TIMEOUT_US;
}
```

24000 µs gives the Standard worst case ~2.9× headroom, in the same spirit as
High-Speed's 4.4×. Both remain fixed compile-time bounds, so the
"every wait has a deadline" rule is untouched. `_executeWrite()`'s
`REQUIRED_RANGE_US` overflow precheck and the scripted-transport deadline
oracles in `test/support/` expect the current constant and must be updated
with it.

### P4. `Driver::setSpeedMode()` same-mode early return skips health bookkeeping

**Severity: cosmetic.**

[AT21CS.cpp:812](../src/AT21CS.cpp#L812): when the requested mode already
matches `_activeSpeed`, the method updates `_config.startupSpeed` and returns
`Ok()` without `_finishOperation()`, so `lastStatus()`, `lastOkUs` and
`totalSuccess` are not updated for this successful call, unlike every other
successful public operation. Either call
`_finishOperation(Status::Ok(), OperationKind::NORMAL_IO, _state)` before
returning, or leave as-is and accept that a no-op does not count as an
operation. No functional consequence; flagged for consistency only.

---

## Verified-correct highlights (for future maintainers)

- **Opcodes and frames** (`CommandTable.h`): all eight opcodes, the Lock
  address `0x60`, the Freeze payload `0x55/0xAA`, ROM-zone register addresses
  `0x01/0x02/0x04/0x08` with value `0xFF`, and manufacturer IDs
  `0x00D200`/`0x00D380` with the 3-bit revision mask all match DS20005857I.
- **AC timings** (`Esp32Transport.cpp`): reset low 600 µs (covers tRESET
  Standard 480 µs and tDSCHG 150 µs), recovery 10 µs (tRRT ≥ 8), discovery
  request 1.2 µs (tDRR 1–2), sample at 4 µs (tMSDR 2–6, tDACK ≥ 8), and both
  speed profiles' tBIT/tLOW0/tLOW1/tRD/sample/tHTSS values sit inside their
  datasheet windows. (The per-frame *deadline* that wraps these bit timings is
  generous at High-Speed but thin at Standard Speed — see P3.)
- **Write-cycle policy**: the Bus-owned 10 ms released-high hold (tWR max
  5 ms, 2× margin) gates *all* subsequent traffic on that Bus, matching the
  datasheet's multi-drop prohibition on communicating during any device's tWR.
- **Evidence model**: `currentWriteByteMayBeAccepted` is set only when all
  8 bits of a data byte were delivered but the ACK was not sampled — exactly
  the case where a following Stop can commit; NACKed and mid-byte-aborted
  frames correctly produce `NOT_ATTEMPTED`.
- **Serial number**: read as one 8-byte frame from Security `0x00`, product ID
  `0xA0`, CRC-8/Maxim over bytes 0–6 — matches rev H/I.
- **Irreversible operations**: precheck → mutate → verify with
  `INDETERMINATE` for the ambiguous frozen-vs-absent NACK, plus a
  manufacturer-ID liveness confirmation before interpreting a Freeze NACK as
  "frozen". This resolves the datasheet's inherent ambiguity correctly.
- **Examples** (`examples/`): `BoundedCli` has no overflow or off-by-one (a
  128-byte buffer accepts 127 characters and always terminates in bounds; the
  discard path defers `TOO_LONG` to the line terminator; CRLF is handled;
  token pointers into the line buffer stay valid because polling stops while a
  command is pending). `parseUnsigned`'s overflow guard is exactly equivalent
  to the unchecked `parsed * base + digit <= maximum`. Range validation
  matches the real constants, including the page-boundary rule
  `(address % 8) + length > 8`. Lifecycle order and teardown follow the
  library contract, every `Status` is reported, `readPresenceIndicator()` is
  only reached behind `hasPresenceIndicator()`, and all cadence arithmetic is
  `millis()`-wraparound-safe.
- **Test support** (`test/support/`): every scripted protocol expectation —
  opcodes, the `opcode<<4 | addressBits<<1 | R/W` device-address layout, and
  the 9000/5000/10000/160/650 µs timing constants — mirrors the authoritative
  values in `Bus.h` and the datasheet.
- **Remaining tooling**: `check_cli_contract.py`, `check_core_timing_guard.py`,
  `check_esp32_iram_sections.py`, `check_no_production_placeholders.py`,
  `generate_version.py` (both entry points) and
  `configure_native_sanitizers.py` were reviewed and are correct;
  `check_docs.py`'s LF-normalized hashing is cross-platform sound.

## Gate status after this review

`native` and `native_sanitize` (130/130 each), `check_docs.py`,
`check_cli_contract.py`, `check_core_timing_guard.py`,
`check_no_production_placeholders.py`, `check_package.py --inspect` and
`generate_version.py --check` all pass locally.
