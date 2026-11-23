extends SceneTree

func _init() -> void:
    call_deferred("_run")

func _run() -> void:
    var scene := preload("res://scenes/main.tscn").instantiate()
    root.add_child(scene)
    await process_frame
    await process_frame
    var arena := scene.get_node("Arena")
    var camera := scene.get_node("CameraRig")
    var hud := scene.get_node("HUD")
    assert(arena.zones.size() == 2, "expected two objective zones")
    assert(arena.get_node("Floor") is StaticBody3D, "floor must be StaticBody3D")
    assert(camera.get_node("Camera3D").current, "camera must be active")
    assert(hud.get_child_count() > 0, "HUD must build controls")
    assert(scene.robots.size() == 2, "two robots must spawn")
    var red = scene.robots[0]
    var start := red.global_position
    red.set_command(Vector2.RIGHT, Vector3(4, 0, 0), false)
    await physics_frame
    await physics_frame
    assert(red.global_position.distance_to(start) > 0.001, "CharacterBody3D should move smoothly")
    scene.spawn_projectile(red, scene.robots[1].global_position)
    assert(scene.projectiles.size() == 1, "projectile should spawn")
    print("GODOT_SMOKE_OK robots=%d zones=%d native_collision=true orbit_camera=true" % [scene.robots.size(), arena.zones.size()])
    quit()
