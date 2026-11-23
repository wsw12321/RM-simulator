extends Node3D
const RobotActorScene := preload("res://scripts/robot_actor.gd")
const ProjectileScene := preload("res://scripts/projectile.gd")

@onready var arena: Node3D = $Arena
@onready var camera_rig: Node3D = $CameraRig
@onready var hud: CanvasLayer = $HUD

var robots: Array[Node] = []
var projectiles: Array[Node] = []
var elapsed: float = 0.0
var running := true
var score := [0, 0]
var events: Array[String] = []
var rng: RandomNumberGenerator = RandomNumberGenerator.new()

func _ready() -> void:
    rng.seed = 2027
    _spawn_robot(0, 0, "RED-01", Vector3(-6.5, 0.0, 0.0))
    _spawn_robot(1, 1, "BLUE-01", Vector3(6.5, 0.0, 0.0))
    camera_rig.call("focus", Vector3.ZERO)
    hud.call("bind_game", self)
    _log_event("RM Tactics Lab · native Godot 3D simulation online")

func _spawn_robot(id: int, team: int, label: String, spawn_position: Vector3) -> void:
    var robot: Node = RobotActorScene.new()
    robot.call("configure", id, team, label, self)
    robot.position = spawn_position
    arena.add_child(robot)
    robot.connect("hp_changed", Callable(self, "_on_robot_hp_changed"))
    robot.connect("disabled", Callable(self, "_on_robot_disabled"))
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
    hud.call("refresh")

func _update_commands() -> void:
    if robots.is_empty():
        return
    var red: Node = robots[0]
    var move: Vector2 = Input.get_vector("move_left", "move_right", "move_forward", "move_back")
    var aim := _aim_point()
    red.call("set_command", move, aim, Input.is_action_pressed("fire"))
    if robots.size() > 1:
        var blue: Node = robots[1]
        var red_position: Vector3 = red.get("global_position")
        var blue_position: Vector3 = blue.get("global_position")
        var to_red: Vector3 = red_position - blue_position
        var ai_move := Vector2(to_red.x, to_red.z).normalized()
        var should_fire := to_red.length() < 10.0
        blue.call("set_command", ai_move, red_position, should_fire)

func _aim_point() -> Vector3:
    var camera: Camera3D = get_viewport().get_camera_3d()
    if camera == null:
        return Vector3.ZERO
    var mouse: Vector2 = get_viewport().get_mouse_position()
    var origin: Vector3 = camera.project_ray_origin(mouse)
    var direction: Vector3 = camera.project_ray_normal(mouse)
    var plane: Plane = Plane(Vector3.UP, 0.0)
    var hit: Variant = plane.intersects_ray(origin, direction)
    return hit if hit != null else Vector3.ZERO

func spawn_projectile(shooter: Node, target: Vector3) -> void:
    var projectile: Node = ProjectileScene.new()
    arena.add_child(projectile)
    projectile.call("configure", shooter.get("global_position"), target, shooter.get("team"), shooter)
    projectiles.append(projectile)

func _update_objectives(delta: float) -> void:
    for team in range(2):
        var actor: Node = robots[team]
        if bool(actor.get("disabled_state")):
            continue
        if bool(arena.call("is_inside_zone", actor.get("global_position"), team)):
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

func _on_robot_hp_changed(actor: Node) -> void:
    _log_event(str(actor.get("display_name")) + " HP " + str(actor.get("hp")))

func _on_robot_disabled(actor: Node) -> void:
    _log_event(str(actor.get("display_name")) + " disabled")

func _log_event(message: String) -> void:
    events.push_front(message)
    if events.size() > 12:
        events.resize(12)
