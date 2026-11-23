extends SceneTree

const RMSimAdapter = preload("res://scripts/sim_adapter.gd")

func _init() -> void:
    var adapter := RMSimAdapter.new()
    root.add_child(adapter)
    adapter.reset()
    assert(adapter.robots.size() == 2)
    var initial_hash := adapter.state_hash()
    for _i in 30:
        adapter.step([
            {"dx": 1000, "dy": 0, "aim_x": 10400, "aim_y": 3800, "fire": true},
            {"dx": -1000, "dy": 0, "aim_x": 1600, "aim_y": 3800, "fire": true}
        ])
    assert(adapter.tick == 30)
    assert(adapter.state_hash() != initial_hash)
    assert(adapter.export_replay().contains("frames"))
    print("GODOT_RM_SIM_SMOKE_OK tick=%d events=%d" % [adapter.tick, adapter.events.size()])
    quit()
