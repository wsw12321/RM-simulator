# Experimental layered arena format v1

This is a **separate, read-only geometry experiment**. It does not replace the
existing simulator map or change either C++ engine. The supplied 6000 × 4000 mm
bridge/tunnel fixture is deliberately synthetic, not an RMUC battlefield. No
source CAD, CAD bounding boxes, unordered face vertices, or official dimensions
were converted into this sample.

The format makes support height, physical solids, rule annotations and adjacency
separate so later reviewed geometry can be supplied as data without editing this
query implementation. A validated document is valid **for this format**, not an
official map, a calibrated survey, or a safe route for a real robot.

## Run

Python 3.10+ and its standard library suffice. From the project root:

```sh
python -m arena_format validate arena_format/examples/synthetic_bridge_tunnel.json
python -m arena_format query arena_format/examples/synthetic_bridge_tunnel.json \
  --x-mm 3000 --y-mm 2000 --radius-mm 200 --height-mm 600
python -m arena_format query arena_format/examples/synthetic_bridge_tunnel.json \
  --x-mm 3000 --y-mm 2000 --radius-mm 200 --height-mm 700 \
  --surface-id tunnel_floor
python tests/test_arena_format.py
```

The first probe has separate, feasible lower-floor and upper-deck candidates.
The second selects the lower floor and is blocked because it touches the 700 mm
ceiling. This says nothing about a route to the upper deck: none is modeled.

CLI stdout is JSON. Exit 0 means a validation/query completed successfully, even
when the queried location is infeasible. Exit 2 means an invalid document/probe,
file failure, or CLI usage error. Document/probe/file errors are JSON; argparse
usage errors retain conventional help on stderr.

## Python contract

```python
from arena_format import ArenaValidationError, parse_document, validate_document, query

doc = parse_document(text)          # strict JSON string -> validated dict
same_doc = validate_document(doc)   # validates only; returns original dict unchanged
result = query(doc, x_mm, y_mm, radius_mm=0, height_mm=0,
               max_slope_deg=90, surface_id=None)
```

All model/probe failures raise `ArenaValidationError`. Its `errors` attribute is
a JSON-safe list with one first-error record `{path, code, message}`. Paths use
`$.surfaces[0].height.z_mm` notation, including quoted brackets for unusual JSON
keys. Probe paths start `$.probe`. Duplicate JSON keys are rejected before they
can be overwritten. The parser never resolves source links, opens other files,
loads code or imports CAD.

`query` revalidates the document, does not cache it, and does not mutate input.
For fixed input on the same Python/math implementation, it returns deterministic
results in authored array order. No bit-identical cross-architecture floating
point guarantee is made. Unknown Python keyword arguments are ordinary Python
signature errors; JSON/HTTP adapters must reject unknown probe keys themselves.

## Data fields

`schema.json` is a strict JSON Schema 2020-12 structural description. The Python
validator additionally enforces geometry, references and resource limits; a
successful generic JSON Schema check alone is insufficient.

Every listed field is required, including explicit `null` values and empty
arrays. Unknown fields are rejected at every level. Identifiers use 1–64 ASCII
characters, begin with a letter, and then allow letters, digits, `_`, `.`, `-`.
Entity IDs are globally unique across source, layer, surface, volume and portal
arrays. The document's `metadata.id` is separate from entity IDs.

- `schema`: exactly `rmuc-layered-arena.v1`.
- `metadata`: `id`, `name`, `description`, and `official_map: false`. This version
  deliberately cannot declare an official map.
- `frame`: `units: "mm"`, `handedness: "right"`, `up_axis: "+z"`, plus nonempty
  `x_axis`, `y_axis`, `origin` descriptions. Every vertex/height is already in
  this one frame. Frame labels do not apply transforms.
- `calibration`: `status` is `synthetic`, `unverified`, `partial`, or `calibrated`;
  `notes` records registration, residuals, tolerances and unresolved gaps as
  applicable. Status is an author declaration, not a certificate issued by this
  validator. Structural validation never promotes it.
- `sources`: objects with `id`, `kind` (`synthetic`, `manual`, `cad`, `survey`),
  `reference`, lowercase SHA-256 `sha256` or `null`, and `notes`. References are
  uninterpreted provenance strings. A supplied digest's syntax is checked; the
  source itself is not fetched or authenticated.
