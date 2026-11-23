extends Node3D
class_name RMGame

const RobotActorScene := preload("res://scripts/robot_actor.gd")
const ProjectileScene := preload("res://scripts/projectile.gd")

@onready var arena: ArenaWorld = $Arena
@onready var camera_rig: ArenaCamera = $CameraRig
@onready var hud: RMHud = $HUD

var robots: Array[RMRobotActor] = []
var projectiles: Array[RMProjectile] = []
var elapsed := 0.0
var running := true
var score := [0, 0]
var events: Array[String] = []
var rng := RandomNumberGenerator.new()

func _ready() -> void:
    rng.seed = 2027
    _spawn_robot(0, 0, "RED-01", Vector3(-6.5, 0.0, 0.0))
    _spawn_robot(1, 1, "BLUE-01", Vector3(6.5, 0.0, 0.0))
    camera_rig.focus(Vector3.ZERO)
    hud.bind_game(self)
    _log_event("RM Tactics Lab · native Godot 3D simulation online")

func _spawn_robot(id: int, team: int, label: String, spawn_position: Vector3) -> void:
    var robot: RMRobotActor = RobotActorScene.new()
    robot.configure(id, team, label, self)
    robot.position = spawn_position
    arena.add_child(robot)
    robot.hp_changed.connect(_on_robot_hp_changed)
    robot.disabled.connect(_on_robot_disabled)
    robots.append(robot)

func _physics_process(delta: float) -> void:
    if not running:
        return
    elapsed += delta
    _update_commands()
    _update_objectives(delta)
    if elapsed > 420.0:
        running = false
        _log_event("420s match limit reached")
    hud.refresh()

func _update_commands() -> void:
    if robots.is_empty():
        return
    var red := robots[0]
    var move := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
    var aim := _aim_point()
    red.set_command(move, aim, Input.is_action_pressed("fire"))
    if robots.size() > 1:
        var blue := robots[1]
        var to_red := red.global_position - blue.global_position
        var ai_move := Vector2(to_red.x, to_red.z).normalized()
        var should_fire := to_red.length() < 10.0
        blue.set_command(ai_move, red.global_position, should_fire)

func _aim_point() -> Vector3:
    var camera := get_viewport().get_camera_3d()
    if camera == null:
        return Vector3.ZERO
    var mouse := get_viewport().get_mouse_position()
    var origin := camera.project_ray_origin(mouse)
    var direction := camera.project_ray_normal(mouse)
    var plane := Plane(Vector3.UP, 0.0)
    var hit := plane.intersects_ray(origin, direction)
    return hit if hit != null else Vector3.ZERO

func spawn_projectile(shooter: RMRobotActor, target: Vector3) -> void:
    var projectile: RMProjectile = ProjectileScene.new()
    projectile.configure(shooter.global_position, target, shooter.team, shooter)
    arena.add_child(projectile)
    projectiles.append(projectile)

func _update_objectives(delta: float) -> void:
    for team in range(2):
        var actor := robots[team]
        if actor.disabled_state:
            continue
        if arena.is_inside_zone(actor.global_position, team):
            score[team] += int(delta * 10.0)

func set_running(value: bool) -> void:
    running = value
    _log_event("Simulation " + ("resumed" if running else "paused"))

func reset_match() -> void:
    for projectile in projectiles:
        if is_instance_valid(projectile):
            projectile.queue_free()
    projectiles.clear()
    for robot in robots:
        robot.queue_free()
    robots.clear()
    elapsed = 0.0
    score = [0, 0]
    running = true
    _ready()

func _on_robot_hp_changed(actor: RMRobotActor) -> void:
    _log_event(actor.display_name + " HP " + str(actor.hp))

func _on_robot_disabled(actor: RMRobotActor) -> void:
    _log_event(actor.display_name + " disabled")

func _log_event(message: String) -> void:
    events.push_front(message)
    if events.size() > 12:
        events.resize(12)
