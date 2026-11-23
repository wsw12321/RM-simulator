class_name RMSimAdapter
extends Node
## Godot-side adapter for the integer-mm sandbox contract.
## Native C++ remains the authority for rule verification and replay export.

const HZ := 60
const WIDTH_MM := 12000
const HEIGHT_MM := 7600
const SPEED_MM_PER_TICK := 55
const PROJECTILE_SPEED_MM_PER_TICK := 260
const DAMAGE := 10
const COOLDOWN_TICKS := 10
const MAX_AMMO := 120
const MAX_HP := 100
const ROBOT_RADIUS_MM := 280

signal snapshot_changed(snapshot: Dictionary)
signal event_emitted(event: Dictionary)

var tick: int = 0
var terminal: bool = false
var next_id: int = 1
var robots: Array[Dictionary] = []
var bullets: Array[Dictionary] = []
var events: Array[Dictionary] = []
var _accumulator: float = 0.0
var _record_journal := true
var journal: Array[Dictionary] = []

func _ready() -> void:
    reset()

func reset() -> void:
    tick = 0
    terminal = false
    next_id = 1
    robots = [
        {"x": 1600, "y": 3800, "yaw": 0, "hp": MAX_HP, "ammo": MAX_AMMO, "cooldown": 0, "shots": 0, "hits": 0},
        {"x": 10400, "y": 3800, "yaw": 128, "hp": MAX_HP, "ammo": MAX_AMMO, "cooldown": 0, "shots": 0, "hits": 0}
    ]
    bullets.clear()
    events.clear()
    journal.clear()
    snapshot_changed.emit(snapshot())

func advance(delta: float, human_action: Dictionary = {}) -> void:
    _accumulator += delta
    var step_dt := 1.0 / float(HZ)
    while _accumulator >= step_dt and not terminal:
        _accumulator -= step_dt
        step([human_action, _scripted_action(1)])

func step(actions: Array[Dictionary]) -> bool:
    if terminal or actions.size() != 2:
        return false
    events.clear()
    var old := robots.duplicate(true)
    for index in 2:
        var robot: Dictionary = robots[index]
        if int(robot["hp"]) <= 0:
            continue
        var action: Dictionary = actions[index]
        var dx := clampi(int(action.get("dx", 0)), -1000, 1000)
        var dy := clampi(int(action.get("dy", 0)), -1000, 1000)
        var length := sqrt(float(dx * dx + dy * dy))
        var divisor := maxf(1000.0, length)
        robot["x"] = clampi(int(robot["x"]) + int(float(dx) * SPEED_MM_PER_TICK / divisor), ROBOT_RADIUS_MM, WIDTH_MM - ROBOT_RADIUS_MM)
        robot["y"] = clampi(int(robot["y"]) + int(float(dy) * SPEED_MM_PER_TICK / divisor), ROBOT_RADIUS_MM, HEIGHT_MM - ROBOT_RADIUS_MM)
        var aim_x := int(action.get("aim_x", robot["x"]))
        var aim_y := int(action.get("aim_y", robot["y"]))
        robot["yaw"] = _aim_direction(Vector2i(int(robot["x"]), int(robot["y"])), Vector2i(aim_x, aim_y), int(robot["yaw"]))
        robot["cooldown"] = maxi(0, int(robot["cooldown"]) - 1)
        robots[index] = robot
    if _overlapping(robots[0], robots[1]):
        robots[0]["x"] = old[0]["x"]
        robots[0]["y"] = old[0]["y"]
        robots[1]["x"] = old[1]["x"]
        robots[1]["y"] = old[1]["y"]
    for index in 2:
        var action: Dictionary = actions[index]
        var robot: Dictionary = robots[index]
        if int(robot["hp"]) > 0 and bool(action.get("fire", false)) and int(robot["cooldown"]) == 0 and int(robot["ammo"]) > 0 and bullets.size() < 128:
            var direction := _direction(int(robot["yaw"]))
            bullets.append({"id": next_id, "owner": index, "x": int(robot["x"]) + direction.x * 320, "y": int(robot["y"]) + direction.y * 320, "vx": direction.x * PROJECTILE_SPEED_MM_PER_TICK, "vy": direction.y * PROJECTILE_SPEED_MM_PER_TICK, "life": 180})
            next_id += 1
            robot["ammo"] = int(robot["ammo"]) - 1
            robot["shots"] = int(robot["shots"]) + 1
            robot["cooldown"] = COOLDOWN_TICKS
            robots[index] = robot
            _emit_event(1, index, -1, 0)
    _advance_bullets()
    tick += 1
    if tick >= 7200 or int(robots[0]["hp"]) <= 0 or int(robots[1]["hp"]) <= 0:
        terminal = true
    if _record_journal:
        journal.append({"tick": tick - 1, "actions": actions.duplicate(true), "hash": state_hash()})
    snapshot_changed.emit(snapshot())
    return true

