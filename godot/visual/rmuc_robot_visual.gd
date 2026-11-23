class_name RMUCRobotVisual
extends Node3D

const Materials = preload("res://visual/rmuc_materials.gd")

@export var team := "red"
@export var unit_role := "INFANTRY"
@export var callsign := "R-01"

var team_color := Materials.RED
var body_root: Node3D
var turret_root: Node3D
var signal_core: MeshInstance3D
var muzzle: MeshInstance3D
var health_bar: Label3D
var hit_flash := 0.0
var motion_time := 0.0

func _ready() -> void:
	set_team(team)
	build_robot()

func set_team(value: String) -> void:
	team = value.to_lower()
	team_color = Materials.BLUE if team == "blue" else Materials.RED

func build_robot() -> void:
	for child in get_children():
		child.queue_free()
	body_root = Node3D.new()
	body_root.name = "Chassis"
	add_child(body_root)
	turret_root = Node3D.new()
	turret_root.name = "Gimbal"
	add_child(turret_root)
	var body_material := Materials.metal(Color("#263754"), 0.28)
	var dark_material := Materials.metal(Color("#111a2b"), 0.42)
	var accent_material := Materials.metal(team_color, 0.24, 2.0)
	var glass_material := Materials.glass(team_color, 0.62)
	body_root.add_child(box("LowerHull", Vector3(1.38, 0.34, 1.58), Vector3(0.0, 0.34, 0.0), body_material))
	body_root.add_child(box("UpperHull", Vector3(1.1, 0.34, 1.18), Vector3(0.0, 0.66, 0.0), dark_material))
	for x in [-0.58, 0.58]:
		for z in [-0.54, 0.54]:
			var wheel := cylinder("Wheel", 0.24, 0.14, Vector3(x, 0.27, z), dark_material)
			wheel.rotation_degrees = Vector3(90.0, 0.0, 0.0)
			body_root.add_child(wheel)
	for side in [-1, 1]:
		var plate := box("ArmorPlate", Vector3(0.08, 0.33, 1.02), Vector3(float(side) * 0.72, 0.62, 0.0), accent_material)
		body_root.add_child(plate)
	var mast := cylinder("TurretRing", 0.44, 0.09, Vector3(0.0, 0.92, 0.0), accent_material)
	turret_root.add_child(mast)
	turret_root.add_child(box("Turret", Vector3(0.66, 0.34, 0.78), Vector3(0.0, 1.13, 0.0), body_material))
	turret_root.add_child(box("TurretRoof", Vector3(0.48, 0.06, 0.58), Vector3(0.0, 1.34, 0.0), accent_material))
	var barrel := cylinder("Barrel", 0.065, 0.82, Vector3(0.0, 1.14, -0.5), dark_material)
	barrel.rotation_degrees = Vector3(90.0, 0.0, 0.0)
	turret_root.add_child(barrel)
	muzzle = cylinder("Muzzle", 0.11, 0.1, Vector3(0.0, 1.14, -0.9), glass_material)
	muzzle.rotation_degrees = Vector3(90.0, 0.0, 0.0)
	turret_root.add_child(muzzle)
	signal_core = cylinder("SignalCore", 0.14, 0.12, Vector3(0.0, 1.53, 0.0), glass_material)
	turret_root.add_child(signal_core)
	var team_light := OmniLight3D.new()
	team_light.name = "TeamLight"
	team_light.position = Vector3(0.0, 1.25, 0.0)
	team_light.light_color = team_color
	team_light.light_energy = 1.8
	team_light.omni_range = 3.2
	turret_root.add_child(team_light)
	var name_label := Label3D.new()
	name_label.name = "Callsign"
	name_label.text = callsign + "  /  " + unit_role
	name_label.position = Vector3(0.0, 2.3, 0.0)
	name_label.modulate = team_color
	name_label.font_size = 28
	name_label.outline_size = 7
	name_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	add_child(name_label)
	health_bar = Label3D.new()
	health_bar.name = "HealthBar"
	health_bar.text = "HP  ████████"
	health_bar.position = Vector3(0.0, 2.0, 0.0)
	health_bar.modulate = Color("#9bffcf")
	health_bar.font_size = 22
	health_bar.outline_size = 5
	health_bar.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	add_child(health_bar)

func _process(delta: float) -> void:
	motion_time += delta
	if signal_core:
		signal_core.scale = Vector3.ONE * (1.0 + sin(motion_time * 4.0) * 0.12)
	if muzzle:
		muzzle.scale = Vector3.ONE * (1.0 + sin(motion_time * 7.0) * 0.05)
	if hit_flash > 0.0:
		hit_flash = maxf(0.0, hit_flash - delta)
		body_root.scale = Vector3.ONE * (1.0 + hit_flash * 2.0)
	else:
		body_root.scale = Vector3.ONE

func flash_hit() -> void:
	hit_flash = 0.22

func set_health(current: int, maximum: int) -> void:
	if health_bar == null:
		return
	var ratio := clampf(float(current) / maxf(float(maximum), 1.0), 0.0, 1.0)
	var segments := int(round(ratio * 8.0))
	health_bar.text = "HP  " + "█".repeat(segments) + "·".repeat(8 - segments)
	health_bar.modulate = Color("#9bffcf") if ratio > 0.35 else Color("#ffbf82")

func box(node_name: String, size: Vector3, position: Vector3, material: Material) -> MeshInstance3D:
	var mesh := BoxMesh.new()
	mesh.size = size
	var instance := MeshInstance3D.new()
	instance.name = node_name
	instance.mesh = mesh
	instance.position = position
	instance.material_override = material
	return instance

func cylinder(node_name: String, radius: float, height: float, position: Vector3, material: Material) -> MeshInstance3D:
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius
	mesh.height = height
	mesh.radial_segments = 16
	var instance := MeshInstance3D.new()
	instance.name = node_name
	instance.mesh = mesh
	instance.position = position
	instance.material_override = material
	return instance
