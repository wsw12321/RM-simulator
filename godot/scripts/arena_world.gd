extends Node3D
const FIELD_SIZE := Vector2(22.0, 14.0)
const FLOOR_Y := 0.0
const ZONE_RADIUS := 2.25

var zones: Array[Area3D] = []
var arena_material: StandardMaterial3D

func _ready() -> void:
    _build_floor()
    _build_walls()
    _build_cover()
    _build_zones()

func _build_floor() -> void:
    var floor_body := StaticBody3D.new()
    floor_body.name = "Floor"
    floor_body.collision_layer = 1
    floor_body.collision_mask = 0
    var shape := CollisionShape3D.new()
    var box := BoxShape3D.new()
    box.size = Vector3(FIELD_SIZE.x, 0.35, FIELD_SIZE.y)
    shape.shape = box
    shape.position.y = -0.18
    floor_body.add_child(shape)
    add_child(floor_body)
    var mesh := MeshInstance3D.new()
    var plane := BoxMesh.new()
    plane.size = Vector3(FIELD_SIZE.x, 0.35, FIELD_SIZE.y)
    mesh.mesh = plane
    mesh.position.y = -0.18
    arena_material = StandardMaterial3D.new()
    arena_material.albedo_color = Color("#0d1c2b")
    arena_material.metallic = 0.72
    arena_material.roughness = 0.3
    mesh.material_override = arena_material
    add_child(mesh)
    _add_grid_lines()

func _add_grid_lines() -> void:
    for x in range(-11, 12):
        var line := MeshInstance3D.new()
        var mesh := BoxMesh.new()
        mesh.size = Vector3(0.012, 0.012, FIELD_SIZE.y - 0.4)
        line.mesh = mesh
        line.position = Vector3(x, 0.015, 0)
        line.material_override = _mat(Color(0.12, 0.3, 0.38, 0.35), 0.0, 0.8)
        add_child(line)
    for z in range(-7, 8):
        var line := MeshInstance3D.new()
        var mesh := BoxMesh.new()
        mesh.size = Vector3(FIELD_SIZE.x - 0.4, 0.012, 0.012)
        line.mesh = mesh
        line.position = Vector3(0, 0.016, z)
        line.material_override = _mat(Color(0.12, 0.3, 0.38, 0.35), 0.0, 0.8)
        add_child(line)

func _build_walls() -> void:
    _add_wall(Vector3(0, 0.8, -FIELD_SIZE.y * 0.5), Vector3(FIELD_SIZE.x, 1.6, 0.35))
    _add_wall(Vector3(0, 0.8, FIELD_SIZE.y * 0.5), Vector3(FIELD_SIZE.x, 1.6, 0.35))
    _add_wall(Vector3(-FIELD_SIZE.x * 0.5, 0.8, 0), Vector3(0.35, 1.6, FIELD_SIZE.y))
    _add_wall(Vector3(FIELD_SIZE.x * 0.5, 0.8, 0), Vector3(0.35, 1.6, FIELD_SIZE.y))

func _add_wall(pos: Vector3, size: Vector3) -> void:
    var body := StaticBody3D.new()
    body.collision_layer = 1
    var shape := CollisionShape3D.new()
    var box := BoxShape3D.new()
    box.size = size
    shape.shape = box
    body.add_child(shape)
    body.position = pos
    add_child(body)
    var mesh := MeshInstance3D.new()
    var cube := BoxMesh.new()
    cube.size = size
    mesh.mesh = cube
    mesh.position = pos
    mesh.material_override = _mat(Color("#1b3242"), 0.55, 0.28)
    add_child(mesh)

func _build_cover() -> void:
    for data in [
        [Vector3(-3.5, 0.55, -1.5), Vector3(3.0, 1.1, 0.65)],
        [Vector3(3.4, 0.55, 1.8), Vector3(2.8, 1.1, 0.65)],
        [Vector3(0.0, 0.35, 3.6), Vector3(2.0, 0.7, 0.55)]
    ]:
        _add_wall(data[0], data[1])

func _build_zones() -> void:
    _add_zone("RED_BASE", Vector3(-8.5, 0.08, 0), Color("#ff5367"))
    _add_zone("BLUE_BASE", Vector3(8.5, 0.08, 0), Color("#4ca7ff"))

func _add_zone(id: String, pos: Vector3, tint: Color) -> void:
    var area := Area3D.new()
    area.name = id
    area.collision_layer = 8
    area.collision_mask = 2
    var shape := CollisionShape3D.new()
    var sphere := SphereShape3D.new()
    sphere.radius = ZONE_RADIUS
    shape.shape = sphere
    area.add_child(shape)
    area.position = pos
    add_child(area)
    var ring := MeshInstance3D.new()
    var torus := TorusMesh.new()
    torus.inner_radius = ZONE_RADIUS - 0.12
    torus.outer_radius = ZONE_RADIUS
    torus.rings = 32
    torus.ring_segments = 8
    ring.mesh = torus
    ring.position = pos
    ring.rotation_degrees.x = 90
    ring.material_override = _mat(tint, 0.25, 0.22, true)
    add_child(ring)
    var beacon := MeshInstance3D.new()
    var cyl := CylinderMesh.new()
    cyl.top_radius = 0.22
    cyl.bottom_radius = 0.38
    cyl.height = 1.25
    beacon.mesh = cyl
    beacon.position = pos + Vector3.UP * 0.62
    beacon.material_override = _mat(tint, 0.35, 0.2, true)
    add_child(beacon)
    zones.append(area)

func _mat(color: Color, metallic: float, roughness: float, emission := false) -> StandardMaterial3D:
    var mat := StandardMaterial3D.new()
    mat.albedo_color = color
    mat.metallic = metallic
    mat.roughness = roughness
    if emission:
        mat.emission_enabled = true
        mat.emission = color
        mat.emission_energy_multiplier = 2.2
    return mat

func is_inside_zone(position: Vector3, team: int) -> bool:
    var target := zones[team].global_position
    return Vector2(position.x - target.x, position.z - target.z).length() <= ZONE_RADIUS
