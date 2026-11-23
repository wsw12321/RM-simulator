extends Camera3D

var focus := Vector3(0, 0, 0)
var azimuth := deg_to_rad(35.0)
var elevation := deg_to_rad(52.0)
var distance := 16.5
var dragging := false

func _ready() -> void:
	current = true
	_update_transform()

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			dragging = event.pressed
		if event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_UP:
			distance = maxf(10.0, distance - 1.0)
		if event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			distance = minf(25.0, distance + 1.0)
	if event is InputEventMouseMotion and dragging:
		azimuth -= event.relative.x * 0.008
		elevation = clampf(elevation + event.relative.y * 0.006, deg_to_rad(35.0), deg_to_rad(72.0))

func _process(_delta: float) -> void:
	_update_transform()

func _update_transform() -> void:
	var offset := Vector3(sin(azimuth) * cos(elevation), sin(elevation), cos(azimuth) * cos(elevation)) * distance
	global_position = focus + offset
	look_at(focus, Vector3.UP)