func snapshot() -> Dictionary:
    return {"build": "godot-adapter-integer-mm-v1", "tick": tick, "terminal": terminal, "robots": robots.duplicate(true), "bullets": bullets.duplicate(true), "events": events.duplicate(true), "hash": state_hash()}

func import_native_snapshot(json_text: String) -> bool:
    var parsed = JSON.parse_string(json_text)
    if typeof(parsed) != TYPE_DICTIONARY or not parsed.has("robots"):
        return false
    tick = int(parsed.get("tick", 0))
    terminal = int(parsed.get("terminal", 0)) != 0
    robots = _dictionary_array(parsed["robots"])
    bullets = _dictionary_array(parsed.get("bullets", []))
    events = _dictionary_array(parsed.get("events", []))
    snapshot_changed.emit(snapshot())
    return true

func _dictionary_array(value: Variant) -> Array[Dictionary]:
    var result: Array[Dictionary] = []
    if typeof(value) != TYPE_ARRAY:
        return result
    for item in value:
        if typeof(item) == TYPE_DICTIONARY:
            result.append(_normalize_dictionary(item))
    return result

func _normalize_dictionary(source: Dictionary) -> Dictionary:
    var normalized: Dictionary = {}
    for key in source:
        var value: Variant = source[key]
        if typeof(value) == TYPE_FLOAT and is_equal_approx(value, round(value)):
            normalized[key] = int(value)
        else:
            normalized[key] = value
    return normalized

func export_replay() -> String:
    return JSON.stringify({"schema": 1, "engine": "godot-adapter-integer-mm-v1", "frames": journal})

func state_hash() -> int:
    var text := JSON.stringify({"tick": tick, "robots": robots, "bullets": bullets, "terminal": terminal})
    var hash := 2166136261
    for byte in text.to_utf8_buffer():
        hash = int((hash ^ int(byte)) * 16777619) & 0x7fffffff
    return hash

func _advance_bullets() -> void:
    var alive: Array[Dictionary] = []
    for bullet in bullets:
        bullet["x"] = int(bullet["x"]) + int(bullet["vx"])
        bullet["y"] = int(bullet["y"]) + int(bullet["vy"])
        bullet["life"] = int(bullet["life"]) - 1
        var target := 1 - int(bullet["owner"])
        if int(robots[target]["hp"]) > 0 and _distance_squared(bullet, robots[target]) <= (ROBOT_RADIUS_MM + 30) * (ROBOT_RADIUS_MM + 30):
            robots[target]["hp"] = maxi(0, int(robots[target]["hp"]) - DAMAGE)
            var owner := int(bullet["owner"])
            robots[owner]["hits"] = int(robots[owner]["hits"]) + 1
            _emit_event(2, owner, target, DAMAGE)
            if int(robots[target]["hp"]) == 0:
                _emit_event(3, target, target, 0)
            continue
        if int(bullet["life"]) > 0 and int(bullet["x"]) >= 0 and int(bullet["x"]) <= WIDTH_MM and int(bullet["y"]) >= 0 and int(bullet["y"]) <= HEIGHT_MM:
            alive.append(bullet)
    bullets = alive

func _scripted_action(index: int) -> Dictionary:
    var enemy := robots[1 - index]
    var self_robot := robots[index]
    return {"dx": signi(int(enemy["x"]) - int(self_robot["x"])) * 1000, "dy": signi(int(enemy["y"]) - int(self_robot["y"])) * 1000, "aim_x": int(enemy["x"]), "aim_y": int(enemy["y"]), "fire": true}

func _aim_direction(from: Vector2i, target: Vector2i, previous: int) -> int:
    var angle := atan2(float(target.y - from.y), float(target.x - from.x))
    var desired := int(round((angle / TAU) * 256.0)) & 255
    var diff := (desired - previous + 384) % 256 - 128
    return (previous + clampi(diff, -4, 4) + 256) % 256

func _direction(yaw: int) -> Vector2i:
    var angle := float(yaw) / 256.0 * TAU
    return Vector2i(roundi(cos(angle) * 1000.0), roundi(sin(angle) * 1000.0))

func _overlapping(a: Dictionary, b: Dictionary) -> bool:
    return _distance_squared(a, b) < (ROBOT_RADIUS_MM * 2) * (ROBOT_RADIUS_MM * 2)

func _distance_squared(a: Dictionary, b: Dictionary) -> int:
    var dx := int(a["x"]) - int(b["x"])
    var dy := int(a["y"]) - int(b["y"])
    return dx * dx + dy * dy

func _emit_event(kind: int, actor: int, target: int, amount: int) -> void:
    var event := {"tick": tick, "kind": kind, "actor": actor, "target": target, "amount": amount}
    events.append(event)
    event_emitted.emit(event)
