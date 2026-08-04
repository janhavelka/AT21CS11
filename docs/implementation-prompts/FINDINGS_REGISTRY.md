# AT21CS v2 finding registry

This registry tracks outcomes; it is not a second behavioral contract. Public
headers, the shared contract, current prompt, tests, and actual evidence decide
whether the implementation is correct.

The current baseline is the completed Stage-05 checkpoint `fd1e4660`. Findings
owned by Stages 01-05 are recorded closed because their root fixes and host
verification are present at that checkpoint. Physical-only downstream evidence
is tracked separately and does not reopen a completed software fix.

Statuses:

- `CLOSED`: root software requirement implemented and verified;
- `OPEN`: work remains in its named stage;
- `HIL_PENDING`: software exists but the physical claim is not yet qualified;
- `OUT_OF_SCOPE`: product-specific behavior is explicitly not a library claim.

## Protocol and functional correctness

| ID | Finding/outcome | Owner | Status | Evidence or remaining gate |
|---|---|---:|---|---|
| P-01 | post-write `tWR` uses a Bus-wide released-high hold, never ACK polling | 1 | CLOSED | Bus/write fault tests; waveform HIL pending |
| P-02 | one whole-frame callback prevents an unintended inter-byte Stop | 1 | CLOSED | Bus/backend frame tests; waveform HIL pending |
| P-03 | ordinary operations perform no hidden Reset/Discovery | 2 | CLOSED | lifecycle/event oracle |
| P-04 | unsafe current-address API removed; reads are address-explicit | 2 | CLOSED | public symbol/read tests |
| P-05 | speed transition uses current timing and commits state only after evidence | 2 | CLOSED | speed/frame tests |
| P-06 | Discovery has one documented request pulse | 4 | CLOSED | backend tests; waveform HIL pending |
| P-07 | read sampling follows absolute datasheet timing | 4 | CLOSED | backend tests; waveform HIL pending |
| P-08 | Security ranges validate full `size_t` values before I/O | 3 | CLOSED | boundary tests including `SIZE_MAX` |
| P-09 | AT21CS11 Standard request fails before I/O/health change | 2 | CLOSED | zero-I/O speed tests |
| P-10 | typed transport distinguishes NACK, timeout, line-stuck, stalled clock, and I/O error | 1 | CLOSED | fault matrix |
| P-11 | initialization preserves exact absence/transport failure | 2 | CLOSED | lifecycle/fault tests |
| P-12 | Check Lock uses documented `2h/W + 0x6X` framing | 3 | CLOSED | trace tests |
| P-13 | invented `1h/R` Freeze query removed | 3 | CLOSED | symbol/trace tests |
| P-14 | CRC expectations use independent CRC-8/Maxim vectors | 5 | CLOSED | host vectors |
| P-15 | ESP32 Backend owns bounded timing and DFS policy | 4 | CLOSED | backend/static tests; physical margins HIL pending |
| P-16 | destructive speed-query behavior removed | 2 | CLOSED | API/event tests |
| P-17 | Reset/Discovery/read/post-frame timing software matches the datasheet contract | 4 | CLOSED | backend tests; waveform HIL pending |
| P-18 | stale non-protected technical reference text | 7 | CLOSED | obsolete references removed; docs/hash checker passes; protected report untouched |
| P-19 | Manufacturer-ID part classification masks only revision bits | 2 | CLOSED | all-revision tests |
| P-20 | Discovery verifies both response and release-high | 4 | CLOSED | backend/fault tests; held-low HIL pending |
| P-21 | fixed 10 ms write policy must not be advertised beyond qualified part/temperature conditions | 8 | HIL_PENDING | scoped HIL and honest documentation |
| P-22 | generic remote harness behavior is not a library guarantee | 8 | OUT_OF_SCOPE | only explicitly recorded electrical setups may be qualified |

## Architecture, lifecycle, and diagnostics

