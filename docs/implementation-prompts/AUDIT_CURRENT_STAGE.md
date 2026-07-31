# AI coder prompt - audit one completed AT21CS stage

Use this in a new turn with the same numbered prompt. Audit and correct that
stage; do not begin the next one.

Reread `AGENTS.md`, packet `README.md`, `00_SHARED_V2_CONTRACT.md`,
`FINDINGS_REGISTRY.md`, the current and earlier prompts. Follow `AGENTS.md`,
except for Q-16 in Prompt 01 and Q-17 in Prompt 07. Treat maintainer decisions
as authoritative. Never modify the protected complete-driver report.

Audit current findings and completed-stage regressions. Record later work
without implementing it. Use only the packet's verified datasheet for protocol
conclusions.

Inspect `git status`, the diff, production paths, Backend/Bus/Driver and fake
transport, callers, examples, fixtures, adapters, tests, fault injection,
builds, packaging, docs, and findings. Preserve unrelated changes.

Spawn read-only subagents for protocol, API/state, backend/integration, tests,
and simplification where applicable. Require line evidence; verify every report.

Build a requirement-by-requirement checklist and verify:

- Exact files, names, values, types/defaults, constants, signatures,
  visibility, transitions, deletions, and registry evidence.
- Datasheet-correct framing, addresses, ACK/NACK phases, MSb order, CRC,
  opcodes, read termination, Discovery, speed changes, Reset, write-high
  behavior, and irreversible boundaries.
- Whole-frame callbacks; deadlines; overflow and stalled-clock handling; stale
  descriptors; post-end inactivity; bounded GPIO, critical, and power behavior.
- One Bus per wire; unique addresses per Bus; independent wires; correct
  claims, epochs, Reset generations, write deadlines, diagnostics, and external
  per-Bus serialization.
- Correct operation admission, lifecycle, rebind/reconnect, speeds,
  FAULT/OFFLINE, health, result tracking, and saturating counters.
- Validation before I/O, including `SIZE_MAX`; transactional outputs/config;
  committed-prefix and accepted-byte evidence; partial/indeterminate writes;
  verification; no replay of ambiguous mutation.
- Security Lock, ROM-zone, and Freeze behavior, with no invented commands,
  status queries, v1 shims, duplicate transports, dead methods, or obsolete
  symbols.

Confirm core is general, synchronous, deterministic, product-neutral,
fixed-size, and free of tasks, queues, mutexes, logging, persistence, retries,
calibration, telemetry, control, and safety policy. Prompt 06's fixture/DTOs
stay under `test/consumer/firmware_owner/`; they must not leak or be packaged.

Trace production code through success, invalid input/state, NACK phases,
transport error, timeout, cleanup error, boundaries, partial mutation, rebind,
recovery, and shutdown where applicable. Tests must call production logic and
use independent expected values.

Fix every current-stage defect and earlier-stage regression. Prefer coherent
refactoring, shared helpers, and deletion over wrappers or local patches. Do
not add later-stage functionality.

Reread the prompt, inspect the corrected diff/callers, verify every criterion
and finding, simplify, and rerun current checks plus affected earlier gates,
including `git diff --check`. Claim only validation performed. Structure-only
HIL is not hardware evidence; mutable or irreversible HIL needs Prompt 08
authorization.

Report findings, corrections, refactoring, removed APIs, requirements verified,
checks run, blockers, and remaining later-stage or hardware work. If and only
if the stage is complete, create its non-empty `stage NN:` checkpoint commit and
push it using the packet README policy. Do not checkpoint a blocked stage. Do
not tag, release, publish, amend history, force-push, or modify downstream
repositories.
