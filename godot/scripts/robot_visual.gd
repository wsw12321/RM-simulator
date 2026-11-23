class_name RobotVisual
extends Node3D

var team := "red"
var role := "infantry"
var robot_name := "R-01"
var health := 100.0
var max_health := 100.0
var phase := 0.0
var accent := Color.WHITE
var turret: Node3D
var ring: MeshInstance3D
var label: Label3D

func configure(team_name: String, role_name: String, display_name: String, hp: float, animation_phase: float) -> void:
	team = team_name
	role = role_name
	robot_name = display_name
	health = hp
	max_health = hp
	phase = animation_phase
	accent = Color("#ff4d65") if team == "red" else Color("#50a7ff")

func _ready() -> void:
	_build()

func _build() -> void:
	var dark := _material(Color("#101b2d"), 0.88, 0.23)
	var accent_material := _material(accent, 0.72, 0.2, true)
	var bright := _material(accent.lightened(0.26), 0.65, 0.16, true)
	var black := _material(Color("#050914"), 0.92, 0.3)
	_box(Vector3(1.25, 0.32, 0.84), Vector3(0, 0.28, 0), dark)
	_box(Vector3(1.08, 0.10, 0.66), Vector3(0, 0.48, 0), accent_material)
	_box(Vector3(0.16, 0.30, 0.98), Vector3(-0.55, 0.36, 0), accent_material)
	_box(Vector3(0.16, 0.30, 0.98), Vector3(0.55, 0.36, 0), accent_material)
	for x in [-0.42, 0.42]:
		for z in [-0.45, 0.45]:
			_box(Vector3(0.20, 0.40, 0.18), Vector3(x, 0.12, z), black)

	turret = Node3D.new()
	turret.name = "Turret"
	add_child(turret)
	var cap := MeshInstance3D.new()
	var cap_mesh := CylinderMesh.new()
	cap_mesh.top_radius = 0.34 if role == "sentry" else 0.28
	cap_mesh.bottom_radius = cap_mesh.top_radius
	cap_mesh.height = 0.18
	cap_mesh.radial_segments = 16
	cap.mesh = cap_mesh
	cap.position.y = 0.69
	cap.material_override = bright
	turret.add_child(cap)
	_box(Vector3(0.15, 0.13, 0.60), Vector3(0, 0.78, -0.26), black, turret)
	_box(Vector3(0.08, 0.08, 0.84), Vector3(0, 0.79, -0.68), accent_material, turret)

	var antenna := MeshInstance3D.new()
	var antenna_mesh := CylinderMesh.new()
	antenna_mesh.top_radius = 0.018
	antenna_mesh.bottom_radius = 0.025
	antenna_mesh.height = 0.5
	antenna_mesh.radial_segments = 8
	antenna.mesh = antenna_mesh
	antenna.position = Vector3(0, 1.05, 0.15)
	antenna.material_override = bright
	add_child(antenna)
	var beacon := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = 0.07
	sphere.height = 0.14
	beacon.mesh = sphere
	beacon.position = Vector3(0, 1.34, 0.15)
	beacon.material_override = bright
	add_child(beacon)

	ring = MeshInstance3D.new()
	var torus := TorusMesh.new()
	torus.inner_radius = 0.75
	torus.outer_radius = 0.79
	torus.rings = 32
	torus.ring_segments = 8
	ring.mesh = torus
	ring.rotation.x = -PI / 2.0
	ring.position.y = 0.035
	ring.material_override = _material(accent, 0.2, 0.1, true)
	add_child(ring)

	label = Label3D.new()
	label.text = "%s  %d%%" % [robot_name, int(health)]
	label.font_size = 28
	label.outline_size = 7
	label.modulate = Color("#f5f9ff")
	label.outline_modulate = Color(0.02, 0.03, 0.08, 0.94)
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.position = Vector3(0, 1.58, 0)
	label.no_depth_test = true
	add_child(label)

func _process(delta: float) -> void:
	position.y = sin(Time.get_ticks_msec() * 0.002 + phase) * 0.018
	if turret:
		turret.rotation.y += delta * (0.32 if role == "sentry" else 0.16)
	if ring:
		var pulse := 0.88 + (sin(Time.get_ticks_msec() * 0.003 + phase) + 1.0) * 0.07
		ring.scale = Vector3.ONE * pulse
	if label:
		label.text = "%s  %d%%" % [robot_name, int(health)]

func set_health(value: float) -> void:
	health = clampf(value, 0.0, max_health)

func _material(color: Color, metallic: float, roughness: float, glow := false) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.metallic = metallic
	material.roughness = roughness
	if glow:
		material.emission_enabled = true
		material.emission = color
		material.emission_energy_multiplier = 1.8
	return material

func _box(size: Vector3, at: Vector3, material: Material, parent: Node3D = self) -> MeshInstance3D:
	var item := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	item.mesh = mesh
	item.position = at
	item.material_override = material
	parent.add_child(item)
	return item