| ID | Finding/outcome | Owner | Status | Evidence or remaining gate |
|---|---|---:|---|---|
| A-01 | Bus, not Driver, owns Reset/write-hold effects shared by one wire | 1 | CLOSED | shared-Bus tests |
| A-02 | failed initialization retains binding for later recovery | 2 | CLOSED | absent-at-boot hot-plug test |
| A-03 | Backend is externally owned with explicit lifecycle | 1 | CLOSED | bind/end tests |
| A-04 | replacement config is validated before mutation | 2 | CLOSED | transactional bind tests |
| A-05 | writes expose committed-prefix and indeterminate effects | 3 | CLOSED | write fault matrix |
| A-06 | Backend, Bus, and Driver are noncopyable/nonmovable | 1 | CLOSED | compile-time tests |
| A-07 | Status defaults are deterministic and dead API is removed | 1 | CLOSED | contract tests |
| A-08 | lifecycle/state admission is explicit | 2 | CLOSED | table-driven state tests |
| A-09 | failed OFFLINE recovery remains OFFLINE | 2 | CLOSED | recovery tests |
| A-10 | dead `tick()` behavior removed | 2 | CLOSED | symbol checks |
| A-11 | composite calls record one final health result | 2 | CLOSED | health tests |
| A-12 | snapshots contain scalar state, not callback pointers | 1 | CLOSED | public contract tests |
| A-13 | hidden discovery retry policy removed | 2 | CLOSED | event-count tests |
| A-14 | Bus owns shared Reset generation and physical speed knowledge | 1 | CLOSED | multi-device tests |
| A-15 | counters saturate and persistent last error survives success | 2 | CLOSED | saturation/health tests |
| A-16 | Drivers resynchronize after another Driver resets their shared Bus | 2 | CLOSED | shared-generation tests |
| A-17 | Bus validates complete callback evidence before accepting success | 1 | CLOSED | malformed-result tests |
| A-18 | rebind/end preserves retained hold and stale Drivers fail safely | 1 | CLOSED | epoch/rebind/end tests; Stage 6 separately demonstrates shutdown |
| A-19 | deadline addition is checked and stalled clocks fail closed | 1 | CLOSED | boundary/stalled-clock tests |
| A-20 | uncertain write-byte acceptance is represented without replay | 1 | CLOSED | every-index fault tests |
| A-21 | independent physical-wire instances share no mutable state | 1 | CLOSED | backend and two-Bus isolation tests; HIL-04 pending |
| A-22 | duplicate live address claims fail within one Bus; separate Buses may reuse the address | 1 | CLOSED | claim/multi-Bus tests |
| A-23 | a multi-frame public read can expose an earlier completed frame when a later frame fails, contrary to whole-call transactional output | 8 | OPEN | final audit must use fixed scratch storage and add a later-frame failure regression without changing the public API |

## Examples, packaging, and release quality

| ID | Finding/outcome | Owner | Status | Evidence or remaining gate |
|---|---|---:|---|---|
| Q-01 | supported framework is Arduino S2/S3 only | 4 | CLOSED | pinned Stage-04 builds; package verification is tracked by Q-11/Q-12 |
| Q-02 | host suite covers production paths and injected faults | 5 | CLOSED | native and sanitizer suites |
| Q-03 | physical timing remains unqualified until raw HIL evidence exists | 8 | HIL_PENDING | Prompt-08 waveform rows |
| Q-04 | example helpers/builds must work without repository-root includes | 6 | CLOSED | four pinned independent Arduino example builds use local common headers |
| Q-05 | command checking must reject placeholders and missing handlers | 6 | CLOSED | fixed catalog/registration dispatcher tests and semantic CLI checker |
| Q-06 | version generation must be deterministic | 7 | CLOSED | strict RC parser, byte-stable generator, and read-only `--check` |
| Q-07 | unsafe parser, scan, and unbounded example behavior | 6 | CLOSED | fixed 128-byte parser tests; scans/stress/raw paths removed |
| Q-08 | product `LoadCellMap` and duplicated paging do not belong in examples | 6 | CLOSED | product helper removed; page write calls production API once |
| Q-09 | concise two-device/two-pin example is missing | 6 | CLOSED | S2/S3 multi-example builds with independent address-zero tuples |
| Q-10 | consumer documentation does not match current API | 7 | CLOSED | focused link/API/version/default checker and warning-clean Doxygen build |
| Q-11 | package contents are not explicitly curated | 7 | CLOSED | exact export allowlist; safe external unpack; clean core and four Arduino builds |
| Q-12 | CI lacks final docs/package/example gates | 7 | CLOSED | pinned separated native, static, docs, version, package, and PHY-smoke jobs |
| Q-13 | destructive example input lacks a strong interlock | 6 | CLOSED | exact page-write confirmation/rejection tests; irreversible commands absent |
| Q-14 | non-protected docs contain stale protocol/product claims | 7 | CLOSED | v1/product/old-datasheet material removed and current docs check passes |
| Q-15 | mutable/irreversible HIL needs explicit authorization and evidence | 8 | HIL_PENDING | Prompt-08 authorization records |
| Q-16 | governing `tWR` wording now requires no-ACK-poll released-high hold | 1 | CLOSED | `AGENTS.md` and write tests |
| Q-17 | obsolete native-IDF example/checker contradicts the two-Arduino-example model | 6 | CLOSED | native-IDF example/checker removed; exact layout check passes |
| Q-18 | no concise synchronous multi-instance/hot-plug examples and RTOS guidance exist | 6 | CLOSED | detect debounce/no-detect polling, serial comparison, fairness tests, and one-owner guidance |
| Q-19 | docs lack simple topology, serialization, hot-plug, and firmware responsibility guidance | 7 | CLOSED | README/MIGRATION document exact ownership, detect/no-detect limits, and firmware policy |

## Closure rule

A software finding closes when the root behavior is implemented, a named test or
supported build verifies it, current documentation does not contradict it, and
no obsolete path preserves the defect. A later stage may update stale registry
bookkeeping when evidence is already present; that is not a reason to duplicate
the implementation or block unrelated work.

Physical evidence qualifies only the exact recorded setup. `HIL_PENDING` is an
honest limitation, not software failure and not hardware success.
