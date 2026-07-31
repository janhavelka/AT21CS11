# AI coder prompt - implement one AT21CS stage

Use this with exactly one numbered packet prompt. Follow its frozen contract
and maintainer decisions. Follow `AGENTS.md`, except for Q-16 in Prompt 01 and
Q-17 in Prompt 07. Stop for any other material contradiction.

Implement only the current prompt's findings. Preserve completed earlier-stage
requirements and fix regressions you introduce, but do not implement later
stages early or change a stage's stated purpose.

Before editing:

- Read `AGENTS.md`, the packet `README.md`, `00_SHARED_V2_CONTRACT.md`,
  `FINDINGS_REGISTRY.md`, the current prompt, and all earlier prompts.
- Obtain and verify the packet's exact authoritative datasheet before making
  protocol decisions; a matching filename is insufficient.
- Inspect `git status`, the working tree, code, tests, examples, adapters,
  builds, docs, and callers. Preserve unrelated changes. Never modify the
  protected complete-driver report.
- Do not invent hardware behavior, pins, timing evidence, APIs, releases, or
  validation results.

Maintain this architecture:

- One external Backend owns one SI/O wire and timing. Its Bus owns binding
  epoch, Reset generation, address claims, diagnostics, and write-high deadline.
- Each Driver owns one device's lifecycle, identity, speed, state, and health.
  A Bus supports one to eight unique addresses; separate buses may reuse them.
- Firmware serializes each Bus; the safe default is one firmware owner for all
  channels. The library creates no task, queue, scheduler, or mutex.
- Core stays synchronous, deterministic, framework- and product-neutral.
  Firmware owns scheduling, retries, persistence, calibration, logging,
  telemetry, machine control, and safety policy.

Prompt 06's specified static FreeRTOS fixture is allowed only under
`test/consumer/firmware_owner/`; it must not enter core, public API, examples,
or packaged content.

Use the contract's exact names, values, types, signatures, transitions,
ownership, and errors. Retain no v1 shim unless required. Prefer root-cause
refactoring and shared helpers; remove patches, parallel, and superseded code.

Use fixed-size state, no exceptions, steady-state allocation, logging, hidden
retry/recovery, or unbounded wait. Validate complete inputs before I/O.
Preserve transactional outputs/configuration, whole-frame callbacks, checked
deadlines, stale-binding behavior, per-Bus effects, and distinct validation,
NACK, transport, and ambiguous-write results.

Update code, tests, docs, builds, examples, and checks together. Tests must use
production paths and cover each correction.

Spawn non-overlapping subagents for protocol, API/state, backend, integration,
and tests where applicable. The primary agent reconciles them. Assign one a
final read-only simplification/coverage review; verify every report.

Reread the prompt, inspect the diff and callers, check every criterion, remove
duplication, and run current checks plus affected earlier gates. Claim only
validation performed. Structure-only HIL is not hardware success; mutable or
irreversible HIL requires Prompt 08 authorization.

Report changed/removed files and APIs, decisions, findings closed, checks run,
blockers, and remaining later-stage or hardware work. Do not checkpoint during
this implementation turn: the separate audit turn owns the stage commit and
push under the packet README policy. Do not tag, release, publish, amend
history, force-push, or modify downstream repositories.
