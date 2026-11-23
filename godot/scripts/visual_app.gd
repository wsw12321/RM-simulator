extends Node3D

const RobotVisualScript = preload("res://scripts/robot_visual.gd")

const FIELD := Vector2(12.0, 7.6)
const RED := Color("#ff4765")
const BLUE := Color("#4b9dff")
const CYAN := Color("#3fe7ff")
const AMBER := Color("#ffc45d")

var camera: Camera3D
var yaw := 0.72
var pitch := -0.78
var distance := 14.2
var dragging := false
var last_mouse := Vector2.ZERO
var active_touch := -1
var robots: Array = []
var live_targets: Dictionary = {}
var event_label: Label

func _ready() -> void:
	_setup_environment()
	_build_arena()
	_build_camera()
	_spawn_robots()
	_build_hud()

func _setup_environment() -> void:
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color("#050913")
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color("#496a9a")
	environment.ambient_light_energy = 0.64
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	environment.glow_enabled = true
	environment.glow_intensity = 0.9
	environment.glow_bloom = 0.18
	environment.glow_blend_mode = Environment.GLOW_BLEND_MODE_SCREEN
	world.environment = environment
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-52, -28, 0)
	sun.light_color = Color("#a7c7ff")
	sun.light_energy = 1.35
	sun.shadow_enabled = true
	add_child(sun)
	var fill := OmniLight3D.new()
	fill.position = Vector3(0, 3.2, 0)
	fill.light_color = CYAN
	fill.light_energy = 2.2
	fill.omni_range = 11.0
	add_child(fill)

func _build_arena() -> void:
	_box(Vector3(FIELD.x, 0.22, FIELD.y), Vector3(0, -0.12, 0), _mat(Color("#0b1425"), 0.6, 0.28))
	var grid := _mat(Color("#213653"), 0.45, 0.32)
	for x in range(-6, 7):
		_box(Vector3(0.018, 0.012, FIELD.y - 0.2), Vector3(x, 0.005, 0), grid)
	for z in range(-4, 5):
		_box(Vector3(FIELD.x - 0.2, 0.012, 0.018), Vector3(0, 0.006, z), grid)
	var rail := _mat(Color("#4f6f97"), 0.82, 0.21, true)
	_box(Vector3(FIELD.x + 0.2, 0.22, 0.09), Vector3(0, 0.18, -FIELD.y / 2.0), rail)
	_box(Vector3(FIELD.x + 0.2, 0.22, 0.09), Vector3(0, 0.18, FIELD.y / 2.0), rail)
	_box(Vector3(0.09, 0.22, FIELD.y + 0.2), Vector3(-FIELD.x / 2.0, 0.18, 0), rail)
	_box(Vector3(0.09, 0.22, FIELD.y + 0.2), Vector3(FIELD.x / 2.0, 0.18, 0), rail)
	_build_base("RED BASE", Vector3(-5.0, 0, 0), RED)
	_build_base("BLUE BASE", Vector3(5.0, 0, 0), BLUE)
	_build_outpost(Vector3(-3.2, 0, -2.2), RED, "OUTPOST A")
	_build_outpost(Vector3(3.2, 0, 2.2), BLUE, "OUTPOST B")
	_build_energy_mechanism()
	_box(Vector3(8.8, 0.035, 0.9), Vector3(0, 0.025, 0), _mat(Color("#172944"), 0.55, 0.3))
	for x in [-4.0, -2.0, 0.0, 2.0, 4.0]:
		_box(Vector3(0.82, 0.045, 0.06), Vector3(x, 0.055, 0), _mat(CYAN if x == 0 else Color("#2c4768"), 0.7, 0.25, true))

