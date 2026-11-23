extends Node3D

const RobotActor = preload("res://scripts/robot_actor_3d.gd")

var robots: Dictionary = {}
var arena: Node3D
var sim: Node
var camera: Camera3D

func setup(state: Node) -> void:
	sim = state
	_build_arena()
	_build_camera()
	_sync_robots()

func _process(delta: float) -> void:
	if sim == null:
		return
	_sync_robots()
	for id in robots:
		var actor: Node3D = robots[id]
		var source: Dictionary = sim.robots[int(id)]
		var target := Vector3(float(source.x) / 1000.0 - 6.0, 0.42, float(source.y) / 1000.0 - 3.8)
		actor.position = actor.position.lerp(target, minf(1.0, delta * 9.0))

func _build_arena() -> void:
	arena = Node3D.new()
	arena.name = "Arena3D"
	add_child(arena)
	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color("07111f")
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color("8fa8c7")
	env.ambient_light_energy = 0.65
	env_node.environment = env
	arena.add_child(env_node)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-55, -30, 0)
	light.light_energy = 1.3
	light.shadow_enabled = true
	arena.add_child(light)
	var floor := MeshInstance3D.new()
	var floor_mesh := BoxMesh.new()
	floor_mesh.size = Vector3(24, 0.28, 15.2)
	floor.mesh = floor_mesh
	floor.position = Vector3(0, -0.14, 0)
	floor.material_override = _material(Color("172c3f"), 0.2, 0.36)
	arena.add_child(floor)
	for x in range(-11, 12):
		_add_line(Vector3(x, 0.02, -7.3), Vector3(x, 0.02, 7.3), Color("25465c"))
	for z in range(-7, 8):
		_add_line(Vector3(-11.5, 0.022, z), Vector3(11.5, 0.022, z), Color("25465c"))
	for item in [{"p":Vector3(-9,0.4,-5.6),"c":Color("ef476f")},{"p":Vector3(9,0.4,5.6),"c":Color("38bdf8")},{"p":Vector3(0,0.25,-2.2),"c":Color("f6c453")},{"p":Vector3(0,0.25,2.2),"c":Color("f6c453")}]:
		_add_objective(item.p, item.c)

func _build_camera() -> void:
	camera = Camera3D.new()
	camera.name = "OrbitCamera"
	camera.current = true
	camera.fov = 48.0
	add_child(camera)
	camera.position = Vector3(0, 13.0, 15.5)
	camera.look_at(Vector3(0, 0, 0), Vector3.UP)

func _sync_robots() -> void:
	if sim == null: return
	for index in sim.robots.size():
		if not robots.has(index):
			var actor := RobotActor.new()
			actor.team = index % 2
			actor.robot_id = index
			arena.add_child(actor)
			robots[index] = actor

func _add_objective(pos: Vector3, color: Color) -> void:
	var ring := MeshInstance3D.new()
	var torus := TorusMesh.new()
	torus.inner_radius = 0.9
	torus.outer_radius = 0.98
	torus.rings = 32
	ring.mesh = torus
	ring.rotation.x = -PI / 2.0
	ring.position = pos
	ring.material_override = _material(color, 0.1, 0.12, true)
	arena.add_child(ring)
	var beacon := OmniLight3D.new()
	beacon.position = pos + Vector3.UP * 1.0
	beacon.light_color = color
	beacon.light_energy = 2.0
	beacon.omni_range = 3.4
	arena.add_child(beacon)

func _add_line(a: Vector3, b: Vector3, color: Color) -> void:
	var line := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = Vector3(0.018, 0.012, a.distance_to(b))
	line.mesh = mesh
	line.position = (a + b) * 0.5
	line.look_at_from_position((a + b) * 0.5, b, Vector3.UP)
	line.material_override = _material(color, 0.0, 0.3)
	arena.add_child(line)

func _material(color: Color, metallic: float, roughness: float, glow := false) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.metallic = metallic
	material.roughness = roughness
	if glow:
		material.emission_enabled = true
		material.emission = color
		material.emission_energy_multiplier = 1.5
	return material
