# Godot bridge contract

`rm_sim_bridge.gd` is the narrow adapter between Godot scenes and the
authoritative RM-simulator engines. It exposes `reset()`, `advance()`, and
`export_replay()` with the same state vocabulary used by the native sandbox.

The default adapter is an explicitly non-official deterministic mock. This
keeps the Godot scene runnable while a native build is unavailable and labels
every snapshot with `transport: "mock"` and `official: false`. A production
scene must display that status and must not present the mock as an official
RMUC result.

The native integration should replace the transport implementation behind this
contract, preserving the state keys and the reset/advance transaction boundary.
The C++ command protocol is line based (`RESET`, `STATE`, `ADVANCE`, `FRAME`,
`EXPORT`). The Python loopback bridge remains the reference implementation for
timeouts, size bounds, replay validation, and filtered observations.

Run the standalone contract probe with:

```bash
godot4 --headless --path . --script godot/rm_sim_bridge_probe.gd
```
