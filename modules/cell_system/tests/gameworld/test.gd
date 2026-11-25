extends Node

# Drives GameWorld through its whole surface and prints ok / FAIL per check.

var world
var cam
var frame = 0
var stage = 0
var fails = 0
var events = []
var saved_state = null

func check(cond, msg):
	if cond:
		print("  ok   ", msg)
	else:
		print("  FAIL ", msg)
		fails += 1

func cell_x(i):
	return i * 64.0 + 32.0

func make_world(terrain):
	var w = GameWorld.new()
	w.name = "World"
	w.has_terrain = terrain
	w.cell_size = 64.0
	w.load_radius = 1
	w.unload_margin = 1
	w.world_dir = "user://gwtest_world"
	var folder = Spatial.new()
	folder.name = "Village"
	w.add_child(folder)
	folder.owner = w
	for i in range(21):
		var m = MeshInstance.new()
		m.name = "House%d" % i
		m.mesh = CubeMesh.new()
		m.translation = Vector3(cell_x(i - 10), 0, 32)
		folder.add_child(m)
		m.owner = w

	# A scripted controller far from the origin, with something visible under
	# it: logic, must never be streamed out.
	var sc = GDScript.new()
	sc.source_code = "extends Spatial\n"
	sc.reload()
	var weather = Spatial.new()
	weather.name = "Weather"
	weather.set_script(sc)
	weather.translation = Vector3(cell_x(9), 0, 32)
	w.add_child(weather)
	weather.owner = w
	var dust = MeshInstance.new()
	dust.name = "Dust"
	dust.mesh = CubeMesh.new()
	weather.add_child(dust)
	dust.owner = w

	# A trigger far away: left alone.
	var trig = Area.new()
	trig.name = "Trigger"
	trig.translation = Vector3(cell_x(-9), 0, 32)
	w.add_child(trig)
	trig.owner = w

	# A long object: pivot in cell 3, but 300 units wide, so it reaches cell 1.
	var cliff = MeshInstance.new()
	cliff.name = "Cliff"
	var cm = CubeMesh.new()
	cm.size = Vector3(300, 2, 2)
	cliff.mesh = cm
	cliff.translation = Vector3(cell_x(3), 0, 32)
	w.add_child(cliff)
	cliff.owner = w
	return w

func add_crate(w, x):
	var rb = RigidBody.new()
	rb.name = "Crate"
	rb.mode = RigidBody.MODE_STATIC
	var cs = CollisionShape.new()
	cs.shape = BoxShape.new()
	rb.add_child(cs)
	rb.translation = Vector3(x, 50, 32)
	w.add_child(rb)
	rb.owner = w
	cs.owner = w
	return rb

func has(path):
	return get_node_or_null(path) != null

func _on_loaded(cx, cz):
	events.append("L%d" % cx)

func _on_unloaded(cx, cz):
	events.append("U%d" % cx)

func next_stage():
	stage += 1
	frame = 0

func _ready():
	cam = Camera.new()
	cam.name = "Cam"
	add_child(cam)

func _process(_dt):
	frame += 1
	match stage:
		0:
			stage_interior()
		1:
			stage_bake()
		2:
			stage_terrain()
		3:
			print("")
			print("RESULT: %d failure(s)" % fails)
			get_tree().quit()
			stage = 4

