# Independent sourced-parameter review

Status: source, resolver, native boundary, session-helper, and integration review
passed. No outstanding blocking finding remains. Real-browser QA is not claimed.

This review concerns an additive, partial parameter projection into the existing
experimental roster engine. It does not certify official robots, the official
starting lineup, official match simulation, real-browser rendering, or strategy
strength. No native source, reused rule source, or old replay schema is changed
by this review.

## Independently checked source

The reviewed document is the official **RoboMaster 2027 competition rules manual,
Chinese V1.0.0 (20261015)**. Its SHA-256 was checked directly:

`9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be`

All page references below are printed pages; physical PDF page = printed page + 1.
The independent reviewer inspected rendered page pixels, not just extracted text
or the implementation author's interpretation: pp13,17,26,65,66,89,91,92,93,94,108,109,110.
The reviewer also independently rerendered physical PDF pp92-95 directly from
the pinned PDF and confirmed byte-identical rendered pixels for printed pp91-94.
The extracted pp21-28,89-90 and the separately prepared rules matrix were also
cross-checked. The matrix is a research aid, not a stronger authority than the PDF.
The source is a fixed manual version, not a claim to have incorporated subsequent
official Q&A or amendments.

### Allowed parameter projections

- Infantry: 40 frozen snapshots, formed from two chassis choices, two launcher
  choices, and levels 1-10. Table 5-17 (pp92-93) supplies only the projected
  maximum HP. Table 5-18 (pp93-94) supplies 17 mm heat limit Q0 and base cooling C.
  Conversion Q0 -> native `heat_limit_tenths` is exactly Q0 x 10; C remains heat
  per second. Existing native 100 ms cooling subtracts C tenths each boundary.
- Heavy: four HP-only snapshots from table 5-16 (pp91-92): 400,450,500,600.
  The 600 row condition is completion of third assembly **or** successful
  installation of the modular 42 mm launcher. It is not an AND condition.
  The native engine's 17 mm shot heat and projectile model cannot stand in for
  the heavy 42 mm weapon. Heavy heat/weapon fields therefore remain synthetic.
- Sentry: two HP-only snapshots from table 3-6 (p26): automatic 400 and
  semi-automatic 200. These labels select a source table row, not implemented
  official controller restrictions. Source base heat/cooling values exist, but
  this release does not apply them as effective sentry heat behavior.

The complete independent infantry oracle is in
`tests/test_robot_profiles_review.py`. It explicitly protects against these
easy interpolation errors:

- Power-priority maximum HP reaches 400 at level 10, not 375.
- HP-priority maximum HP is 400 at both levels 9 and 10.
- Cooling-priority heat limit is 120 at level 10, not 112.
- Burst-priority cooling is 5,7,9,11,12,13,14,16,18,20, not a fixed increment.

### Source boundaries that must stay visible

- Page 13 merges older Hero and Engineer roles into Heavy. Old-season role names
  or tables cannot silently create 2027 presets.
- Page 17 distinguishes initial HP, current HP, and maximum HP. Page 89 says
  infantry starts the match at level 1. Page 91 says upgrades add the cap
  increase to current HP, not restore full health. Resetting a level-10 or
  later-assembly snapshot with full current HP is an experimental initialization
  choice, not a sourced official round start or implemented upgrade system.
- Page 109 starts sentry in movement posture and applies one-third cooling after
  relevant buffs. It says to take integer values without specifying a rounding
  direction. Semi-automatic base cooling 10 therefore does not support silently
  choosing 3, 3.333..., or 4 as an official effective cooling value. Enhanced
  attack can force heat to zero. Base table Q/C alone would omit material
  posture behavior; keeping this release HP-only is the conservative projection.
- A muzzle-speed **cap** is not a measured or required projectile speed.
  Chassis power is not translation speed. Construction dimensions do not define
  the experimental circular collider. Allowed projectile quantity is not a
  physical initial inventory. Table 5-2 armor damage (p65) is not an unqualified
  universal damage scalar for every synthetic impact. These values stay out of
  the source-applied field set.
- Buffs, XP, upgrades, posture, assembly transitions, power, official class
  legality, supply, penalties, initial allowance, respawn, and armor detection
  are not implemented by selecting one of these presets.

