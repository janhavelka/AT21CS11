# Code audit resolution — 2026-08-31

## Scope and source

The repository was fetched and checked before editing. `main` was clean,
matched `origin/main`, and was the newest remote branch at commit `a980c77`
(`refactor: update CI checks and documentation for accuracy and clarity`).

The requested `docs/CODE_AUDIT.md` does not exist in the checked-out tree or
in any fetched Git revision. The only audit report is
[`CODE_REVIEW_2026-08-26.md`](CODE_REVIEW_2026-08-26.md), introduced by that
newest commit, so it was treated as the intended source. This resolution keeps
that report unchanged as a historical review record.

Every F1–F4 and P1–P4 item was compared with the current implementation,
tests, documentation, package tooling, and the hash-pinned Microchip
DS20005857I datasheet. Relevant datasheet pages 10, 16, 25, and 34 were also
rendered and inspected visually rather than relying only on extracted text.

## Finding dispositions

| Finding | Assessment | Resolution |
|---|---|---|
| F1 — stale rev-D protocol claims | Valid. The existing Check Lock, Freeze-observation, and CRC corrections match DS20005857I. Two residual timing-table errors remained: tRD max omitted tPUP, and tMRS used the wrong range. | Corrected tRD max to `8 − tPUP` / `2 − tPUP` and tMRS to `tRD + tPUP` through `8` / `2` microseconds. Updated the protected-document hash. |
| F2 — discovery release margin | The change from 25 µs to 30 µs is safe and improves robustness, but the report overstates the practical supported-bus defect. The Backend's fixed High-Speed read timing already requires tPUP no greater than 0.6 µs, leaving at least 1 µs after tDACK max at the old check. | Retained the already-applied 30 µs check; no additional code change was warranted. |
| F3 — checkout-name-dependent IRAM gate | Valid; the already-applied path-independent object glob is the simplest proper fix. | Retained and revalidated it with the ESP32-S2/S3 IRAM object checks. |
| F4 — parent-relative package links | Valid, but the existing `startswith("..")` escape test also rejected legal names such as `..valid.md`. | Tightened the test to reject only `..` and `../...`. Added a real packaged `../README.md` link so package inspection exercises normalized parent-relative resolution. |
| P1 — Standard Speed on a shared Bus | Valid. The proposed four-line check was incomplete because it could not reject the reverse order: a second claim after the sole device had already entered Standard Speed. | Added a Bus-owned Standard-Speed reservation coupled to address claims. Exclusivity is enforced transactionally in both bind orders, same-Bus rebinds, runtime changes, lazy restoration, ambiguous speed-change evidence, and successful Bus reset/discovery. Shared Buses are documented as High-Speed-only. Because software cannot discover unclaimed physical chips, Standard Speed additionally requires exactly one physical device on the wire. |
| P2 — interrupt window during Standard writes | Valid. DS20005857I section 4.1.3.3 says an idle interval after a write-data ACK becomes Stop and starts tWR, so an ISR in the old inter-byte opening could commit a partial page. | A Standard-Speed transfer with a write payload now keeps its timing critical section for the complete frame. Reads and address-only transfers still release it per byte. The maximum 8-byte page-write mask is approximately 5.76 ms; all exits, including NACK and timeout, are tested for balanced cleanup. |
| P3 — thin 9 ms Standard deadline | Valid. The maximum Standard random read is about 8.286 ms. The report's numeric total is correct, although it comprises three high intervals (pre-Start, repeated-Start, and Stop), not two. | Added speed-aware fixed bounds: 9 ms for High Speed and 24 ms for Standard Speed, including the write preflight calculation. The separate presence-indicator operation remains at 9 ms because it has no device SpeedMode and reads an independent GPIO. Exact boundary tests cover both Standard transfer and write-preflight deadlines. |
| P4 — same-mode speed call skips health accounting | The observation is true, but the proposed `_finishOperation()` call is not correct. A bus-silent no-op provides no device-health evidence; recording it would clear real failures, increment successes, update `lastOkUs`, and potentially force `READY` without contacting the device. | Kept the existing behavior, documented that validation failures and bus-silent no-ops do not update device-health accounting, and added a regression test. |

Consequently, the source report's broad verdict that no functional Bus or
Driver defect was found is superseded for Standard Speed: P1–P3 describe real
behavioral defects even though High-Speed operation is unaffected.

The report's verified opcode, frame, write-hold, mutation-evidence, serial-number,
irreversible-operation, example, and remaining tooling observations were also
checked. No additional implementation defect was found there. The AC-timing
highlight should be read with the Backend's explicit tPUP envelope noted above.

## Implementation summary

- Added explicit Bus state for the Standard-Speed reservation and exposed it
  in `BusSnapshot` for deterministic diagnostics and tests.
- Made Driver bind and speed-mode transitions transactional around that
  reservation, preserving conservative exclusivity after a transition that
  may have reached the device.
- Made frame deadlines speed-aware without weakening any High-Speed or
  presence-indicator timeout.
- Prevented Standard write-payload frames from being split by an interrupt
  window after an accepted byte.
- Added test-only critical-section counters and lifecycle/deadline regression
  coverage. The native suite now contains 131 tests.
- Corrected the two remaining datasheet timing claims, hardened package-link
  escape detection, and updated README, migration, coverage, and changelog
  documentation.

These changes remain under `[Unreleased]`; `library.json` stays at 2.0.0
because this work does not itself create a release.

## Verification

The completed change was checked with:

- native tests: 131/131 passed;
- native tests with configured sanitizers: 131/131 passed;
- core timing, CLI contract, production-placeholder, generated-version, and
  documentation guards;
- package inspection and platform-neutral package consumer build;
- a clean exported-package run that reported successful Arduino builds for
  both shipped examples on ESP32-S2 and ESP32-S3 before its repository-change
  guard noticed this report and two style-only edits made concurrently;
- a complete direct ESP32-S2 physical-transport smoke build, ESP32-S3 source
  compilation and link, and IRAM section verification of both target objects;
- Git whitespace validation.

An otherwise identical final Arduino rerun was not possible in this session.
Another pre-existing build had replaced the shared Arduino framework package
with the dependency set for an older installed PioArduino platform. The
mandated wrapper attempted to restore Arduino-ESP32 3.3.11, but Windows kept
`cores/esp32/Stream.cpp` open and blocked the package replacement. No global
package file was manually deleted and no alternate PlatformIO Core was
installed. This is an environment/cache lock, not a source compile failure.

No hardware-in-the-loop run was performed. The Standard-Speed changes are
covered by deterministic native protocol and Backend timing tests plus both
target-family compile and IRAM checks.
