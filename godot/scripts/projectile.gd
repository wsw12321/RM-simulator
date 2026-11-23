extends Area3D
var velocity := Vector3.ZERO
var owner_team := 0
var damage := 28
var life := 2.4
var source: Node
var trail: MeshInstance3D

func configure(origin: Vector3, target: Vector3, team: int, shooter: Node) -> void:
    global_position = origin + Vector3.UP * 0.7
    owner_team = team
    source = shooter
    velocity = (target - global_position).normalized() * 18.0
    collision_layer = 4
    collision_mask = 2
    var shape := CollisionShape3D.new()
    var sphere := SphereShape3D.new()
    sphere.radius = 0.12
    shape.shape = sphere
    add_child(shape)
    var mesh := MeshInstance3D.new()
    var ball := SphereMesh.new()
    ball.radius = 0.15
    ball.height = 0.3
    mesh.mesh = ball
    mesh.material_override = _material(team)
    add_child(mesh)
    trail = MeshInstance3D.new()
    var beam := BoxMesh.new()
    beam.size = Vector3(0.04, 0.04, 0.8)
    trail.mesh = beam
    trail.material_override = _material(team)
    add_child(trail)
    body_entered.connect(_on_body_entered)

func _physics_process(delta: float) -> void:
    global_position += velocity * delta
    if trail:
        trail.look_at(global_position - velocity.normalized(), Vector3.UP)
    life -= delta
    if life <= 0.0:
        queue_free()

func _on_body_entered(body: Node) -> void:
    if body == source:
        return
    if body.has_method("take_damage") and int(body.team) != owner_team:
        body.take_damage(damage, global_position)
        queue_free()

func _material(team: int) -> StandardMaterial3D:
    var mat := StandardMaterial3D.new()
    mat.albedo_color = Color("#ff5c72") if team == 0 else Color("#55a9ff")
    mat.emission_enabled = true
    mat.emission = mat.albedo_color
    mat.emission_energy_multiplier = 4.0
    return mat
