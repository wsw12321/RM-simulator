extends CharacterBody3D
class_name RMRobotActor

signal hp_changed(actor)
signal disabled(actor)
signal fired(actor)

var robot_id := 0
var team := 0
var display_name := "R-01"
var max_hp := 100
var hp := 100
var speed := 4.2
var acceleration := 16.0
var move_input := Vector2.ZERO
var aim_point := Vector3.ZERO
var fire_requested := false
var fire_cooldown := 0.0
var cooldown_time := 0.42
var game: Node
var body_mesh: MeshInstance3D
var turret_mesh: MeshInstance3D
var base_material: StandardMaterial3D
var target_marker: MeshInstance3D
var disabled_state := false

func configure(id: int, faction: int, label: String, owner: Node) -> void:
    robot_id = id
    team = faction
    display_name = label
    game = owner
    collision_layer = 2
    collision_mask = 1
    _build_collision()
    _build_visual()

func _build_collision() -> void:
    var shape := CollisionShape3D.new()
    var capsule := CapsuleShape3D.new()
    capsule.radius = 0.55
    capsule.height = 0.9
    shape.shape = capsule
    shape.position.y = 0.55
    add_child(shape)

func _build_visual() -> void:
    base_material = StandardMaterial3D.new()
    base_material.albedo_color = Color("#ff5367") if team == 0 else Color("#4ca7ff")
    base_material.metallic = 0.68
    base_material.roughness = 0.28
    var body := MeshInstance3D.new()
    var capsule := CapsuleMesh.new()
    capsule.radius = 0.72
    capsule.height = 0.82
    body.mesh = capsule
    body.position.y = 0.55
    body.material_override = base_material
    body_mesh = body
    add_child(body)
    var ring := MeshInstance3D.new()
    var torus := TorusMesh.new()
    torus.inner_radius = 0.72
    torus.outer_radius = 0.82
    ring.mesh = torus
    ring.position.y = 0.18
    ring.rotation_degrees.x = 90
    ring.material_override = _emissive_material(base_material.albedo_color, 2.5)
    add_child(ring)
    turret_mesh = MeshInstance3D.new()
    var turret := BoxMesh.new()
    turret.size = Vector3(0.38, 0.22, 0.95)
    turret_mesh.mesh = turret
    turret_mesh.position = Vector3(0, 1.02, -0.3)
    turret_mesh.material_override = _emissive_material(base_material.albedo_color, 1.6)
    add_child(turret_mesh)
    var label := Label3D.new()
    label.text = display_name
    label.position = Vector3(0, 2.0, 0)
    label.font_size = 32
    label.modulate = Color("#dcecff")
    label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
    add_child(label)

func _emissive_material(color: Color, energy: float) -> StandardMaterial3D:
    var mat := StandardMaterial3D.new()
    mat.albedo_color = color
    mat.emission_enabled = true
    mat.emission = color
    mat.emission_energy_multiplier = energy
    mat.roughness = 0.25
    return mat

func set_command(movement: Vector2, target: Vector3, fire: bool) -> void:
    move_input = movement.limit_length(1.0)
    aim_point = target
    fire_requested = fire

func _physics_process(delta: float) -> void:
    if disabled_state:
        velocity = Vector3.ZERO
        return
    fire_cooldown = maxf(0.0, fire_cooldown - delta)
    var desired := Vector3(move_input.x, 0.0, move_input.y) * speed
    velocity = velocity.move_toward(desired, acceleration * delta)
    move_and_slide()
    var half := ArenaWorld.FIELD_SIZE * 0.5 - Vector2(1.0, 1.0)
    global_position.x = clampf(global_position.x, -half.x, half.x)
    global_position.z = clampf(global_position.z, -half.y, half.y)
    if desired.length_squared() > 0.02:
        var facing := Vector3(desired.x, 0.0, desired.z)
        rotation.y = lerp_angle(rotation.y, atan2(-facing.x, -facing.z), minf(1.0, delta * 9.0))
    if turret_mesh and aim_point != Vector3.ZERO:
        turret_mesh.look_at(aim_point + Vector3.UP * 0.85, Vector3.UP)
    if fire_requested and fire_cooldown <= 0.0:
        _fire()
    fire_requested = false

func _fire() -> void:
    fire_cooldown = cooldown_time
    if game and game.has_method("spawn_projectile"):
        game.spawn_projectile(self, aim_point)
        fired.emit(self)

func take_damage(amount: int, hit_position: Vector3) -> void:
    if disabled_state:
        return
    hp = maxi(0, hp - amount)
    hp_changed.emit(self)
    if body_mesh:
        var original := base_material.albedo_color
        base_material.albedo_color = Color.WHITE
        var tween := create_tween()
        tween.tween_property(base_material, "albedo_color", original, 0.12)
    if hp <= 0:
        disabled_state = true
        disabled.emit(self)
        collision_layer = 0
        var tween := create_tween()
        tween.tween_property(self, "scale", Vector3(1.0, 0.12, 1.0), 0.4)
