# Sourced 2027 parameter projection, v1

This is a **partial, audited parameter projection into the unchanged synthetic
roster engine**, not an official robot class implementation or a faithful RMUC
match simulator. The words “heavy”, “infantry” and “sentry” identify the source
row only. Unsupported class behavior is not added by choosing a row.

## Isolation and release identity

No file in `roster_mode/`, any native physics/SDK source, or any existing rule
module is changed by this feature. The pinned native source identity is
`2328a3428507c59f156683eb4afc14adcdb1ba5513115c899cb747ffbeeb1428`.
(It was `2ee07a70036b4759ea409c704f95ba2de41a02794087c634fed744c943f82f37` until
2026-11-10, when the roster transport moved to the shared, Windows-capable
`native_pipe.py`; roster physics, SDK and rule sources did not change, and the
catalog changed only in its `native_source_pin` field.)
The legacy `rmuc-experimental-roster-replay.v1` format remains a raw synthetic
replay even if its numeric values happen to equal a catalog row. Source claims
are never inferred from numbers, profile IDs, a caller-provided badge or a URL.

The separately versioned catalog and source audit have exact byte SHA-256 pins
in `pins.py`. Every sourced operation checks both files. They can change in a
new catalog release without changing the legacy native identity. Existing
sourced wrappers fail closed if their catalog identity or any reconstructed
source claim differs. These hashes are integrity checks, not signatures or
proof that a policy generated a replay.

## Explicit selection

`resolve_config(selection)` accepts exactly:

- `schema`: `rmuc-sourced2027-selection.v1`.
- `catalog`: exact current `{id, version, sha256, source_audit_sha256}` identity.
- `strategies`: all five unchanged native strategy selections.
- `robots`: 2–8 records, each with exactly `id`, `team`, `spawn`, `eligible`,
  `preset_id`, `conditions`, `synthetic`.

`preset_id` must name an exposed catalog row. `conditions` must exactly equal
that row's trusted conditions, including the selected role, level/options and
static-snapshot restrictions. Booleans are not interchangeable with integers,
and integer-looking floats are not integers. Unsupported options and unknown
keys are rejected.

`synthetic` must contain `id` plus **every numeric engine field not supplied by
the selected row**, and no other field. Its ID must still start `SYNTHETIC-`.
Sourced values cannot be overridden. There is no implicit residual/default
fill. `example_selection()` is a separately labeled editable example which
copies residual inputs from the old synthetic demo; users submit those values
explicitly before resolution.

The resolver projects only verified `max_hp` and, for eligible audited 17 mm
snapshots, `heat_limit_tenths` and `cooling_per_second`. Source heat capacity is
converted exactly from heat units to tenths by multiplication by ten. It does
not convert a projectile speed limit into actual projectile velocity, official
initial projectile allowance into an ammunition inventory, or chassis power
into travel speed. The remaining engine inputs stay synthetic assumptions.

The native validator retains authority over numeric bounds, stable IDs, reused
profile consistency, team counts, spawns, walls and circle overlap. Selections
are canonicalized by stable robot ID. Strategies, team assignment, spawning and
eligibility are synthetic scenario assumptions, not source-backed qualification.

## Configuration envelope

`resolve_config` returns exactly:

- `schema`: `rmuc-sourced2027-roster-config.v1`.
- `official`: false; `complete`: false.
- `scope`: `partial-parameter-projection-into-synthetic-roster-physics`.
- `selection`: canonical explicit selection.
- `resolved_config`: exact native six-key config.
- `provenance`: catalog identity, native pin, source documents, per-robot source
  selection and per-field provenance, synthetic context and limitations.
- `configuration_sha256`: canonical-JSON SHA-256 of the preceding fields.

Each per-field record names the resolved numeric value, engine unit, either
`sourced-parameter` or `user-supplied-synthetic-assumption`, and source evidence
or null. The provenance does not declare missing behavior implemented. If heat
is disabled, the heat parameters remain configured source values but have no
thermal effect. Source fields do not override any synthetic engine strategy.

