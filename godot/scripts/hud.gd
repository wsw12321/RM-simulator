extends CanvasLayer
class_name RMHud

var game: RMGame
var title_label: Label
var timer_label: Label
var score_label: Label
var event_label: RichTextLabel
var status_label: Label
var pause_button: Button
var root: Control

func _ready() -> void:
    _build_ui()

func bind_game(owner: RMGame) -> void:
    game = owner
    refresh()

func _build_ui() -> void:
    root = Control.new()
    root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
    add_child(root)
    var top := ColorRect.new()
    top.color = Color(0.025, 0.05, 0.08, 0.92)
    top.set_anchors_preset(Control.PRESET_TOP_WIDE)
    top.size.y = 76
    root.add_child(top)
    title_label = _label("RM TACTICS LAB", Vector2(32, 18), 24, Color("#dff6ff"))
    root.add_child(title_label)
    status_label = _label("NATIVE GODOT 3D · FIELD REPLICA", Vector2(34, 50), 12, Color("#6fa5bc"))
    root.add_child(status_label)
    timer_label = _label("00:00", Vector2(640, 18), 28, Color("#f2f8ff"))
    timer_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    timer_label.size.x = 160
    root.add_child(timer_label)
    score_label = _label("RED 0   ·   BLUE 0", Vector2(1120, 24), 16, Color("#cce7ff"))
    score_label.size.x = 270
    root.add_child(score_label)
    var left_panel := _panel(Vector2(28, 112), Vector2(270, 190), Color(0.035, 0.075, 0.11, 0.94))
    root.add_child(left_panel)
    left_panel.add_child(_label("RED-01  ·  MANUAL", Vector2(18, 14), 15, Color("#ff7888")))
    left_panel.add_child(_label("WASD / ARROWS  MOVE", Vector2(18, 48), 12, Color("#a6c1ce")))
    left_panel.add_child(_label("MOUSE  AIM  ·  LMB FIRE", Vector2(18, 70), 12, Color("#a6c1ce")))
    var right_panel := _panel(Vector2(1140, 112), Vector2(270, 190), Color(0.035, 0.075, 0.11, 0.94))
    root.add_child(right_panel)
    right_panel.add_child(_label("BLUE-01  ·  AI", Vector2(18, 14), 15, Color("#6db8ff")))
    right_panel.add_child(_label("CHARACTERBODY3D", Vector2(18, 48), 12, Color("#a6c1ce")))
    right_panel.add_child(_label("AREA3D PROJECTILES", Vector2(18, 70), 12, Color("#a6c1ce")))
    var bottom := _panel(Vector2(28, 760), Vector2(1384, 112), Color(0.025, 0.05, 0.08, 0.96))
    bottom.name = "BottomBar"
    root.add_child(bottom)
    pause_button = Button.new()
    pause_button.text = "Ⅱ  PAUSE"
    pause_button.position = Vector2(18, 22)
    pause_button.size = Vector2(140, 54)
    pause_button.pressed.connect(_toggle_pause)
    bottom.add_child(pause_button)
    var reset := Button.new()
    reset.text = "RESET MATCH"
    reset.position = Vector2(172, 22)
    reset.size = Vector2(160, 54)
    reset.pressed.connect(_reset)
    bottom.add_child(reset)
    event_label = RichTextLabel.new()
    event_label.position = Vector2(360, 12)
    event_label.size = Vector2(980, 82)
    event_label.bbcode_enabled = true
    event_label.fit_content = false
    bottom.add_child(event_label)
    var hint := _label("Drag to orbit  ·  Wheel to zoom  ·  Touch: one finger orbit / two finger zoom", Vector2(350, 86), 11, Color("#587f91"))
    bottom.add_child(hint)

func _panel(pos: Vector2, size: Vector2, color: Color) -> Panel:
    var panel := Panel.new()
    panel.position = pos
    panel.size = size
    var style := StyleBoxFlat.new()
    style.bg_color = color
    style.border_color = Color(0.18, 0.36, 0.44, 0.82)
    style.set_border_width_all(1)
    style.corner_radius_top_left = 8
    style.corner_radius_top_right = 8
    style.corner_radius_bottom_left = 8
    style.corner_radius_bottom_right = 8
    panel.add_theme_stylebox_override("panel", style)
    return panel

func _label(text: String, pos: Vector2, size: int, color: Color) -> Label:
    var label := Label.new()
    label.text = text
    label.position = pos
    label.add_theme_font_size_override("font_size", size)
    label.add_theme_color_override("font_color", color)
    return label

func _toggle_pause() -> void:
    if game:
        game.set_running(not game.running)

func _reset() -> void:
    if game:
        game.reset_match()

func refresh() -> void:
    if game == null or not is_instance_valid(game):
        return
    var mins := int(game.elapsed) / 60
    var secs := int(game.elapsed) % 60
    timer_label.text = "%02d:%02d" % [mins, secs]
    score_label.text = "RED %d   ·   BLUE %d" % [game.score[0], game.score[1]]
    if game.robots.size() >= 2:
        var red: RMRobotActor = game.robots[0]
        var blue: RMRobotActor = game.robots[1]
        status_label.text = "RED %03d HP   ·   BLUE %03d HP   ·   %s" % [red.hp, blue.hp, "LIVE" if game.running else "PAUSED"]
    var lines: Array[String] = []
    for event in game.events:
        lines.append("[color=#80aabb]›[/color] " + event)
    event_label.text = "\n".join(lines)
