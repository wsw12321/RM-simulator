extends "res://scripts/visual_app.gd"

const RMSimAdapter = preload("res://scripts/sim_adapter.gd")

var adapter: RMSimAdapter

func _ready() -> void:
	super._ready()
	adapter = RMSimAdapter.new()
	adapter.name = "SimulationAdapter"
	add_child(adapter)
	adapter.snapshot_changed.connect(_on_snapshot)
	adapter.event_emitted.connect(_on_event)
	adapter.reset()
	print("GODOT_RM_SIM_OK tick=%d robots=%d" % [adapter.tick, adapter.robots.size()])
	if OS.has_feature("headless"):
		await get_tree().process_frame
		print("GODOT_RM_SIM_HEADLESS_OK hash=%d" % adapter.state_hash())
		get_tree().quit()

func _physics_process(delta: float) -> void:
	if adapter == null or OS.has_feature("headless"):
		return
	var move := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	adapter.advance(delta, {"dx": int(move.x * 1000.0), "dy": int(move.y * 1000.0), "aim_x": 6000, "aim_y": 3800, "fire": Input.is_action_pressed("fire")})

func _on_snapshot(snapshot: Dictionary) -> void:
	if robots.size() >= 2 and snapshot.has("robots"):
		var state: Array = snapshot["robots"]
		for i in min(robots.size(), state.size()):
			if robots[i].has_method("set_health"):
				robots[i].set_health(float(state[i].get("hp", 100)))
			live_targets[i] = Vector3(float(state[i].get("x", 0)) / 1000.0 - 6.0, 0.0, float(state[i].get("y", 0)) / 1000.0 - 3.8)

func _on_event(_event: Dictionary) -> void:
	pass