- `boundary_mm`: one strictly convex counterclockwise XY polygon, defining the
  modeled horizontal domain, not a solid wall or an automatically inferred CAD
  bounding box. Surface polygons must lie inside it.
- `layers`: objects with `id`, `name`, and `source_ids`. Layers group surfaces for
  selection and display. They do **not** filter physical collisions.
- `surfaces`: `id`, `layer_id`, `polygon_mm`, `height`, `ceiling_z_mm`,
  `traversable`, `source_ids`. Height is an affine plane
  `z = z_mm + dx*x + dy*y`; `dx` and `dy` are dimensionless rise/run, not degrees.
  `ceiling_z_mm` is a constant world-Z upper clearance bound or `null`.
  It must exceed the floor height at every polygon vertex. A surface has no
  automatic thickness or solid underside; encode occupied solids separately.
- `collision_volumes`: `id`, `polygon_mm`, `z_min_mm`, `z_max_mm`, `source_ids`.
  These are global closed horizontal convex prisms with positive height, used
  for physical blocking. They may extend outside the arena boundary.
- `trigger_volumes`: the same prism fields, plus nonempty `surface_ids` and
  `rule_tag`. The surface list explicitly scopes an informational overlap hit.
  Trigger hits never block movement, establish legal eligibility, apply a buff,
  implement a team restriction, or change a score. A forbidden-zone annotation
  needs a separate future rule adjudicator, not an implicit physical obstacle.
- `transitions`: `id`, `from_surface`, `to_surface`, `portal_mm` (two XY endpoints),
  `bidirectional`, `max_step_mm`, `source_ids`. A portal must lie on one boundary
  edge of each of its two different surfaces. Their height differences at both
  endpoints must not exceed the authored `max_step_mm`; affine planes make the
  endpoint bound sufficient along the segment. `max_step_mm` describes a
  geometric declaration, not a robot's climb capability. Direction is explicit.

Polygons contain 3–64 vertices, without a repeated closing vertex. They must be
simple, strictly convex and counterclockwise; duplicates, collinear extra
vertices, zero-area rings, concavities and self-intersections are rejected.
Decompose concave geometry into reviewed convex pieces. This version does not
union support pieces, so a seam may conservatively reject a robot footprint.
Do not reverse or convex-hull raw CAD samples to bypass failed topology review.

## Query meaning and output

Arguments are finite numbers, not booleans. Default radius and height are zero,
so the default is a point feasibility query. Coordinates use mm. A nonzero
radius/height models a conservative **upright circular robot envelope**, not a
mesh, wheel contact model, orientation-dependent rectangle, or carried payload.
The caller must choose envelope dimensions and a slope limit appropriate to its
own experiment. No official robot dimensions or mobility limit are assumed.

Only surfaces containing the XY center are returned. If `surface_id` is supplied,
only that known surface is considered. At stacked XY positions, all candidates
remain separate. With no selection, top-level `feasible` means **at least one**
candidate passes. It does not select that surface or establish access to it.
An outside point or a known selected surface elsewhere yields no candidates and
`feasible: false` rather than a malformed-input error.

For each candidate:

1. Require `traversable: true` and the full circular footprint inside that one
   support polygon and the arena boundary. Tangency to their edges is allowed.
2. Compute support height `zc`, gradient magnitude `g = hypot(dx,dy)`, slope
   `degrees(atan(g))`, and full-circle support range `[zc-r*g, zc+r*g]`. Require
   slope no greater than `max_slope_deg`. The broad range remains conservative
   even on a rejected footprint extending beyond the surface.
3. Use world-Z envelope `[zc-r*g, zc+r*g+height]`. This is a conservative volume
   around an upright robot on a slope, **not** a simulated pose. A declared ceiling
   must be strictly above its top. Report `clearance_mm = ceiling-(zc+r*g)` or
   `null` when that surface declares no ceiling. This is declared ceiling headroom,
   not the nearest distance to every collision object.
