extends Node3D

const StageScript = preload("res://visual/rmuc_stage.gd")
const RobotScript = preload("res://visual/rmuc_robot_visual.gd")
const HudScript = preload("res://visual/rmuc_hud.gd")

var camera: Camera3D
var camera_target := Vector3.ZERO
var camera_yaw := 0.0
var camera_pitch := -0.78
var camera_distance := 23.0
var dragging := false
var previous_pointer := Vector2.ZERO
var simulation_root: Node
var visual_robots: Array[Node3D] = []

func _ready() -> void:
	var stage := StageScript.new()
	stage.name = "RMUCStage"
	add_child(stage)
	spawn_robot("red", "R-01", "INFANTRY", Vector3(-8.6, 0.0, -4.5), 0.0)
	spawn_robot("red", "R-02", "HERO", Vector3(-8.6, 0.0, 4.5), 0.15)
	spawn_robot("blue", "B-01", "INFANTRY", Vector3(8.6, 0.0, -4.5), PI)
	spawn_robot("blue", "B-02", "SENTRY", Vector3(8.6, 0.0, 4.5), PI - 0.15)
	simulation_root = get_parent()
	if simulation_root != null:
		var runtime_arena := simulation_root.get_node_or_null("Arena")
		if runtime_arena:
			runtime_arena.visible = false
		var runtime_hud := simulation_root.get_node_or_null("HUD")
		if runtime_hud:
			runtime_hud.visible = false
		var runtime_camera := simulation_root.get_node_or_null("CameraRig")
		if runtime_camera:
			runtime_camera.visible = false
	build_camera()
	var hud := HudScript.new()
	hud.name = "RMUCHUD"
	add_child(hud)

func spawn_robot(team: String, callsign: String, role: String, spawn_position: Vector3, yaw: float) -> void:
	var robot := RobotScript.new()
	robot.team = team
	robot.callsign = callsign
	robot.unit_role = role
	robot.position = spawn_position
	robot.rotation.y = yaw
	robot.name = callsign
	add_child(robot)
	visual_robots.append(robot)

func _process(_delta: float) -> void:
	if simulation_root == null:
		return
	var runtime_robots_variant = simulation_root.get("robots")
	if runtime_robots_variant == null or not runtime_robots_variant is Array:
		return
	var runtime_robots: Array = runtime_robots_variant
	for index in mini(visual_robots.size(), runtime_robots.size()):
		var visual: Node3D = visual_robots[index]
		var runtime = runtime_robots[index]
		if runtime == null or not is_instance_valid(runtime):
			continue
		visual.global_position = runtime.global_position
		visual.global_rotation.y = runtime.global_rotation.y
		if visual.has_method("set_health"):
			visual.set_health(int(runtime.get("hp")), int(runtime.get("max_hp")))

func build_camera() -> void:
	camera = Camera3D.new()
	camera.name = "RMUCOrbitCamera"
	camera.current = true
	camera.fov = 48.0
	add_child(camera)
	update_camera()

func update_camera() -> void:
	if camera == null:
		return
	var offset := Vector3(0.0, 0.0, camera_distance)
	offset = offset.rotated(Vector3.RIGHT, camera_pitch)
	offset = offset.rotated(Vector3.UP, camera_yaw)
	camera.position = camera_target + offset
	camera.look_at(camera_target + Vector3(0.0, 0.5, 0.0), Vector3.UP)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			dragging = event.pressed
			previous_pointer = event.position
		if event.button_index == MOUSE_BUTTON_WHEEL_UP and event.pressed:
			camera_distance = maxf(12.0, camera_distance - 1.4)
			update_camera()
		if event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
			camera_distance = minf(36.0, camera_distance + 1.4)
			update_camera()
	if event is InputEventMouseMotion and dragging:
		var delta: Vector2 = event.position - previous_pointer
		previous_pointer = event.position
		camera_yaw -= delta.x * 0.008
		camera_pitch = clampf(camera_pitch - delta.y * 0.005, -1.3, -0.35)
		update_camera()
	if event is InputEventScreenTouch:
		dragging = event.pressed
		previous_pointer = event.position
	if event is InputEventScreenDrag and dragging:
		var delta: Vector2 = event.position - previous_pointer
		previous_pointer = event.position
		camera_yaw -= delta.x * 0.008
		camera_pitch = clampf(camera_pitch - delta.y * 0.005, -1.3, -0.35)
		update_camera()
