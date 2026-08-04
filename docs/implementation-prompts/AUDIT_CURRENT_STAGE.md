# Audit the current numbered stage

Use this wrapper in a new turn with the same active numbered prompt. Audit and
correct that stage; do not begin the next one.

Read `AGENTS.md`, the packet `README.md`, `00_SHARED_V2_CONTRACT.md`,
`FINDINGS_REGISTRY.md`, and the current prompt. Earlier numbered prompts are
completed history, consulted only when tracing a suspected regression.

Inspect `git status`, the full stage diff, production paths, Backend/Bus/Driver,
fake transport, callers, examples, tests, builds, packaging, documentation, and
finding evidence. Preserve unrelated changes and never modify the protected
complete-driver report.

Verify requirement by requirement:

- the exact current public declarations and documented defaults;
- datasheet-correct framing, address construction, ACK/NACK phases, MSb order,
  CRC, Reset/Discovery, reads, writes, speed changes, Lock, ROM zones, and
  Freeze;
- validation before I/O, bounded deadlines/waits, transactional outputs, and
  conservative write/mutation evidence;
- one Backend/Bus per wire, unique addresses within a Bus, address reuse across
  separate Buses, and no cross-instance state leakage;
- synchronous externally serialized operation with no library task, queue,
  application-facing mutex, retry, logging, or product policy, while private
  bounded Backend timing-critical facilities remain allowed;
- exact optional detect semantics: `-1` disabled, valid distinct input when
  enabled, active-high/active-low mapping, no internal pulls/debounce/poller,
  one Bus-wide sample, and callback error distinct from absence;
- hot-plug through retained binding plus one explicit `recover()` per caller
  event, with `probe()` remaining liveness-only;
- Prompt-06 detect debounce and 1,000 ms no-detect probe/recovery scheduling are
  fixed-state, wrap-safe, bounded, host-tested, and example-only;
- two examples and RTOS guidance do not introduce an owner framework, mailbox,
  request DTO, attachment generation, or scheduler.

Trace success, invalid input/state, NACK phases, transport faults, timeouts,
cleanup failures, boundaries, ambiguous writes, rebind/recovery, hot-plug, and
shutdown where applicable. Tests must exercise production paths and independent
expected values.

Fix every current-stage defect and completed-stage regression without adding
later-stage work. Simplify duplicated code and remove obsolete paths.

Stop only for a material protocol, public-API, electrical-safety,
object-lifetime, or irreversible-authorization contradiction. Resolve stale
historical wording and ordinary tooling ownership through the packet hierarchy
and document the decision.

Rerun current and affected earlier gates plus `git diff --check`. Physical HIL
is exclusive to Prompt 08. If and only if software criteria are complete, create
a non-empty `stage NN:` checkpoint commit and push it according to the packet
README. Do not tag, publish, amend, force-push, or modify downstream repositories.

Report findings, corrections, removed APIs/artifacts, requirements verified,
checks run, blockers, and deferred hardware work.
