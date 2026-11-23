class_name RMUCVisualStage
extends Node3D

const Materials = preload("res://visual/rmuc_materials.gd")

const FIELD_SIZE := Vector2(28.0, 15.0)
const TILE := 1.0

var floor_root: Node3D
var accent_root: Node3D

func _ready() -> void:
	build_stage()

func build_stage() -> void:
	for child in get_children():
		child.queue_free()
	floor_root = Node3D.new()
	floor_root.name = "ArenaGeometry"
	add_child(floor_root)
	accent_root = Node3D.new()
	accent_root.name = "SignalAccents"
	add_child(accent_root)
	build_lighting()
	build_floor()
	build_boundaries()
	build_lanes()
	build_highlands()
	build_objectives()
	build_spawn_bays()

func build_lighting() -> void:
	var environment_node := WorldEnvironment.new()
	environment_node.name = "Atmosphere"
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color("#070b16")
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color("#6b82b0")
	environment.ambient_light_energy = 0.72
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	environment.glow_enabled = true
	environment.glow_intensity = 0.72
	environment.glow_bloom = 0.18
	environment_node.environment = environment
	add_child(environment_node)
	var key := DirectionalLight3D.new()
	key.name = "KeyLight"
	key.rotation_degrees = Vector3(-54.0, -28.0, 0.0)
	key.light_color = Color("#d7e5ff")
	key.light_energy = 1.25
	key.shadow_enabled = true
	add_child(key)
	var fill := OmniLight3D.new()
	fill.name = "CoreFill"
	fill.position = Vector3(0.0, 3.5, 0.0)
	fill.light_color = Materials.CYAN
	fill.light_energy = 4.0
	fill.omni_range = 15.0
	add_child(fill)

func build_floor() -> void:
	var slab := box("ArenaSlab", Vector3(FIELD_SIZE.x, 0.25, FIELD_SIZE.y), Vector3(0.0, -0.16, 0.0), Materials.metal(Color("#101a2a"), 0.5))
	floor_root.add_child(slab)
	for x in range(-14, 15):
		for z in range(-7, 8):
			var tile_color := Color("#16233a") if (x + z) % 2 == 0 else Color("#132036")
			var tile := box("DeckTile", Vector3(0.94, 0.045, 0.94), Vector3(float(x), 0.0, float(z)), Materials.metal(tile_color, 0.78))
			floor_root.add_child(tile)
	for x in range(-13, 14, 2):
		var stripe := box("LaneStripe", Vector3(0.045, 0.012, 14.2), Vector3(float(x), 0.045, 0.0), Materials.line(Color(0.24, 0.53, 0.72, 0.22)))
		floor_root.add_child(stripe)
	for z in range(-6, 7, 2):
		var stripe := box("LaneStripe", Vector3(27.2, 0.012, 0.045), Vector3(0.0, 0.046, float(z)), Materials.line(Color(0.24, 0.53, 0.72, 0.16)))
		floor_root.add_child(stripe)

func build_boundaries() -> void:
	for edge in [-1, 1]:
		var x: float = float(edge) * 14.0
		var wall := box("Boundary", Vector3(0.32, 1.1, 15.0), Vector3(x, 0.5, 0.0), Materials.metal(Color("#263552"), 0.4))
		floor_root.add_child(wall)
	for edge in [-1, 1]:
		var z: float = float(edge) * 7.5
		var wall := box("Boundary", Vector3(28.0, 1.1, 0.32), Vector3(0.0, 0.5, z), Materials.metal(Color("#263552"), 0.4))
		floor_root.add_child(wall)

func build_lanes() -> void:
	for side in [-1, 1]:
		var team_color := Materials.RED if side < 0 else Materials.BLUE
		var lane_x := float(side) * 10.5
		var rail := box("TeamRail", Vector3(0.12, 0.22, 10.8), Vector3(lane_x, 0.16, 0.0), Materials.line(team_color, 0.05))
		accent_root.add_child(rail)
		for z in [-5.0, -2.5, 0.0, 2.5, 5.0]:
			var node := cylinder("SignalNode", 0.14, 0.08, Vector3(lane_x, 0.26, z), Materials.metal(team_color, 0.22, 2.4))
			accent_root.add_child(node)

