# Configurable experimental tactical roster

This local scene extends the playable tactical experiment to **1–4 robots per
side**, with a synthetic **2v2** default. It is not an official RMUC lineup,
physical model, referee implementation or win-rate estimator. The earlier
`/tactical.html` two-robot scene and all older modes remain separate.

## Run and control

From the project root, stop any running server before rebuilding:

```sh
bash tools/build.sh
python3 server.py
```

Open `http://127.0.0.1:8765/roster.html` on the same computer. This is a native C++
authority behind a loopback Python bridge, not WebAssembly or a published site.
The scene supports choosing a robot viewpoint and selecting manual or trusted
C++ control independently for each configured robot. Human input controls only
the selected manual robot. Other manual robots receive neutral input. Switching
viewpoint or control source pauses play and releases held input.

WASD/arrow keys move; pointer aim and Space/primary pointer fire submit the same
validated action type as a policy. Batches advance fixed 60 Hz ticks without
skipping simulation steps. A disabled robot cannot move or fire, but disabling
robots does not end the match early. The experimental judge runs at 420 seconds.

The bundled all-policy 2v2 example is intentionally symmetric: its verified run
swaps all four objectives and finishes with control scores 0:0. The reused control
formula awards points only when one team controls **both outposts** or **both
bases**, not for each individual capture. Equal team damage and remaining HP then
produce a draw. To observe an asymmetric control run, keep one side's robots
manual and stationary while the other side runs its policies, or write a different
coordination policy. Do not change the source-backed scoring formula just to make
the demonstration score increase.

The roster/profile configuration is applied only by resetting. Each robot has a
stable, case-sensitive ID, a team, an explicitly synthetic profile, a spawn pose
and an objective-eligibility flag. IDs bind actions and observations; list order
does not. The engine canonicalizes IDs in ASCII lexical order. Profile IDs must
start with `SYNTHETIC-`; reuse of one profile ID with different values is rejected.
See `CONTRACT.md` for exact fields, bounds and protocol shapes.

## What is public

The complete starting roster, profile values, spawn poses and eligibility are
**deliberately public experimental configuration**. They are not current enemy
telemetry. Objective geometry, ownership, control degree, rates and team scores
are also explicit public broadcasts.

Each policy receives an owning, frozen pre-step snapshot containing its own
position, HP, ammunition, cooldown and heat. Other robots, including teammates,
provide only ID/team/pose when within that observer's configured center-range and
wall line of sight. A visible disabled robot has no death flag or exposed HP.
Visible projectiles provide only kinematics, not owner, damage, global ID or
lifetime. Hidden current HP/heat, damage totals, detection state, grace deadlines
and capture progress are not policy inputs or ordinary UI responses.

All snapshots are made before any policy runs. Every requested action is checked
before a step commits. Controllers are trusted stateless C++ in the same native
process: owning snapshots are not a security boundary against malicious memory
access, shared globals, file/network I/O or other external side effects.

## Explicit synthetic multi-robot mechanics

- Each robot keeps its own qualified objective detector and ordinary-loss grace.
  Death clears only that robot's detector. An eligible living teammate's logical
  activation remains valid. Team activation is the OR of individual activations,
  with no second grace added after aggregation. This aggregation is not an
  assertion about official qualification rules.
- Movement proposals use one frozen world. Conservative swept circle conflicts
  block affected translations, including newly stationary obstacles, without
  collision HP damage. Disabled bodies remain movement obstacles.
- Living robots emit before same-tick damage commits. Friendly projectiles pass
  through their own team; disabled enemies are projectile-transparent. Wall/robot
  impact ordering and geometric approximations are named engineering choices.
- Opposing hits aggregate per victim before HP is clamped. Actual HP loss is
  credited to the opposing team, without invented individual overkill allocation.
  Simultaneous mutual disable is possible. No penalty generator, XP or official
  armor-detection model is supplied.
- Per-robot thermal states are independent. Actual emission, not requested fire,
  consumes ammunition and adds heat. Temporary heat lock does not disable movement
  or blank the tactical map. Heat creates no HP damage or attack score.

## Reused rule subset and unresolved dependencies

The scene reuses the versioned capture, control/match, settled damage and 17 mm
heat modules. Their source/provenance records remain available under `rules/`
and `tactical_mode/*SOURCES.md`. Reuse does not establish full competition fidelity.
The default explicitly selects timer-before-input capture ordering, per-second
control-credit sampling, cooling before the firing gate and observed shot before
death reset. These event phases are engineering choices. The higher heat-threshold
prose/diagram conflict remains strict by default, with named opt-in alternatives.
Unsupported capture transitions reject atomically rather than guessing.

Temporary control remains latched. Bases use a positive alive sentinel. There is
no official arena, respawn, restoration, outpost/base destruction, robot class
system, power budget, level/buff/upgrade system, supply economy, first-person video,
aerial/dart/radar subsystem or complete referee rules. A configured profile is not
made official by using one value that appears in a rule table.

## Replay and failures

Developer replay records each resolved per-ID requested action, source label and
diagnostic observation/state hashes. It is a developer artifact that reveals all
action streams, not a filtered player view or proof of authenticated policy intent.
Import reconstructs actions without invoking policy callbacks and requires the
matching source/configuration/schema. Old modes and older source builds are not
silently reinterpreted.

A failed step, callback, response validation or replay does not commit the candidate
world, rule state, heat, score or action log. This does not undo arbitrary external
side effects of user-written trusted C++. After an uncertain transport outcome,
the UI pauses and resynchronizes instead of blindly retrying the mutation.
Candidate reconstruction is proportional to saved history, so accelerated controls
are not a real-time performance guarantee.

## Verification limits

The project test workflow is `bash tools/check.sh`. Native, protocol and actual
JavaScript-handler/mock-DOM tests do not establish real-browser rendering, pointer
capture, mobile behavior or download persistence. Chromium execution remains
blocked in the development environment; no bypass is attempted. WebAssembly is
not built. See `../docs/VERIFICATION.md` for the final recorded checks, rather than
assuming that a supplied test file has passed.