`validate_config_envelope` does not merely check the hash. It reconstructs the
entire expected envelope from the selection and the release-pinned catalog and
compares canonical JSON. Changing a source locator, value, condition, label,
exclusion, synthetic classification, completeness flag or derived native config
is rejected even if the caller recalculates the checksum.

## Replay envelope and transactions

The new `rmuc-sourced2027-roster-replay.v1` wrapper contains exactly `schema`,
`official:false`, `complete:false`, `configuration`, `native_replay`, and
`native_replay_sha256`. The nested native replay is unchanged. Its canonical
configuration must exactly equal the validated resolved configuration; its
native source/engine/observation identity, frame structure, hashes and false
`official` flag are checked. Native frame arrays in this new wrapper are
canonical, even though the separate legacy raw importer can reorder inputs.

`validate_replay_wrapper` verifies structure and provenance, **not** a full
simulation. A valid checksum alone never proves a replay correct.
`import_process(process, observer, wrapper)` additionally passes the unchanged
raw replay to `RosterProcess.import_replay`, which reruns every recorded action
and checks native state hashes using its existing disposable candidate process.
`reset_process(process, observer, configuration)` similarly validates first,
then calls the unchanged transactional reset. A validation or native candidate
failure leaves the committed run untouched. The caller must update its active
source metadata only after these functions return successfully.

Legacy raw reset/import routes must remain separate. A successful raw reset or
raw import clears any sourced UI context. A failed raw or sourced transaction
must not replace existing source context. Steps never rewrite provenance or
reclassify raw runs. Exports must revalidate active source context before
wrapping the current native replay.

## Local CLI

- `python3 -m robot_profiles_v1 catalog`
- `python3 -m robot_profiles_v1 example [preset_id]`
- `python3 -m robot_profiles_v1 resolve selection.json`
- `python3 -m robot_profiles_v1 validate configuration.json`
- `python3 -m robot_profiles_v1 validate-replay replay.json`

Input files use strict JSON without duplicate keys or non-finite numbers.
Validation errors exit with status 2. The CLI is read-only and never starts a
simulation, changes a run, installs software or publishes anything.

## Local HTTP session integration

The existing `/api/roster/*` raw endpoint shapes remain unchanged. New routes:

- GET `/api/roster/profiles/catalog`: pinned catalog.
- GET `/api/roster/profiles/session/state?observer=ID`: atomic session packet.
- GET `/api/roster/profiles/replay`: full-action sourced wrapper; rejects raw runs.
- POST `/api/roster/profiles/prepare`: `{config,presets:{robotID:presetID}}`.
  This explicit convenience operation uses submitted raw values for every
  residual and builds independent synthetic profile IDs; it does not mutate a run.
- POST `/api/roster/profiles/resolve`: full explicit selection.
- POST `/api/roster/profiles/validate`: `{configuration:envelope}`.
- POST `/api/roster/profiles/session/reset|step|import`: original raw request
  nested in the guarded mutation envelope described below.
- POST `/api/roster/profiles/reset`: `{observer,configuration}` nested request.
- POST `/api/roster/profiles/import`: `{observer,replay:sourced_wrapper}` nested request.

All new mutation routes require exactly
`{expected_revision,expected_tick,request}` from the latest session packet.
A stale revision or tick returns409 with `resync_required:true` before native work;
the client pauses and resynchronizes rather than retrying automatically.
Every successful raw or wrapped mutation advances the session revision.

State and successful mutations return
`{schema:"rmuc-roster-profile-session.v1",run_revision,state,active_config,provenance_status}`.
`state` is the unchanged filtered native public payload. `active_config` is the
verified envelope or null. Status is `sourced-partial`, `synthetic` or `unverified`.
Association follows the committed native process lineage, not numeric similarity.
Ordinary raw reset/import success clears it, failed operations preserve it, and
ordinary successful steps carry it. State/configuration are public experiment
inputs; no current opponent telemetry is added to policy observations.

Browser draft exports are explicitly drafts; only a committed verified run can
export a sourced replay. Catalog/source failure disables claims and source actions
without preventing ordinary synthetic operation. A network-uncertain mutation
pauses the interface and requires verified state recovery; it is not retried.
