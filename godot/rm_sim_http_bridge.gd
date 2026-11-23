class_name RMSimHttpBridge
extends Node
## Asynchronous loopback bridge to the existing Python/C++ authority.
## The server must be started with `python3 server.py` before enabling this node.

signal state_received(snapshot: Dictionary)
signal request_failed(message: String)

@export var base_url := "http://127.0.0.1:8765"
var transport := "python-loopback-cpp"
var _request: HTTPRequest

func _ready() -> void:
	_request = HTTPRequest.new()
	_request.timeout = 10.0
	add_child(_request)
	_request.request_completed.connect(_on_request_completed)

func fetch_state() -> void:
	_queue(HTTPClient.METHOD_GET, "/api/state", {})

func reset() -> void:
	_queue(HTTPClient.METHOD_POST, "/api/reset", {})

func step(count: int, mode: int, action: Array) -> void:
	if count < 1 or count > 12 or mode < 0 or mode > 2 or action.size() != 5:
		request_failed.emit("Invalid sandbox step request")
		return
	_queue(HTTPClient.METHOD_POST, "/api/step", {"count": count, "mode": mode, "action": action})

func _queue(method: HTTPClient.Method, path: String, payload: Dictionary) -> void:
	if _request == null:
		request_failed.emit("HTTP bridge is not ready")
		return
	var headers := PackedStringArray(["X-Sandbox-Client: 1", "Accept: application/json"])
	var body := ""
	if method == HTTPClient.METHOD_POST:
		headers.append("Content-Type: application/json")
		body = JSON.stringify(payload)
	var error := _request.request(base_url.trim_suffix("/") + path, headers, method, body)
	if error != OK:
		request_failed.emit("HTTP request failed with code %d" % error)

func _on_request_completed(result: int, response_code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	if result != HTTPRequest.RESULT_SUCCESS or response_code < 200 or response_code >= 300:
		request_failed.emit("Loopback server unavailable (%d/%d)" % [result, response_code])
		return
	var parsed = JSON.parse_string(body.get_string_from_utf8())
	if typeof(parsed) != TYPE_DICTIONARY:
		request_failed.emit("Loopback response was not a JSON object")
		return
	if parsed.has("error"):
		request_failed.emit(str(parsed["error"]))
		return
	state_received.emit(parsed)