func build_highlands() -> void:
	var north := box("NorthHighland", Vector3(7.0, 0.72, 3.2), Vector3(0.0, 0.36, -5.2), Materials.metal(Color("#1d2e4e"), 0.46))
	floor_root.add_child(north)
	var south := box("SouthHighland", Vector3(7.0, 0.72, 3.2), Vector3(0.0, 0.36, 5.2), Materials.metal(Color("#1d2e4e"), 0.46))
	floor_root.add_child(south)
	for z in [-5.2, 5.2]:
		var trim := box("HighlandTrim", Vector3(7.0, 0.05, 0.09), Vector3(0.0, 0.76, z), Materials.line(Materials.AMBER))
		accent_root.add_child(trim)
	var bridge := box("CentralBridge", Vector3(4.8, 0.42, 1.5), Vector3(0.0, 0.22, 0.0), Materials.metal(Color("#293c60"), 0.32))
	floor_root.add_child(bridge)
	var bridge_glow := box("BridgeGlow", Vector3(4.5, 0.035, 0.12), Vector3(0.0, 0.46, 0.0), Materials.line(Materials.CYAN))
	accent_root.add_child(bridge_glow)

func build_objectives() -> void:
	for side in [-1, 1]:
		var team_color := Materials.RED if side < 0 else Materials.BLUE
		var base_x := float(side) * 11.8
		var base := box("BaseObjective", Vector3(1.6, 1.0, 3.2), Vector3(base_x, 0.5, 0.0), Materials.metal(Color("#27334b"), 0.3))
		floor_root.add_child(base)
		var core := cylinder("BaseCore", 0.42, 1.0, Vector3(base_x - float(side) * 0.9, 1.15, 0.0), Materials.glass(team_color, 0.6))
		accent_root.add_child(core)
		var beacon := cylinder("BaseBeacon", 0.08, 2.8, Vector3(base_x, 2.1, 0.0), Materials.metal(team_color, 0.2, 2.5))
		accent_root.add_child(beacon)
		var label := Label3D.new()
		label.text = "BASE  //  " + ("RED" if side < 0 else "BLUE")
		label.position = Vector3(base_x, 3.2, 0.0)
		label.font_size = 32
		label.modulate = team_color
		label.outline_size = 8
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		accent_root.add_child(label)
	for z in [-3.0, 3.0]:
		var outpost := cylinder("Outpost", 0.58, 0.65, Vector3(0.0, 0.34, z), Materials.metal(Color("#314663"), 0.28))
		floor_root.add_child(outpost)
		var ring := torus("OutpostRing", 0.92, 0.05, Vector3(0.0, 0.72, z), Materials.line(Materials.AMBER))
		accent_root.add_child(ring)

func build_spawn_bays() -> void:
	for side in [-1, 1]:
		var team_color := Materials.RED if side < 0 else Materials.BLUE
		for z in [-4.5, 0.0, 4.5]:
			var bay := box("SpawnBay", Vector3(3.5, 0.04, 1.5), Vector3(float(side) * 8.6, 0.08, z), Materials.metal(Color(team_color.r, team_color.g, team_color.b, 0.20), 0.6))
			floor_root.add_child(bay)
			var marker := box("SpawnMarker", Vector3(2.2, 0.025, 0.06), Vector3(float(side) * 8.6, 0.12, z), Materials.line(team_color))
			accent_root.add_child(marker)

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
	mesh.radial_segments = 20
	var instance := MeshInstance3D.new()
	instance.name = node_name
	instance.mesh = mesh
	instance.position = position
	instance.material_override = material
	return instance

func torus(node_name: String, outer_radius: float, inner_radius: float, position: Vector3, material: Material) -> MeshInstance3D:
	var mesh := TorusMesh.new()
	mesh.inner_radius = inner_radius
	mesh.outer_radius = outer_radius
	mesh.rings = 32
	mesh.ring_segments = 12
	var instance := MeshInstance3D.new()
	instance.name = node_name
	instance.mesh = mesh
	instance.position = position
	instance.material_override = material
	return instance