## Trust and compatibility review

The original native source identity must remain:

`2ee07a70036b4759ea409c704f95ba2de41a02794087c634fed744c943f82f37`

The additive resolver reconstructs provenance from release-pinned catalog/audit
bytes, validates every remaining numeric field with the original native contract,
and requires the original `SYNTHETIC-` native profile IDs. Source metadata is not
sent into native robot records, policy observations, or raw replay schemas.

A configuration digest is a consistency check, not trust by itself. Rehashing
altered provenance, claiming completeness, changing source/version, or substituting
derived values must still fail reconstruction against the trusted catalog.

A replay envelope binds an explicit source configuration to the original raw
native replay. Structural envelope validation alone does not establish successful
deterministic replay. The unchanged native bridge must reconstruct the action
history before import can succeed. Neither hash binding nor deterministic replay
authenticates the historical controller labels or proves when a source selection
was first made. A matching raw synthetic replay must not gain a source badge by
value inference; legacy import remains legacy and clears any previous session
source association.

## Verification record

Reviewed catalog byte SHA-256:
`d4b8257a1c7a2dee773f28b3646d53bf2c3cf160597b920d5d1676bc30e614d3`

Reviewed source-audit byte SHA-256:
`1e6a40af79a5a9124716ff375be2786291a96eb214cd13a22fea0e2c5e8ebbb1`

- Independent source-pixel and table-array review: completed as described above.
- Native source identity: checked before and after implementation review; matched
  the original pin. Additionally, all 36 original manifest-listed files under
  `src/`, `roster_mode/`, and `rules/` were compared against their pre-milestone
  SHA-256 entries and were byte-identical. All 63 manifest-listed C++/header/
  direction-include files across the project were also byte-identical.
- `python3 tests/test_robot_profiles_review.py`: **28 tests passed**, including
  every source row and selection mapping, all residual boundaries and strict
  types, exact native HP/heat projection for all 40 infantry records, raw/wrapped
  replay compatibility, rehashed metadata and action tampering, native candidate
  failure atomicity, session source-context clearing/carrying, catalog drift,
  read-only preparation, and stale revision/tick rejection before mutation.
- Existing `test_roster_bridge.py`: **11 passed**; existing
  `test_roster_review.py`: **10 passed**. These are focused regression checks,
  not a substitute for the final aggregate workflow.
- Source-session helper review passed, including matching raw values never gaining
  provenance. The separate HTTP/JavaScript integration suites were inspected.
  The integration author reported these passing after the final source-label fix
  and actual combat/capture scenario: `test_sourced_profiles_http.py` **6/6**,
  `test_roster_http.py` **7/7**, `sourced_profiles_ui_handlers.cjs` **PASS**, and
  the old `roster_ui_handlers.cjs` fast guard subset **PASS**. This evidence is
  distinguished from this reviewer's own 28-test run above; none is real-browser QA.
- Two UI findings were sent for correction: the heavy role was incorrectly
  rendered as the older Hero label, and the catalog validator expected an array
  where the pinned catalog's excluded-field reasons are an object. These findings
  did not affect catalog values or native behavior. Both corrections were checked
  in final code; the UI suite now explicitly asserts the Heavy/重装 chooser label.
- Real Chromium/browser rendering, pointer capture, mobile behavior, and download
  persistence: not run. Chromium is blocked in this environment; no retry or
  bypass is part of this review.

## Re-pin note · 2026-11-10

The identities above are the reviewed ones. For native Windows support the
roster bridge's pipe transport moved to the shared `native_pipe.py`, which is
now part of the roster source identity. Only the pins changed: native source
identity `11ea10bb3434847fa9793ac856f7d01a63043ddd81782345aa0d519d081e6af5`,
catalog byte SHA-256
`54bc39785183892a979b3c96fd922229c34ebee96f3f5dde7ca0fe9f31456da6` (the
catalog differs from the reviewed bytes only in `native_source_pin`). The
configuration example was regenerated with `python -m robot_profiles_v1 resolve`
from the unchanged selection example; it differs only in these pins and its
`configuration_sha256`. Catalog values, sources and resolver behavior were not
re-reviewed because they did not change.
