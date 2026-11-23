class_name RMUCHud
extends CanvasLayer

const BG := Color("#07101f")
const PANEL := Color("#0f1c31")
const PANEL_HI := Color("#172945")
const TEXT := Color("#e8f0ff")
const MUTED := Color("#8194b8")
const CYAN := Color("#59e6d5")
const AMBER := Color("#ffc56b")
const RED := Color("#ff5b6e")
const BLUE := Color("#5aa8ff")

var root: Control
var timer_label: Label
var phase_label: Label
var score_label: Label
var event_label: Label
var telemetry_label: Label
var selected_label: Label

func _ready() -> void:
	build_hud()

func build_hud() -> void:
	root = Control.new()
	root.name = "HudRoot"
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)
	var top := panel("TopBar", Vector2(0.0, 0.0), Vector2(0.0, 92.0), Color("#0a1528"))
	top.set_anchors_preset(Control.PRESET_TOP_WIDE)
	root.add_child(top)
	var title := label("RMUC // TACTICS LAB", 28, CYAN)
	title.position = Vector2(28.0, 17.0)
	top.add_child(title)
	var subtitle := label("SIMULATION DECK  /  FIELD 01", 12, MUTED)
	subtitle.position = Vector2(31.0, 54.0)
	top.add_child(subtitle)
	phase_label = label("PRE-MATCH", 14, AMBER)
	phase_label.position = Vector2(360.0, 22.0)
	top.add_child(phase_label)
	timer_label = label("07:00", 30, TEXT)
	timer_label.position = Vector2(360.0, 42.0)
	top.add_child(timer_label)
	score_label = label("RED  000   —   000  BLUE", 18, TEXT)
	score_label.position = Vector2(650.0, 34.0)
	top.add_child(score_label)
	var right_tag := label("BUILD  /  RMUC-TACTICS-GODOT", 12, MUTED)
	right_tag.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	right_tag.position = Vector2(-315.0, 38.0)
	top.add_child(right_tag)
	var left := panel("MatchPanel", Vector2(24.0, 122.0), Vector2(290.0, 286.0), PANEL)
	root.add_child(left)
	var left_title := label("MATCH CONTROL", 13, CYAN)
	left_title.position = Vector2(20.0, 18.0)
	left.add_child(left_title)
	selected_label = label("SELECTED  /  NONE", 15, TEXT)
	selected_label.position = Vector2(20.0, 58.0)
	left.add_child(selected_label)
	left.add_child(meter("HP", Vector2(20.0, 100.0), RED, 0.82))
	left.add_child(meter("AMMO", Vector2(20.0, 146.0), AMBER, 0.64))
	left.add_child(meter("HEAT", Vector2(20.0, 192.0), CYAN, 0.28))
	var hint := label("DRAG TO PAN  ·  WHEEL TO ZOOM", 11, MUTED)
	hint.position = Vector2(20.0, 246.0)
	left.add_child(hint)
	var right := panel("TelemetryPanel", Vector2(-286.0, 122.0), Vector2(262.0, 304.0), PANEL)
	right.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	root.add_child(right)
	var telemetry_title := label("LIVE TELEMETRY", 13, CYAN)
	telemetry_title.position = Vector2(18.0, 18.0)
	right.add_child(telemetry_title)
	telemetry_label = label("TICK       000000\nCLOCK      00:00.0\nMODE       OBSERVE\nUNITS      02 / 02\nCONTROL    RED 0.0%\n           BLUE 0.0%\n\nSIGNAL     NOMINAL\nPHYSICS    LOCKSTEP", 13, TEXT)
	telemetry_label.position = Vector2(18.0, 56.0)
	telemetry_label.add_theme_constant_override("line_spacing", 8)
	right.add_child(telemetry_label)
	var bottom := panel("EventBar", Vector2(24.0, -92.0), Vector2(-48.0, 64.0), Color("#0a1528"))
	bottom.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	root.add_child(bottom)
	event_label = label("●  SYSTEM READY     Select a unit to inspect its telemetry", 13, MUTED)
	event_label.position = Vector2(20.0, 21.0)
	bottom.add_child(event_label)
	var pause := button("PAUSE", Vector2(-116.0, 13.0), Vector2(94.0, 38.0))
	pause.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	bottom.add_child(pause)
	var reset := button("RESET", Vector2(-218.0, 13.0), Vector2(86.0, 38.0))
	reset.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	bottom.add_child(reset)

func set_match_state(phase: String, clock: String, red_score: int, blue_score: int) -> void:
	if phase_label:
		phase_label.text = phase
	if timer_label:
		timer_label.text = clock
	if score_label:
		score_label.text = "RED  %03d   —   %03d  BLUE" % [red_score, blue_score]

func set_selected(callsign: String, role: String) -> void:
	if selected_label:
		selected_label.text = "SELECTED  /  %s\n%s" % [callsign, role]

func set_event(message: String, accent := MUTED) -> void:
	if event_label:
		event_label.text = "●  " + message
		event_label.modulate = accent

func panel(node_name: String, position: Vector2, size: Vector2, color: Color) -> Panel:
	var panel_node := Panel.new()
	panel_node.name = node_name
	panel_node.position = position
	panel_node.size = size
	var style := StyleBoxFlat.new()
	style.bg_color = color
	style.border_color = Color(0.28, 0.56, 0.78, 0.24)
	style.set_border_width_all(1)
	style.corner_radius_top_left = 8
	style.corner_radius_top_right = 8
	style.corner_radius_bottom_left = 8
	style.corner_radius_bottom_right = 8
	style.shadow_color = Color(0.0, 0.0, 0.0, 0.34)
	style.shadow_size = 10
	panel_node.add_theme_stylebox_override("panel", style)
	return panel_node

func label(text_value: String, font_size: int, color: Color) -> Label:
	var node := Label.new()
	node.text = text_value
	node.add_theme_font_size_override("font_size", font_size)
	node.add_theme_color_override("font_color", color)
	node.add_theme_color_override("font_shadow_color", Color(0.0, 0.0, 0.0, 0.6))
	node.add_theme_constant_override("shadow_offset_x", 1)
	node.add_theme_constant_override("shadow_offset_y", 2)
	return node

func meter(title: String, position: Vector2, color: Color, ratio: float) -> Control:
	var node := Control.new()
	node.position = position
	node.size = Vector2(248.0, 32.0)
	var caption := label(title, 11, MUTED)
	node.add_child(caption)
	var track := ColorRect.new()
	track.position = Vector2(52.0, 5.0)
	track.size = Vector2(176.0, 8.0)
	track.color = Color(0.07, 0.12, 0.20, 1.0)
	node.add_child(track)
	var fill := ColorRect.new()
	fill.position = Vector2(52.0, 5.0)
	fill.size = Vector2(176.0 * ratio, 8.0)
	fill.color = color
	node.add_child(fill)
	return node

func button(text_value: String, position: Vector2, size: Vector2) -> Button:
	var node := Button.new()
	node.text = text_value
	node.position = position
	node.size = size
	node.add_theme_font_size_override("font_size", 11)
	var normal := StyleBoxFlat.new()
	normal.bg_color = PANEL_HI
	normal.border_color = Color(0.35, 0.7, 0.82, 0.5)
	normal.set_border_width_all(1)
	normal.corner_radius_top_left = 5
	normal.corner_radius_top_right = 5
	normal.corner_radius_bottom_left = 5
	normal.corner_radius_bottom_right = 5
	node.add_theme_stylebox_override("normal", normal)
	var hover := normal.duplicate()
	hover.bg_color = Color("#234263")
	node.add_theme_stylebox_override("hover", hover)
	return node
