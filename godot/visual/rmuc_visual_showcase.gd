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

func _ready() -> void:
	var stage := StageScript.new()
	stage.name = "RMUCStage"
	add_child(stage)
	spawn_robot("red", "R-01", "INFANTRY", Vector3(-8.6, 0.0, -4.5), 0.0)
	spawn_robot("red", "R-02", "HERO", Vector3(-8.6, 0.0, 4.5), 0.15)
	spawn_robot("blue", "B-01", "INFANTRY", Vector3(8.6, 0.0, -4.5), PI)
	spawn_robot("blue", "B-02", "SENTRY", Vector3(8.6, 0.0, 4.5), PI - 0.15)
	build_camera()
	var hud := HudScript.new()
	hud.name = "HUD"
	add_child(hud)

func spawn_robot(team: String, callsign: String, role: String, position: Vector3, yaw: float) -> void:
	var robot := RobotScript.new()
	robot.team = team
	robot.callsign = callsign
	robot.unit_role = role
	robot.position = position
	robot.rotation.y = yaw
	robot.name = callsign
	add_child(robot)

func build_camera() -> void:
	camera = Camera3D.new()
	camera.name = "OrbitCamera"
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