func _build_base(label_text: String, at: Vector3, color: Color) -> void:
	_box(Vector3(1.7, 0.2, 2.2), at + Vector3(0, 0.1, 0), _mat(color.darkened(0.52), 0.7, 0.27))
	_box(Vector3(1.42, 0.18, 1.9), at + Vector3(0, 0.3, 0), _mat(color.darkened(0.25), 0.78, 0.22, true))
	_cylinder(0.48, 1.4, at + Vector3(0, 1.02, 0), _mat(Color("#14233d"), 0.9, 0.2))
	_box(Vector3(1.0, 0.10, 0.10), at + Vector3(0, 1.72, 0), _mat(color, 0.72, 0.18, true))
	_label(label_text, at + Vector3(0, 2.15, 0), color)

func _build_outpost(at: Vector3, color: Color, label_text: String) -> void:
	_cylinder(0.65, 0.22, at + Vector3(0, 0.12, 0), _mat(color.darkened(0.52), 0.65, 0.32))
	_cylinder(0.36, 1.2, at + Vector3(0, 0.78, 0), _mat(Color("#162844"), 0.86, 0.22))
	var glow := _mat(color, 0.72, 0.22, true)
	_cylinder(0.48, 0.08, at + Vector3(0, 1.42, 0), glow)
	for angle in [0.0, 1.57, 3.14, 4.71]:
		var arm := _box(Vector3(0.78, 0.08, 0.08), at + Vector3(cos(angle) * 0.34, 1.46, sin(angle) * 0.34), glow)
		arm.rotation.y = angle
	_label(label_text, at + Vector3(0, 1.9, 0), color)

func _build_energy_mechanism() -> void:
	_box(Vector3(0.24, 2.0, 1.8), Vector3(0, 1.0, 0), _mat(Color("#1b304e"), 0.85, 0.25))
	for z in [-0.66, 0.0, 0.66]:
		var ring := MeshInstance3D.new()
		var torus := TorusMesh.new()
		torus.inner_radius = 0.34
		torus.outer_radius = 0.40
		torus.rings = 32
		torus.ring_segments = 8
		ring.mesh = torus
		ring.position = Vector3(0.16, 0.92, z)
		ring.rotation.y = PI / 2.0
		ring.material_override = _mat(AMBER if z == 0 else CYAN, 0.65, 0.17, true)
		add_child(ring)
	_label("ENERGY CORE", Vector3(0, 2.25, 0), AMBER)

func _spawn_robots() -> void:
	var specs := [
		["red", "hero", "RED-01", 100.0, Vector3(-2.8, 0, -1.1), 0.0],
		["red", "infantry", "RED-02", 82.0, Vector3(-1.6, 0, 1.2), 1.2],
		["blue", "hero", "BLUE-01", 94.0, Vector3(2.8, 0, 1.1), 2.2],
		["blue", "sentry", "BLUE-S", 128.0, Vector3(1.7, 0, -1.35), 3.2]
	]
	for spec in specs:
		var robot = RobotVisualScript.new()
		robot.configure(spec[0], spec[1], spec[2], spec[3], spec[5])
		robot.position = spec[4]
		add_child(robot)
		robots.append(robot)

func _build_camera() -> void:
	camera = get_node_or_null("Camera3D") as Camera3D
	if camera == null:
		camera = Camera3D.new()
		add_child(camera)
	camera.current = true
	camera.fov = 41.0
	_update_camera()

func _update_camera() -> void:
	if not camera:
		return
	var horizontal := cos(pitch) * distance
	camera.position = Vector3(sin(yaw) * horizontal, -sin(pitch) * distance, cos(yaw) * horizontal)
	camera.look_at(Vector3(0, 0.5, 0), Vector3.UP)

