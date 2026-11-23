extends Node3D
@export var target_path: NodePath
@export var distance := 19.0
@export var min_distance := 10.0
@export var max_distance := 30.0
@export var pitch := -52.0
@export var yaw := 0.0
@export var orbit_sensitivity := 0.008
@export var zoom_sensitivity := 1.2

var target := Vector3.ZERO
var dragging := false
var last_pointer := Vector2.ZERO
var touch_points: Dictionary = {}
var last_pinch := 0.0

func _ready() -> void:
    position = Vector3(0, 10, 14)
    _apply_transform()

func _process(_delta: float) -> void:
    _apply_transform()

func _apply_transform() -> void:
    var pitch_rad := deg_to_rad(pitch)
    var yaw_rad := deg_to_rad(yaw)
    var offset := Vector3(
        sin(yaw_rad) * cos(pitch_rad) * distance,
        -sin(pitch_rad) * distance,
        cos(yaw_rad) * cos(pitch_rad) * distance
    )
    global_position = target + offset
    look_at(target + Vector3.UP * 0.2, Vector3.UP)

func focus(world_target: Vector3) -> void:
    target = world_target

func _unhandled_input(event: InputEvent) -> void:
    if event is InputEventMouseButton:
        if event.button_index == MOUSE_BUTTON_LEFT:
            dragging = event.pressed
            last_pointer = event.position
        elif event.button_index == MOUSE_BUTTON_WHEEL_UP:
            distance = clamp(distance - zoom_sensitivity, min_distance, max_distance)
        elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
            distance = clamp(distance + zoom_sensitivity, min_distance, max_distance)
    elif event is InputEventMouseMotion and dragging:
        yaw -= event.relative.x * orbit_sensitivity * 55.0
        pitch = clamp(pitch - event.relative.y * orbit_sensitivity * 55.0, -78.0, -28.0)
    elif event is InputEventScreenTouch:
        if event.pressed:
            touch_points[event.index] = event.position
        else:
            touch_points.erase(event.index)
        if touch_points.size() < 2:
            last_pinch = 0.0
    elif event is InputEventScreenDrag:
        touch_points[event.index] = event.position
        if touch_points.size() == 1:
            yaw -= event.relative.x * orbit_sensitivity * 36.0
            pitch = clamp(pitch - event.relative.y * orbit_sensitivity * 36.0, -78.0, -28.0)
        elif touch_points.size() >= 2:
            var points := touch_points.values()
            var first: Vector2 = points[0]
            var second: Vector2 = points[1]
            var pinch := first.distance_to(second)
            if last_pinch > 0.0:
                distance = clamp(distance - (pinch - last_pinch) * 0.02, min_distance, max_distance)
            last_pinch = pinch
