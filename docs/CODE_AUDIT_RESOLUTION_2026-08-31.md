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
DS20005857I datasheet. Relevant datasheet pages 10, 16, 20, 25, 29, and 34
were also rendered and inspected visually rather than relying only on
extracted text.

## Fresh independent re-audit

Before the fresh pass, `main` was fetched again, was clean, and matched
`origin/main` at `bad1091` with zero ahead/behind divergence. The original
review was then read again in full and the completed commit was audited from
the actual source and diff, without using the earlier resolution summary as
evidence. Parallel passes covered:

- protocol requirements, documentation, CI and package tooling (F1–F4);
- Bus/Driver lifecycle, exclusivity, error evidence, public API scope and P4;
- Backend timing, interrupt boundaries, deadline arithmetic and failure exits
  (P2–P3).

The F1–F4 corrections, P1 reservation design, P3 deadlines and P4 disposition
were independently confirmed. The re-audit found two gaps in the first
implementation: P2 had been corrected only for writes even though the
datasheet's maximum-idle rule applies to every continuing transaction, and the
private Standard-Speed reservation had been added to the public `BusSnapshot`
solely for tests. Both were simplified and corrected as described below.

## Finding dispositions

| Finding | Assessment | Resolution |
|---|---|---|
| F1 — stale rev-D protocol claims | Valid. The existing Check Lock, Freeze-observation, and CRC corrections match DS20005857I. Two residual timing-table errors remained: tRD max omitted tPUP, and tMRS used the wrong range. | Corrected tRD max to `8 − tPUP` / `2 − tPUP` and tMRS to `tRD + tPUP` through `8` / `2` microseconds. Updated the protected-document hash. |
| F2 — discovery release margin | The change from 25 µs to 30 µs is safe and improves robustness, but the report overstates the practical supported-bus defect. The Backend's fixed High-Speed read timing already requires tPUP no greater than 0.6 µs, leaving at least 1 µs after tDACK max at the old check. | Retained the already-applied 30 µs check; no additional code change was warranted. |
| F3 — checkout-name-dependent IRAM gate | Valid; the already-applied path-independent object glob is the simplest proper fix. | Retained and revalidated it with the ESP32-S2/S3 IRAM object checks. |
| F4 — parent-relative package links | Valid, but the existing `startswith("..")` escape test also rejected legal names such as `..valid.md`. | Tightened the test to reject only `..` and `../...`. Added a real packaged `../README.md` link so package inspection exercises normalized parent-relative resolution. |
| P1 — Standard Speed on a shared Bus | Valid. The proposed four-line check was incomplete because it could not reject the reverse order: a second claim after the sole device had already entered Standard Speed. | Added a Bus-owned Standard-Speed reservation coupled to address claims. Exclusivity is enforced transactionally in both bind orders, same-Bus rebinds, runtime changes, lazy restoration, ambiguous speed-change evidence, and successful Bus reset/discovery. Shared Buses are documented as High-Speed-only. Because software cannot discover unclaimed physical chips, Standard Speed additionally requires exactly one physical device on the wire. |
| P2 — interrupt window during Standard transactions | Valid, and broader than the report stated. DS20005857I section 4.1.3.3 requires a transaction interrupted for more than tBIT to be restarted after tHTSS. Reopening interrupts between Standard read or address bytes could therefore abort or reinterpret a command while the Backend continued, including a false-success read of released-high bits. | Removed the per-byte critical-section split. Each uninterrupted Standard protocol segment is now continuous; the deliberate repeated-Start high interval remains a safe segment boundary. Maximum page-write and direct-read segments are approximately 5.76 ms and 5.18 ms. Maximum Standard writes and direct/random reads plus accepted-prefix NACK and timeout exits verify balanced cleanup. |
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

- Added private Bus state for the Standard-Speed reservation, with test-only
  observation through the existing `TestAccess` friend. The fresh audit
  removed an unnecessary `BusSnapshot` field, keeping this internal invariant
  out of the public diagnostic surface.
- Made Driver bind and speed-mode transitions transactional around that
  reservation, preserving conservative exclusivity after a transition that
  may have reached the device.
- Made frame deadlines speed-aware without weakening any High-Speed or
  presence-indicator timeout.
- Kept every uninterrupted Standard-Speed protocol segment continuous across
  address, write and read bytes, while retaining safe repeated-Start and Stop
  boundaries.
- Added test-only critical-section counters and lifecycle/deadline regression
  coverage. The native suite now contains 131 tests.
- Corrected the two remaining datasheet timing claims, hardened package-link
  escape detection, and updated README, migration, coverage, and changelog
  documentation.

These changes remain under `[Unreleased]`; `library.json` stays at 2.0.0
because this work does not itself create a release.

## Verification

The final source and exported package were checked with:

- native tests: 131/131 passed;
- native tests with configured sanitizers: 131/131 passed;
- documentation, CLI contract, core timing, production-placeholder and
  generated-version guards;
- package inspection and a clean exported-package platform-neutral consumer
  build;
- clean exported-package Arduino builds of both shipped examples on ESP32-S2
  and ESP32-S3 (four builds total);
- direct ESP32-S2 and ESP32-S3 physical-transport smoke builds;
- IRAM section verification on both target objects: all 17 timing methods and
  all 3 emitted timing lambdas passed;
- Git whitespace validation.

No hardware-in-the-loop run was performed. The Standard-Speed changes are
covered by deterministic native protocol and Backend timing tests plus both
target-family compile and IRAM checks.

## 2026-09-01 Standard-Speed reservation addendum

A follow-up audit found that the P1 reservation lifecycle above was incomplete
in two cases: a same-address rebind from Standard to High Speed retained stale
ownership, while a successful Bus Reset discarded a still-configured Standard
owner's reservation. The mask now represents configuration entitlement only;
Reset changes observed device speed but not entitlement. A High-Speed-configured
Driver releases any conservative transition reservation when it observes the
new Reset generation. Three focused lifecycle regressions cover both rebind
directions, Reset retention, and release during Driver resynchronization. This
addendum corrects the P1 design record without rewriting the original audit
history.