func _process(delta: float) -> void:
	var anchors := [Vector3(-2.8, 0, -1.1), Vector3(-1.6, 0, 1.2), Vector3(2.8, 0, 1.1), Vector3(1.7, 0, -1.35)]
	for i in robots.size():
		var target: Vector3 = anchors[i] + Vector3(sin(Time.get_ticks_msec() * 0.00035 + i) * 0.32, 0, cos(Time.get_ticks_msec() * 0.00028 + i) * 0.18)
		if live_targets.has(i):
			target = live_targets[i]
		robots[i].position = robots[i].position.lerp(target, delta * 1.8)
	if event_label:
		var cycle := int(Time.get_ticks_msec() / 1000.0) % 4
		event_label.text = ["TACTICAL LINK  /  SIMULATION READY", "ENERGY CORE CHARGE  72%", "BLUE-01 ENTERED OUTPOST B", "BALLISTIC CHANNEL STABLE"][cycle]

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			dragging = event.pressed
			last_mouse = event.position
		elif event.button_index == MOUSE_BUTTON_WHEEL_UP and event.pressed:
			distance = max(9.5, distance - 0.7)
			_update_camera()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
			distance = min(21.0, distance + 0.7)
			_update_camera()
	elif event is InputEventMouseMotion and dragging:
		var motion: Vector2 = event.position - last_mouse
		last_mouse = event.position
		yaw -= motion.x * 0.006
		pitch = clampf(pitch - motion.y * 0.004, -1.25, -0.45)
		_update_camera()
	elif event is InputEventScreenTouch:
		dragging = event.pressed
		active_touch = event.index if event.pressed else -1
		last_mouse = event.position
	elif event is InputEventScreenDrag and event.index == active_touch:
		yaw -= event.relative.x * 0.006
		pitch = clampf(pitch - event.relative.y * 0.004, -1.25, -0.45)
		_update_camera()
	elif event is InputEventMagnifyGesture:
		distance = clampf(distance / maxf(event.factor, 0.1), 9.5, 21.0)
		_update_camera()
	elif event is InputEventKey and event.pressed and event.keycode == KEY_R:
		yaw = 0.72
		pitch = -0.78
		distance = 14.2
		_update_camera()

func _build_hud() -> void:
	var canvas := get_node_or_null("HUD") as CanvasLayer
	if canvas == null:
		canvas = CanvasLayer.new()
		add_child(canvas)
	canvas.layer = 10
	var root := Control.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	canvas.add_child(root)
	var viewport_size := get_viewport().get_visible_rect().size
	var compact := viewport_size.x < 980.0
	var top := Panel.new()
	top.position = Vector2(28, 24)
	top.size = Vector2(maxf(680.0, viewport_size.x - 56.0), 82)
	top.add_theme_stylebox_override("panel", _panel(Color(0.035, 0.065, 0.12, 0.94), Color("#244267")))
	root.add_child(top)
	_text(top, Vector2(24, 12), "RMUC TACTICS LAB", 24, Color("#e8f4ff"))
	_text(top, Vector2(25, 47), "OFFICIAL FIELD REPLICA  /  MATCH SIMULATOR", 12, Color("#7fa5cc"))
	_score_card(top, Vector2(465, 11), "RED  /  02", RED)
	_score_card(top, Vector2(660, 11), "BLUE  /  01", BLUE)
	_text(top, Vector2(1140, 18), "06:42", 32, Color("#ffc96b"))
	_text(top, Vector2(1145, 53), "LIVE  •  TICK 024812", 11, Color("#5ee7cf"))

	var side := Panel.new()
	side.position = Vector2(maxf(28.0, viewport_size.x - 334.0), 132)
	side.size = Vector2(306, 500)
	side.visible = not compact
	side.add_theme_stylebox_override("panel", _panel(Color(0.028, 0.055, 0.105, 0.92), Color("#213c5d")))
	root.add_child(side)
	_text(side, Vector2(22, 20), "ROBOT INSPECTOR", 15, Color("#a9c8e9"))
	_text(side, Vector2(22, 58), "RED-01  /  HERO", 24, RED)
	_text(side, Vector2(22, 112), "HP       100 / 100\nAMMO       148\nHEAT        28%\nPOWER      82%\nPOSITION   A-03\nMODE       ASSAULT", 15, Color("#d8e8fa"))
	var divider := ColorRect.new()
	divider.position = Vector2(22, 284)
	divider.size = Vector2(262, 1)
	divider.color = Color("#29496c")
	side.add_child(divider)
	_text(side, Vector2(22, 304), "EVENT STREAM\n\n09:12  OUTPOST B  CONTROLLED\n09:09  ENERGY CORE  CHARGE\n09:07  BLUE-01  FIRING\n09:03  RED-02  PATH LOCKED", 12, Color("#84a8cd"))

	var bottom := Panel.new()
	bottom.position = Vector2(28, maxf(480.0, viewport_size.y - 142.0))
	bottom.size = Vector2(maxf(680.0, viewport_size.x - 412.0), 112)
	bottom.add_theme_stylebox_override("panel", _panel(Color(0.035, 0.065, 0.12, 0.94), Color("#244267")))
	root.add_child(bottom)
	_text(bottom, Vector2(24, 18), "MATCH CONTROL", 14, Color("#abc6e4"))
	_button(bottom, Vector2(24, 54), "PAUSE", Color("#385d83"), 118)
	_button(bottom, Vector2(142, 54), "STEP TICK", Color("#385d83"), 118)
	_button(bottom, Vector2(284, 54), "RESET", Color("#5b354b"), 118)
	_button(bottom, Vector2(422, 54), "EXPORT REPLAY", Color("#275f69"), 164)
	event_label = Label.new()
	event_label.position = Vector2(610, 58)
	event_label.text = "TACTICAL LINK  /  SIMULATION READY"
	event_label.add_theme_font_size_override("font_size", 13)
	event_label.add_theme_color_override("font_color", Color("#61e4d0"))
	bottom.add_child(event_label)

