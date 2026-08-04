# Run the current numbered stage

Use this wrapper with exactly one active numbered prompt.

Read `AGENTS.md`, the packet `README.md`, `00_SHARED_V2_CONTRACT.md`,
`FINDINGS_REGISTRY.md`, and the current prompt. Prompts 01-05 are completed
history; do not reread them as competing frozen contracts unless diagnosing a
specific regression.

Before editing:

- verify the authoritative datasheet by exact size and SHA-256 when protocol
  decisions are involved;
- inspect `git status`, the relevant code, tests, examples, builds, packaging,
  docs, and callers;
- preserve unrelated changes;
- never modify the protected complete-driver report.

Implement only the current stage. Preserve completed behavior and fix any
regression introduced by the stage. Do not retain obsolete v1 APIs, parallel
transports, compatibility wrappers, or superseded example paths.

Maintain the simple architecture:

- one external Backend and one Bus per physical SI/O wire;
- one Driver per addressed device, with unique addresses only within that Bus;
- separate wires use separate tuples and may reuse address zero;
- calls are synchronous, bounded, deterministic, and externally serialized;
- the safe default is one firmware task/loop owning all instances;
- the library creates no task, queue, scheduler, application-facing mutex,
  retry loop, or hot-plug poller; private bounded Backend timing-critical
  facilities remain allowed;
- absence retains valid bindings and firmware explicitly calls `recover()` after
  attachment;
- firmware owns RTOS messaging, retry/backoff, identity association,
  persistence, logging, calibration, machine control, and safety policy.

Prompt 06 adds no RTOS owner fixture, mailbox, application DTOs, or scheduling
layer. Do not create `test/consumer/firmware_owner/` or
`tools/run_firmware_owner_fixture.py`.

Validate complete inputs before I/O. Preserve transactional outputs, exact
NACK/transport distinctions, whole-frame callbacks, checked deadlines,
write-effect ambiguity, and independent per-Bus state.

Use subagents only when independent read-only work will materially help. Keep
one integrator responsible for shared edits and verify every report.

Stop for a genuine datasheet, public-API, electrical-safety, object-lifetime, or
irreversible-authorization conflict. Do not stop for stale historical wording or
ordinary tooling/stage ownership ambiguity; follow the packet hierarchy, choose
the simpler current result, and record it.

Run the current gates plus affected regression gates and `git diff --check`.
Claim only checks actually run. Prompts 01-07 perform no physical HIL.

Report files changed/removed, APIs affected, decisions, findings, checks,
blockers, and deferred hardware work. Do not checkpoint during the
implementation turn. Do not tag, release, publish, amend, force-push, or modify
downstream repositories.