# ---------------------------------------------------------------- interior
func stage_interior():
	if frame == 1:
		print("== Interior world: object streaming, hysteresis, state, layers ==")
		world = make_world(false)
		add_crate(world, cell_x(0))
		world.layer_data = {"next_id": 1, "layers": [{"id": 0, "name": "Quest", "visible": true, "runtime": true, "children": []}]}
		world.get_node("Village/House16").set_meta("_scene_layer_ids", [0])
		world.connect("cell_loaded", self, "_on_loaded")
		world.connect("cell_unloaded", self, "_on_unloaded")
		add_child(world)
		cam.translation = Vector3(cell_x(0), 10, 32)
		cam.current = true
		check(world.get_terrain() == null and world.get_foliage() == null, "has_terrain off: no terrain, no foliage created")
	elif frame == 4:
		check(world.get_object_count() == 23, "23 objects tracked (got %d)" % world.get_object_count())
		check(world.get_attached_object_count() == 5, "3 houses + crate + cliff in the tree around cell 0 (got %d)" % world.get_attached_object_count())
		check(has("World/Cliff"), "large object loaded: its bounding box reaches cell 1 though its pivot is in cell 3")
		check(has("World/Weather/Dust"), "scripted controller 9 cells away stays loaded, subtree included")
		check(has("World/Trigger"), "Area 9 cells away stays loaded")
		check(has("World/Village/House10") and has("World/Village/House9") and has("World/Village/House11"), "houses of cells -1..1 are in the tree")
		check(not has("World/Village/House13"), "house of cell 3 is NOT in the tree")
		check(has("World/Village"), "folder node untouched")
		check(world.get_node("Village/House10").get_parent().name == "Village", "hierarchy preserved (no reparenting)")
		check(world.get_cell_state(0, 0) == GameWorld.CELL_ACTIVE and world.get_cell_state(5, 0) == GameWorld.CELL_UNLOADED, "cell states reported")
		cam.translation.x = cell_x(5)
	elif frame == 8:
		check(not has("World/Village/House10"), "cell 0 unloaded after moving to cell 5")
		check(has("World/Village/House14") and has("World/Village/House15") and has("World/Village/House16"), "cells 4..6 loaded")
		check(not has("World/Crate"), "crate parked with its cell")
		check(events.has("U0") and events.has("L5"), "cell_loaded / cell_unloaded emitted")
		cam.translation.x = cell_x(6)
	elif frame == 12:
		check(has("World/Village/House14"), "hysteresis: cell 4 kept at distance 2")
		check(has("World/Village/House17"), "cell 7 loaded")
		world.set_layer_enabled(world.get_layer_id("Quest"), false)
		world.get_node("Village/House15").queue_free()
	elif frame == 16:
		check(not has("World/Village/House16"), "disabled layer: its object left the tree")
		check(has("World/Village/House17"), "other objects unaffected by the layer")
		world.set_layer_enabled(0, true)
		cam.translation.x = cell_x(-8)
	elif frame == 20:
		check(not has("World/Cliff"), "large object unloaded once every cell it touches is out of range")
		check(has("World/Weather/Dust") and has("World/Trigger"), "logic nodes still loaded after moving far away")
		saved_state = world.get_world_state()
		check(saved_state.removed.has("Village/House15"), "freed object recorded as removed: %s" % str(saved_state.removed))
		check(saved_state.moved.has("Crate"), "dynamic object position recorded")
		cam.translation.x = cell_x(5)
	elif frame == 24:
		check(not has("World/Village/House15"), "removed object stays gone")
		check(has("World/Village/House16"), "re-enabled layer: object is back")
		world.queue_free()
	elif frame == 27:
		# A fresh copy of the world, given the saved state.
		world = make_world(false)
		add_crate(world, cell_x(0))
		add_child(world)
		world.set_world_state(saved_state)
	elif frame == 31:
		check(not has("World/Village/House15"), "state restored on a new world: House15 still removed")
		check(has("World/Village/House14"), "state restored: neighbours present")
		world.queue_free()
		next_stage()

# ---------------------------------------------------------------- bake
var level

