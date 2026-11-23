extends SceneTree

const Adapter = preload("res://scripts/sim_adapter.gd")

## Determinism and replay contract probe for the Godot adapter.
## This does not replace native rule tests. It catches accidental divergence in
## the client-side preview that would make a replay or imported snapshot drift.

func _init() -> void:
	var left := Adapter.new()
	var right := Adapter.new()
	root.add_child(left)
	root.add_child(right)
	left.reset()
	right.reset()

	for tick in 120:
		var red_action := {
			"dx": 1000 if tick < 60 else 0,
			"dy": 250 if tick % 3 == 0 else 0,
			"aim_x": 10400,
			"aim_y": 3800,
			"fire": tick % 7 == 0,
		}
		var blue_action := {
			"dx": -1000 if tick < 90 else 0,
			"dy": -250 if tick % 5 == 0 else 0,
			"aim_x": 1600,
			"aim_y": 3800,
			"fire": tick % 11 == 0,
		}
		var actions: Array[Dictionary] = [red_action, blue_action]
		assert(left.step(actions))
		assert(right.step(actions))
		assert(left.state_hash() == right.state_hash())

	assert(left.tick == 120)
	assert(left.journal.size() == 120)
	assert(left.export_replay().contains("frames"))

	var clone := Adapter.new()
	root.add_child(clone)
	assert(clone.import_native_snapshot(JSON.stringify(left.snapshot())))
	assert(clone.tick == left.tick)
	assert(clone.state_hash() == left.state_hash())
	print("GODOT_ADAPTER_QA_OK tick=%d hash=%d frames=%d" % [left.tick, left.state_hash(), left.journal.size()])
	quit(0)