func _score_card(parent: Control, at: Vector2, text_value: String, color: Color) -> void:
	var card := Panel.new()
	card.position = at
	card.size = Vector2(164, 58)
	card.add_theme_stylebox_override("panel", _panel(Color(color.r, color.g, color.b, 0.12), color.darkened(0.25)))
	parent.add_child(card)
	_text(card, Vector2(17, 15), text_value, 17, color.lightened(0.18))

func _button(parent: Control, at: Vector2, text_value: String, color: Color, width: float) -> void:
	var button := Button.new()
	button.position = at
	button.size = Vector2(width, 38)
	button.text = text_value
	button.add_theme_font_size_override("font_size", 12)
	button.add_theme_color_override("font_color", Color("#e6f1ff"))
	button.add_theme_stylebox_override("normal", _panel(color, color.lightened(0.12)))
	button.add_theme_stylebox_override("hover", _panel(color.lightened(0.1), Color("#7ce8da")))
	parent.add_child(button)

func _text(parent: Control, at: Vector2, value: String, size: int, color: Color) -> void:
	var label := Label.new()
	label.position = at
	label.text = value
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", color)
	parent.add_child(label)

func _label(value: String, at: Vector3, color: Color) -> void:
	var label := Label3D.new()
	label.text = value
	label.position = at
	label.font_size = 24
	label.outline_size = 6
	label.modulate = color.lightened(0.15)
	label.outline_modulate = Color(0.01, 0.02, 0.05, 0.92)
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.no_depth_test = true
	add_child(label)

func _box(size: Vector3, at: Vector3, material: Material) -> MeshInstance3D:
	var item := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	item.mesh = mesh
	item.position = at
	item.material_override = material
	add_child(item)
	return item

func _cylinder(radius: float, height: float, at: Vector3, material: Material) -> MeshInstance3D:
	var item := MeshInstance3D.new()
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius
	mesh.height = height
	mesh.radial_segments = 24
	item.mesh = mesh
	item.position = at
	item.material_override = material
	add_child(item)
	return item

func _mat(color: Color, metallic: float, roughness: float, glow := false) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.metallic = metallic
	material.roughness = roughness
	if glow:
		material.emission_enabled = true
		material.emission = color
		material.emission_energy_multiplier = 1.6
	return material

func _panel(fill: Color, border: Color) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = fill
	box.border_color = border
	box.set_border_width_all(1)
	box.corner_radius_top_left = 6
	box.corner_radius_top_right = 6
	box.corner_radius_bottom_left = 6
	box.corner_radius_bottom_right = 6
	return box
