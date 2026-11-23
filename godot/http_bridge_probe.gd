extends SceneTree
func _init() -> void:
	var bridge = preload("res://rm_sim_http_bridge.gd").new()
	assert(bridge.transport == "python-loopback-cpp")
	print("RM_SIM_HTTP_BRIDGE_OK")
	quit()
