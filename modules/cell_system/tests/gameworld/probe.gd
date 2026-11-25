tool
extends Node

var f = 0

func dump(tag):
	var w = get_parent()
	print("PROBE ", tag, " ", w.name, " has_terrain=", w.has_terrain, " format=", w.format_version, " cell_size=", w.cell_size, " foliage_mult=", w.foliage_cell_multiple)
	for c in w.get_children():
		var o = "null"
		if c.owner:
			o = c.owner.name
		print("PROBE   child ", c.name, " (", c.get_class(), ") owner=", o, " children=", c.get_child_count())

func _process(_d):
	if not Engine.editor_hint:
		return
	f += 1
	if f == 40:
		dump("opened")
		var w = get_parent()
		if w.has_node("Cell_0_0"):
			w.migrate_legacy(w)
	elif f == 44:
		dump("after")
		get_tree().quit()
