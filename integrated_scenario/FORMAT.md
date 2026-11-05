# Integrated logical-capture scenario format v1

This is a **manually adjudicated tactical experiment**, not full RMUC, physical
simulation, official timing, a UI or WASM. All JSON keys described below are
required. Unknown/duplicate keys, floats, booleans as integers, invalid Unicode,
non-UTF-8, depth >32 and incompatible versions fail before native execution.

## Scenario

- `schema`: `rmuc2027-integrated-scenario-v1`.
- `scenario_id`: 1–80 ASCII letters/digits/dot/underscore/hyphen.
- `description`: nonempty text, at most 2048 Unicode characters.
- `fact_status`: `MANUALLY_SUPPLIED_ADJUDICATED_FACTS`.
- `phase_policy`: `ENGINEERING-NOT-OFFICIAL-capture-continuation-credit-judge-v1`.
- `capture_policy`: `strict-reject-unadjudicated-v1` only.
- `timing_policy`: `explicit_milliseconds_external_credits_no_sampling-v1`.
- `world_contract`: `EXTERNAL_FACTS_COMPLETE_NO_UNLISTED_CONTROL_TRANSITIONS`.
  Author asserts no unlisted external control transition (including outpost
  destruction) occurs. This assertion is not verified against a referee.
- `initial_logical`: object with `red_outpost`, `blue_outpost`, `red_base`,
  `blue_base`; each has boolean `red` and `blue`. At the closed initial timestamp
  0, all positions have their original owner's Full control and zero progress.
  Logical values are whole-position adjudicated activation with any eligibility,
  card/robot aggregation and 2 s deactivation grace already handled upstream.
  No arbitrary detector stream is promoted to whole-position activation here.
- `events`: 1–512 complete boundary objects. An event has:
  - `sequence`: consecutive integer starting at 1, including rejected attempts.
  - `elapsed_ms`: integer 1–420000, **strictly increasing across all attempts**.
  - `source_note`: nonempty text up to 2048 characters.
  - `logical_inputs`: the four-position object, each value `null` (no new input)
    or a complete `{red: boolean, blue: boolean}` snapshot. Values persist only
    while that CaptureAttempt is active. Redundant inputs fail, as do input at
    its exact capture threshold and input after an earlier capture endpoint.
  - `continuations`: the four-position object, each null or the object below.
  - `credits`: array of `{sequence, source_note}`. Sequence is 1–2^64−1 and must
    follow the last committed credit by one. Each explicitly supplies a settled
    one-second credit; up to 4096 across the entire scenario. No schedule, initial
    credit, accrual, interval, fractional remainder or number of credits is
    invented or verified. At this boundary every credit uses ONLY the integrated
    derived control. A `control` or manual `control_score` field is prohibited.
  - `snapshot`: `{red, blue, referee_ruling, simultaneous_destruction_resolved}`.
    Each team has `base_hp` (integer 0–2^63−1), `attack_damage` and
    `surviving_robot_hp` (same integer range or explicit null). These are complete
    external facts, never inferred/carried from HP changes, physics or robots.
    `referee_ruling`: `none`, `round_forfeit`, `other`; non-none produces explicit
    `UnsupportedRefereeRuling`. Simultaneous flag is boolean and remains an
    external assertion. Missing tie-break facts produce the unchanged judge's
    errors only when the corresponding tier is needed.

### Temporary-control handoff

A continuation has exactly:
- `from_ms`, `through_ms`: integers 0–420000.
- `assertion`: `TEMPORARY_CONTROL_UNCHANGED_NO_RESTORATION_OR_OUTPOST_LIFECYCLE_TRANSITION`.
- `source_note`: nonempty text up to 2048 characters, explaining external basis.

CaptureAttempt stops at Temporary and its own snapshot remains frozen at that
exact endpoint. If it completes before a boundary, the continuation must cover
exactly `[capture endpoint, boundary]`. If already completed, it must cover
`[previous accepted integrated time, boundary]`. Without this explicit assertion,
`ExternalContinuationRequired` rejects the whole transaction. Wrong coverage
produces `ContinuationIntervalMismatch`; a continuation with no positive
unsupported interval is `UnexpectedContinuation`. Logical input for an already
completed attempt is `UnsupportedTemporaryLogicalInput`. No arbitrary new control
state, restoration transition, restart, destruction or rebuild can be supplied.
Those mechanics require a future independently versioned adapter. The accepted
continuation only extends **externally adjudicated control validity**, not the
CaptureAttempt clock, logical state or progress. A long unsupported interval is
never silently consumed. A capture exactly at a boundary needs no continuation
until a later boundary; it may be credited in that boundary under the named phase
policy. No official simultaneous ordering is claimed.

