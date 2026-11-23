class_name RMSimRobotActor3D
extends CharacterBody3D
## Engine actor shell. Integer-mm rule state is supplied by RMSimAdapter.

const MM_TO_M := 0.001
const MAX_SPEED_MPS := 5.5
const TURN_SPEED := 8.0

var robot_id := 0
var team := 0
var hp := 100
var ammo := 120
var accent: Color

func configure(id: int, team_id: int, position_mm: Vector2i) -> void:
	robot_id = id
	team = team_id
	global_position = Vector3(position_mm.x * MM_TO_M, 0.35, position_mm.y * MM_TO_M)
	_ensure_collision_shape()

func apply_motion(input_vector: Vector2, aim_direction: Vector3, delta: float) -> void:
	velocity = Vector3(input_vector.x, 0.0, input_vector.y).limit_length(1.0) * MAX_SPEED_MPS
	move_and_slide()
	if aim_direction.length_squared() > 0.0001:
		rotation.y = lerp_angle(rotation.y, atan2(aim_direction.x, aim_direction.z), minf(1.0, TURN_SPEED * delta))

func sync_snapshot(snapshot: Dictionary) -> void:
	hp = int(snapshot.get("hp", hp))
	ammo = int(snapshot.get("ammo", ammo))

func _ensure_collision_shape() -> void:
	if get_node_or_null("CollisionShape3D") != null:
		return
	var collider := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.28
	capsule.height = 0.7
	collider.shape = capsule
	add_child(collider)

func _ready() -> void:
	collision_layer = 2
	collision_mask = 1 | 2
	accent = Color("f0546c") if team == 0 else Color("39b8f2")
	_build_visual()

func _build_visual() -> void:
	var chassis := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = Vector3(1.15, 0.42, 0.86)
	chassis.mesh = mesh
	chassis.position.y = 0.34
	chassis.material_override = _material(Color("111d31"), 0.82)
	add_child(chassis)
	var trim := MeshInstance3D.new()
	var trim_mesh := BoxMesh.new()
	trim_mesh.size = Vector3(0.94, 0.09, 0.68)
	trim.mesh = trim_mesh
	trim.position.y = 0.58
	trim.material_override = _material(accent, 0.52, true)
	add_child(trim)
	var turret := MeshInstance3D.new()
	var turret_mesh := CylinderMesh.new()
	turret_mesh.top_radius = 0.28
	turret_mesh.bottom_radius = 0.34
	turret_mesh.height = 0.23
	turret.mesh = turret_mesh
	turret.position.y = 0.76
	turret.material_override = _material(Color("edf4ff"), 0.48)
	add_child(turret)
	var barrel := MeshInstance3D.new()
	var barrel_mesh := BoxMesh.new()
	barrel_mesh.size = Vector3(0.1, 0.11, 0.72)
	barrel.mesh = barrel_mesh
	barrel.position = Vector3(0, 0.78, -0.38)
	barrel.material_override = _material(accent, 0.35, true)
	add_child(barrel)
	var ring := MeshInstance3D.new()
	var ring_mesh := TorusMesh.new()
	ring_mesh.inner_radius = 0.72
	ring_mesh.outer_radius = 0.77
	ring.mesh = ring_mesh
	ring.rotation.x = -PI / 2.0
	ring.position.y = 0.05
	ring.material_override = _material(accent, 0.1, true)
	add_child(ring)
	var label := Label3D.new()
	label.text = "R-%02d" % (robot_id + 1)
	label.font_size = 28
	label.outline_size = 6
	label.modulate = Color("f1f7ff")
	label.outline_modulate = Color(0.02, 0.03, 0.08, 0.95)
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.no_depth_test = true
	label.position.y = 1.55
	add_child(label)

func _material(color: Color, metallic: float, glow := false) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.metallic = metallic
	material.roughness = 0.28
	if glow:
		material.emission_enabled = true
		material.emission = color
		material.emission_energy_multiplier = 1.25
	return material