func stage_bake():
	if frame == 2:
		print("== Bake: per-cell files, stripped scene, load / free from disk ==")
		cam.current = false
		var w = make_world(false)
		add_crate(w, cell_x(0))
		var ps = PackedScene.new()
		check(ps.pack(w) == OK, "authoring scene packed")
		check(ResourceSaver.save("res://level.tscn", ps) == OK, "authoring scene saved")
		w.free()
		level = load("res://level.tscn").instance()
		level.name = "Level"
		add_child(level)
	elif frame == 4:
		var err = level.bake_world()
		check(err == OK, "bake_world() returned OK (%d)" % err)
		var f = File.new()
		check(f.file_exists("res://level.world/world.scn"), "world.scn written")
		check(f.file_exists("res://level.world/cells/cell_0_0.scn"), "cell_0_0.scn written")
		check(f.file_exists("res://level.world/cells/cell_-10_0.scn") and f.file_exists("res://level.world/cells/cell_10_0.scn"), "edge cells written")
		check(level.get_node("Village").get_child_count() == 21, "authoring scene intact after bake (%d houses)" % level.get_node("Village").get_child_count())
		check(level.get_node("Village/House3").owner == level, "ownership restored after bake")
		check(level.has_bake(), "has_bake()")
		level.queue_free()
	elif frame == 7:
		level = load("res://level.world/world.scn").instance()
		level.name = "Level"
		check(level.get_node("Village").get_child_count() == 0, "world.scn holds no static objects")
		check(level.get_node_or_null("Crate") != null, "world.scn keeps the dynamic object")
		add_child(level)
		cam.translation = Vector3(cell_x(0), 10, 32)
		cam.current = true
	elif frame == 11:
		check(has("Level/Village/House10") and has("Level/Village/House9") and has("Level/Village/House11"), "cells -1..1 loaded from disk, under their folder, with their names")
		check(not has("Level/Village/House13"), "far cell not loaded")
		check(level.get_object_count() == 5, "only nearby objects exist in memory, plus the large one (got %d)" % level.get_object_count())
		check(has("Level/Cliff") and has("Level/Weather/Dust"), "large object and logic nodes live in world.scn")
		level.get_node("Village/House11").queue_free()
		cam.translation.x = cell_x(7)
	elif frame == 30:
		check(not has("Level/Village/House10"), "old cells freed")
		check(has("Level/Village/House17"), "new cell loaded from disk")
		cam.translation.x = cell_x(0)
	elif frame == 60:
		check(has("Level/Village/House10"), "cell reloaded from disk on return")
		check(not has("Level/Village/House11"), "object destroyed earlier is not rebuilt from the file")
		level.clear_bake()
		var f = File.new()
		check(not f.file_exists("res://level.world/world.scn"), "clear_bake() removed the files")
		level.queue_free()
		next_stage()

# ---------------------------------------------------------------- terrain
var crate
var sub = 0
var t0 = 0

func elapsed():
	return OS.get_ticks_msec() - t0

func stage_terrain():
	if sub == 0 and frame >= 2:
		print("== Exterior world: owned terrain + foliage, ground rule, has_terrain toggle ==")
		world = make_world(true)
		world.load_radius = 12
		crate = add_crate(world, cell_x(9))
		add_child(world)
		cam.translation = Vector3(cell_x(0), 10, 32)
		cam.current = true
		var t = world.get_terrain()
		var f = world.get_foliage()
		check(t != null and f != null, "has_terrain on: terrain and foliage created automatically")
		if t != null and f != null:
			check(t.cell_size == 64.0, "terrain on the base grid")
			check(f.cell_size == 256.0, "foliage on 4x the base grid (got %s)" % str(f.cell_size))
			check(t.world_dir == "user://gwtest_world" and f.world_dir == "user://gwtest_world", "world_dir pushed to both")
			check(f.get_node_or_null(f.terrain_manager_path) == t, "foliage wired to the terrain")
		sub = 1
		t0 = OS.get_ticks_msec()
	elif sub == 1 and elapsed() > 3000:
		var t = world.get_terrain()
		check(t.get_loaded_cell_count() > 0, "terrain streamed around the camera (%d cells)" % t.get_loaded_cell_count())
		check(t.has_collision_at_world(cam.translation), "collision under the camera")
		check(world.is_ground_ready(cam.translation), "is_ground_ready()")
		check(not t.has_collision_at_world(Vector3(cell_x(9), 0, 32)), "no collision 9 cells out")
		check(not crate.is_inside_tree(), "dynamic object parked where the ground has no collision")
		cam.translation.x = cell_x(9)
		sub = 2
		t0 = OS.get_ticks_msec()
	elif sub == 2 and elapsed() > 5000:
		check(world.get_terrain().has_collision_at_world(Vector3(cell_x(9), 0, 32)), "collision arrived under the crate")
		check(crate.is_inside_tree(), "dynamic object released once there is ground")
		world.has_terrain = false
		sub = 3
		t0 = OS.get_ticks_msec()
	elif sub == 3 and elapsed() > 500:
		var t = world.get_terrain()
		check(t != null and not t.visible, "has_terrain off: terrain kept but hidden")
		check(t.get_loaded_cell_count() == 0, "has_terrain off: terrain cells dropped (%d)" % t.get_loaded_cell_count())
		check(not world.get_foliage().visible, "has_terrain off: foliage hidden")
		world.queue_free()
		next_stage()