## Atomic phases and authority

Validate the whole scenario first, including strict time/sequence order and
bounds. For each event: reject finalized state before work; copy every component;
advance all four captures in fixed position order; validate all required exact
external continuations; derive the four-position ControlSnapshot; apply supplied
credits in listed order; construct the MatchSnapshot using ONLY candidate ledger
scores; judge; seal ledger on terminal decision; then commit all components at
once. Capture, continuation, credit or judge error discards every candidate
mutation. All rejected records have equal before/after. Audit sequence and input
horizon include rejections, but authoritative accepted state does not change.
Phase order and position visitation order are engineering audit policies, not a
referee event scheduler. Inputs for each timestamp are complete/atomic and that
timestamp is closed, preventing advance(T) then input(T).

## Logs, API, replay and checkpoints

Log schema: `rmuc2027-integrated-event-log-v1`. Records embed supplied event,
sequence, before/after state, candidate capture attempts (including actual endpoint,
requested endpoint, unconsumed time and newly generated semantic traces),
continuation checks, derived control, credit attempts, resolved snapshot and
accepted decision or explicit error/source. Candidate outputs are NOT committed
on rejection. Initial state includes the four initialized capture snapshots.
Final state records accepted integrated time, frozen capture snapshots, derived
control, ledger totals/sequence/sealed flag and final judge snapshot/decision.

`identity` includes complete unchanged capture and match provenance, all adapter,
rule source and format file hashes, version names, aggregate source fingerprint
and compiled-in native fingerprint. Verify it before execution and again on
return. Original standalone provenance's `connected_to_*: false` fields describe
those unmodified modules' own delivery, not this new adapter. The integration
identity expressly names the consumed modules and scope.

Canonical hashing uses sorted keys, compact separators, ASCII escapes, no NaN,
no floats, no newline. Logs contain the complete scenario, scenario digest and
log digest (excluding itself). `verify` cold-reexecutes and compares the entire
canonical log, including failures. These hashes detect incompatibility/drift,
not maliciously re-signed data, official authenticity or binary/compiler attestation.

Public Python API in `cli.py`: `validate_scenario`, `run_scenario`, `verify_log`,
`make_checkpoint(log, prefix_count)`, `restore_checkpoint`, `resume_checkpoint`.
Returned values are detached from mutable caller input. A checkpoint has schema
`rmuc2027-integrated-prefix-checkpoint-v1`, complete attempted `prefix_scenario`,
`identity`, `prefix_log_sha256`, canonical `state`, `state_sha256`, and
`checkpoint_sha256` excluding itself. It includes every failed attempt as well
as successful boundaries and therefore all source notes/policies/provenance.
Creation reexecutes the selected prefix, restore reexecutes the **whole prefix**
and checks identity, log digest and full canonical state. This is O(prefix)
replay, never an O(1) snapshot or arbitrary private C++ state restoration.

A continuation document is exactly `{schema: "rmuc2027-integrated-continuation-v1",
checkpoint_sha256, events}`. It is bound to the verified checkpoint, has at least
one event and uses global sequence numbers and strictly later times than all
prefix attempts. Resume validates prefix+suffix together, reexecutes the combined
scenario, checks its prefix log/state consistency, and returns a normal full log.
No overwrite, state injection, policy migration or permissive import exists.

CLI: `run SCENARIO`, `verify LOG`, `extract LOG`, `checkpoint LOG --through N`,
`restore CHECKPOINT`, `resume CHECKPOINT CONTINUATION`, `build-id`. Creation and
extraction commands accept `--output NEW_FILE` or `-` (stdout). Exit 0 means
successful execution/verification; run/resume return 1 for a valid error-containing
scenario, 2 for malformed input/build/replay/output failure. Restore verifies and
emits the normal prefix log, not a newly fabricated snapshot.

Scenario/continuation input and extracted scenario: 8 MiB. Logs/checkpoints:
32 MiB. Read at most limit+1 bytes; max 32 JSON container levels, max 20-digit
integer tokens; native lines max 16 KiB, native events/credits independently
bounded, 15 s subprocess timeout. API values pass the same bounded JSON boundary.
Emitted JSON is readable UTF-8 (not Unicode-escaped), sorted, indented two spaces;
**exact serialized bytes** are guarded before opening a file/writing stdout.
Never partially emit oversized output or overwrite an existing file. Successful
emissions fit the next matching command's input limit. Limits are engineering
safeguards, not official game constraints or match-loss rulings.
