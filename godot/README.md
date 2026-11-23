# RM Tactics Lab · Godot 4

This directory is the native Godot 4 vertical slice of RM-simulator. The original C++/Python/browser modes remain available at the repository root.

## Run

Install Godot 4.3 or later, then run:

~~~bash
godot --path godot --editor
# or
godot --path godot --headless --script scripts/smoke.gd
~~~

The scene is a 3D tactical field. The red robot is manually controlled with WASD/arrow keys and mouse aim/fire. The blue robot follows the red robot using a small deterministic AI. Hold the left mouse button and drag to orbit the camera; use the wheel to zoom. On touch devices one finger orbits and two fingers pinch-zoom.

## Native engine boundaries

- CharacterBody3D owns robot movement and wall collisions.
- StaticBody3D owns the field floor, walls and cover.
- Area3D owns capture zones and projectile hit detection.
- RMProjectile is a real moving scene node; damage only happens on physics body collision.
- The HUD observes live nodes and does not replace gameplay with a state/event table.

The P0 slice uses an intentionally small rules subset (two robots, two bases, HP, capture score and a 420-second clock). The next Godot phases will add the official field geometry, garage data resources and the external control API while keeping the same native scene boundaries.
