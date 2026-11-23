class_name RMSimBridge
extends Node
## Stable Godot-facing adapter for RM-simulator.
## The default transport is an explicit deterministic mock. The native C++
## process can replace it later while preserving reset/advance/export state.

signal state_changed(snapshot: Dictionary)
signal bridge_error(message: String)

const WIDTH := 12000
const HEIGHT := 7600
const ROBOT_RADIUS := 280
const SPEED := 55
const MAX_HP := 100
const MAX_AMMO := 120
const TICK_HZ := 60

var transport: String = "mock"
var state: Dictionary = {}

func _ready() -> void:
	reset()

func reset() -> Dictionary:
	state = {
		"build": "godot-mock-adapter-v1",
		"profile": "sandbox-v1",
		"official": false,
		"transport": transport,
		"width": WIDTH,
		"height": HEIGHT,
		"hz": TICK_HZ,
		"tick": 0,
		"terminal": 0,
		"robots": [_robot(0, 1600, 3800, 0), _robot(1, 10400, 3800, 128)],
		"bullets": [],
		"events": []
	}
	_publish()
	return state.duplicate(true)

func advance(count: int = 1, mode: int = 0, human_action: Dictionary = {}) -> Dictionary:
	if count < 1 or count > 600:
		_fail("advance count must be in [1, 600]")
		return state.duplicate(true)
	if mode < 0 or mode > 2:
		_fail("advance mode must be 0, 1, or 2")
		return state.duplicate(true)
	if state.is_empty():
		reset()
	var action := _action(human_action)
	var events: Array = []
	for _step in range(count):
		if int(state.get("terminal", 0)) != 0:
			break
		var robots: Array = state["robots"]
		var player: Dictionary = robots[0].duplicate(true)
		var rival: Dictionary = robots[1].duplicate(true)
		if mode == 0 or mode == 2:
			_player_step(player, action)
		_scripted_step(rival, player)
		state["tick"] = int(state["tick"]) + 1
		if int(player["hp"]) <= 0 or int(rival["hp"]) <= 0:
			state["terminal"] = 1
		if int(action.get("fire", 0)) != 0 and int(player["ammo"]) > 0 and _distance(player, rival) < 4200.0:
			player["ammo"] = int(player["ammo"]) - 1
			events.append({"tick": state["tick"], "kind": "shot", "actor": 0, "target": -1, "amount": 0})
			if _distance(player, rival) < 2300.0:
				rival["hp"] = maxi(0, int(rival["hp"]) - 10)
				events.append({"tick": state["tick"], "kind": "hit", "actor": 0, "target": 1, "amount": 10})
		state["robots"] = [player, rival]
	state["events"] = events
	_publish()
	return state.duplicate(true)

func export_replay() -> Dictionary:
	return {"schema": 1, "profile": "sandbox-v1", "engine": "godot-adapter-v1", "official": false, "transport": transport, "frames": [], "final_state": state.duplicate(true)}

func use_mock() -> void:
	transport = "mock"
	if not state.is_empty():
		state["transport"] = transport

func _publish() -> void:
	state["transport"] = transport
	state_changed.emit(state.duplicate(true))

func _fail(message: String) -> void:
	bridge_error.emit(message)

func _robot(index: int, x: int, y: int, yaw: int) -> Dictionary:
	return {"id": index, "x": x, "y": y, "yaw": yaw, "hp": MAX_HP, "ammo": MAX_AMMO, "cooldown": 0, "shots": 0, "hits": 0}

func _action(raw: Dictionary) -> Dictionary:
	return {"dx": clampi(int(raw.get("dx", 0)), -1000, 1000), "dy": clampi(int(raw.get("dy", 0)), -1000, 1000), "aim_x": clampi(int(raw.get("aim_x", 1600)), 0, WIDTH), "aim_y": clampi(int(raw.get("aim_y", 3800)), 0, HEIGHT), "fire": 1 if int(raw.get("fire", 0)) != 0 else 0}

func _player_step(robot: Dictionary, action: Dictionary) -> void:
	var dx := int(action["dx"])
	var dy := int(action["dy"])
	var length := sqrt(float(dx * dx + dy * dy))
	if length > 0.0:
		robot["x"] = clampi(int(robot["x"]) + int(round(float(dx) / maxf(1000.0, length) * SPEED)), ROBOT_RADIUS, WIDTH - ROBOT_RADIUS)
		robot["y"] = clampi(int(robot["y"]) + int(round(float(dy) / maxf(1000.0, length) * SPEED)), ROBOT_RADIUS, HEIGHT - ROBOT_RADIUS)
	var target := Vector2(float(action["aim_x"]), float(action["aim_y"]))
	var origin := Vector2(float(robot["x"]), float(robot["y"]))
	if origin.distance_to(target) > 0.1:
		robot["yaw"] = int(round(origin.angle_to_point(target) / TAU * 256.0 + 256.0)) % 256

func _scripted_step(robot: Dictionary, target: Dictionary) -> void:
	var from := Vector2(float(robot["x"]), float(robot["y"]))
	var to := Vector2(float(target["x"]), float(target["y"]))
	var delta := from.direction_to(to)
	robot["x"] = clampi(int(robot["x"]) + int(round(delta.x * SPEED * 0.65)), ROBOT_RADIUS, WIDTH - ROBOT_RADIUS)
	robot["y"] = clampi(int(robot["y"]) + int(round(delta.y * SPEED * 0.65)), ROBOT_RADIUS, HEIGHT - ROBOT_RADIUS)
	robot["yaw"] = int(round(from.angle_to_point(to) / TAU * 256.0 + 256.0)) % 256

func _distance(first: Dictionary, second: Dictionary) -> float:
	return Vector2(float(first["x"]), float(first["y"])).distance_to(Vector2(float(second["x"]), float(second["y"])))
