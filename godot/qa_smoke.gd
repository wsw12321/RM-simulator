extends SceneTree

## Headless acceptance probe for the Godot migration.
##
## The probe deliberately checks only engine-level contracts so it can run in CI
## without rendering hardware. Game-specific rule tests remain in the native
## C++/Python suites and are not duplicated here.

const REQUIRED_PROJECT := "res://project.godot"

func _init() -> void:
	var failures: Array[String] = []
	if not FileAccess.file_exists(REQUIRED_PROJECT):
		failures.append("project.godot is missing")

	# ProjectSettings exposes the [application] section with its namespace.
	var main_scene_path := str(ProjectSettings.get_setting("application/run/main_scene", ""))
	if main_scene_path.is_empty():
		failures.append("run/main_scene is not configured")
	elif not ResourceLoader.exists(main_scene_path):
		failures.append("main scene does not exist: %s" % main_scene_path)

	var root_node: Node = null
	if failures.is_empty():
		var packed := load(main_scene_path) as PackedScene
		if packed == null:
			failures.append("main scene is not a PackedScene")
		else:
			root_node = packed.instantiate()
			if root_node == null:
				failures.append("main scene could not instantiate")

	if root_node != null:
		var node3d_count := _count_type(root_node, Node3D)
		var camera_count := _count_type(root_node, Camera3D)
		var canvas_count := _count_type(root_node, CanvasLayer)
		if node3d_count == 0:
			failures.append("main scene has no Node3D content")
		if camera_count == 0:
			failures.append("main scene has no Camera3D")
		if canvas_count == 0:
			failures.append("main scene has no CanvasLayer HUD")
		print("QA_SCENE_OK nodes3d=%d cameras=%d canvas_layers=%d" % [node3d_count, camera_count, canvas_count])
		root_node.free()

	if failures.is_empty():
		print("GODOT_QA_SMOKE_OK")
		quit(0)
	else:
		for failure in failures:
			push_error("QA: " + failure)
		print("GODOT_QA_SMOKE_FAIL count=%d" % failures.size())
		quit(1)

func _count_type(node: Node, type: Variant) -> int:
	var total := 1 if is_instance_of(node, type) else 0
	for child in node.get_children():
		total += _count_type(child, type)
	return total
