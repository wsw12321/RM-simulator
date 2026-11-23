extends SceneTree

func _init() -> void:
	var bridge = preload("res://rm_sim_bridge.gd").new()
	var reset_state := bridge.reset()
	assert(int(reset_state["tick"]) == 0)
	assert(reset_state["official"] == false)
	assert(reset_state["transport"] == "mock")
	var advanced := bridge.advance(12, 0, {"dx": 1000, "dy": 0, "aim_x": 10400, "aim_y": 3800, "fire": 1})
	assert(int(advanced["tick"]) == 12)
	assert(int(advanced["robots"][0]["x"]) > 1600)
	assert(int(advanced["robots"][1]["x"]) < 10400)
	var replay := bridge.export_replay()
	assert(replay["profile"] == "sandbox-v1")
	print("RM_SIM_BRIDGE_OK tick=%d transport=%s" % [advanced["tick"], advanced["transport"]])
	quit()