4. Check circle/prism intersections against all collision volumes. Touching a
   solid's side or underside blocks. Resting exactly on a solid's top is allowed
   when the whole envelope is above it. This permits the bridge slab to block
   the lower route without blocking its upper support surface.
5. Report surface-scoped trigger intersections, including touching, independently
   of feasibility. These are volume-overlap annotations, not official detector,
   occupancy, interaction-card, team, or activation semantics.
6. Report nearby outgoing transition IDs when the center is within the footprint
   radius of their segment. Do not certify portal width, destination footprint,
   robot step capability, swept motion, a continuous route, or layer transfer.

A footprint crossing a seam can therefore fail **both** adjacent surfaces despite
an explicit portal being nearby. This is an intentional conservative limitation,
not evidence that the physical seam is impassable.

The response is:

```json
{
  "schema": "rmuc-layered-query.v1",
  "feasible": true,
  "point_mm": [3000, 2000],
  "robot": {"radius_mm": 200, "height_mm": 600, "max_slope_deg": 90},
  "selected_surface_id": null,
  "candidates": [{
    "surface_id": "tunnel_floor", "layer_id": "ground_layer",
    "support_z_mm": 0, "support_range_mm": [0, 0], "slope_deg": 0,
    "clearance_mm": 700, "feasible": true, "reasons": [],
    "collision_ids": [], "trigger_ids": ["tunnel_test_trigger"],
    "transition_ids": []
  }],
  "warnings": ["Model limitations and calibration status are stated here."]
}
```

This shortened example omits the real query's second, upper-deck candidate.
Reason codes are `not_traversable`, `footprint_outside_surface`,
`footprint_outside_boundary`, `slope_exceeded`, `ceiling_clearance`, `collision`.
All reasons/collisions are retained rather than stopping at the first blocker.

## Bounds and numeric limits

- Input JSON: at most 262,144 UTF-8 bytes. Parsed/direct documents must also fit
  262,144 bytes when compactly serialized with ASCII escaping.
- Maximum tree depth 16 and node count 20,000. Cycles, shared containers in direct
  Python input, subclasses/custom objects, oversized strings and non-string
  object keys are rejected. Convert reusable Python objects to an actual JSON
  tree before validation. Object keys are capped at 128 characters.
- At most 256 entries in each entity array; at least one source, layer and surface.
  Up to 32 calibration notes; field-specific string lengths are in the schema.
- XY/Z coordinates and surface vertex heights: −1,000,000..1,000,000 mm.
  `dx`, `dy`: −10..10. Probe radius/height and declared step: 0..10,000 mm.
  Probe slope limit: 0..90 degrees.
- Numeric comparison tolerance: **1e-7 mm** (and 1e-7 degrees for a slope comparison).
  This accommodates arithmetic roundoff and is not clearance padding, structural
  tolerance, registration uncertainty or a real-world safety guarantee. A caller
  needing a safety margin must inflate its envelope deliberately.
- Validation is bounded, fail-fast, and uses no network or third-party dependency.

## Honest scope and future calibration

Not implemented: official battlefield boundaries, raw CAD import or repairs,
source-to-world transforms, holes/curved shapes, non-convex union containment,
sloped collision prisms or ceilings, step-climbing physics, orientation, friction,
ballistics, sensing/visibility, dynamic mechanisms, motion integration, swept
collision, pathfinding, rule execution or robot-specific payload articulation.
The existing combat and objective engines keep their original geometry.

The independent official-source audit is a reason to keep this boundary: the
manual's nominal 28,000 × 15,000 mm core differs from the raw ground object's
approximately 28,000 × 16,003 mm projected bound; the local G surfaces are sloped;
one inspected resource-area BRep is invalid; 41 of 68 instances remain outside
that partial inspection. None establishes a validated playable boundary. This
module does not import the audit datasets or treat those dimensions as a map.

Before replacing the synthetic fixture, independently establish the true inner
boundary, coordinate registration and local ground planes; inspect finite face
loops/solids and remaining structures; preserve layered clearances; attach source
IDs/hashes and calibration caveats; and review source distribution rights. Record
registration evidence in provenance/calibration notes and supply coordinates
already transformed into the declared frame. Encode a genuinely new shape/model
capability in a new version rather than silently weakening v1 validation.
