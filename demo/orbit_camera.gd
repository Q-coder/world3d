extends Camera3D

# Turntable orbit camera for the globe demo. The Earth is centred at the ECEF
# origin (0,0,0); ECEF +Z is the true north pole, so the camera keeps north up.
# Left-drag grabs and rotates the globe; wheel zooms.

const EARTH_R := 6378137.0

@export var distance := 2.2e7
@export var min_distance := 6.6e6
@export var max_distance := 6.0e7
@export var auto_rotate := true
@export var auto_rotate_speed := 0.05 # rad/s

# _lon = longitude around the north (Z) axis, _lat = latitude from the equator.
var _lon := 0.6
var _lat := 0.4
var _dragging := false
var _frames := 0

# Fly-down profiler state (opt-in via W3D_FLY).
var _fly := false
var _fly_t := 0.0
var _fly_dur := 10.0
var _fly_from := 3.0e7
var _fly_to := 6.55e6
var _fly_frames := 0
var _fly_ms_sum := 0.0
var _fly_ms_max := 0.0
var _fly_jank := 0

func _ready() -> void:
	near = 1000.0
	far = 2.0e8
	var d := OS.get_environment("W3D_DIST")
	if d != "":
		distance = float(d)
		near = clamp(distance * 0.001, 10.0, 1000.0)
	if OS.get_environment("W3D_FLY") != "":
		_fly = true
		auto_rotate = false
		distance = _fly_from
		Engine.max_fps = 0
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	if OS.get_environment("W3D_MASKTEST") == "2":
		# Step C check: mask in LV95 grid metres while the globe sits in ECEF —
		# a 3x3 checkerboard of 300 km LV95 cells around Bern must show up as
		# holes aligned with the Swiss grid (rotated vs ECEF axes).
		var globe := get_node_or_null("../World3D/Globe")
		if globe:
			var cells := PackedByteArray([255, 0, 255,  0, 255, 0,  255, 0, 255])
			var cell := 300000.0
			globe.set_coverage_mask_frame(1)
			globe.set_coverage_mask(cells, 3, 2600000.0 - 1.5 * cell, 1200000.0 - 1.5 * cell, cell, cell)
			print("[demo] W3D_MASKTEST=2 LV95 mask uploaded; bern suppressed=", globe.suppresses_ecef_point(Vector3(4324840.06, 564737.18, 4638154.67)))
	elif OS.get_environment("W3D_MASKTEST") != "":
		# Verify the per-fragment coverage mask: a 3x3 checkerboard of 200 km
		# cells in ECEF XZ centred on Bern must punch holes in the globe.
		var globe := get_node_or_null("../World3D/Globe")
		if globe:
			var lat0 := deg_to_rad(46.9524)
			var lon0 := deg_to_rad(7.4396)
			var bern := Vector3(cos(lat0) * cos(lon0), cos(lat0) * sin(lon0), sin(lat0)) * 6371000.0
			var cells := PackedByteArray([255, 0, 255,  0, 255, 0,  255, 0, 255])
			var cell := 200000.0
			globe.set_coverage_mask(cells, 3, bern.x - 1.5 * cell, bern.z - 1.5 * cell, cell, cell)
			print("[demo] W3D_MASKTEST mask uploaded (size=", globe.get_coverage_mask_size(), ")")
	if OS.get_environment("W3D_VIEW") == "bern":
		# Look straight down the Bern direction so Switzerland is centred, with
		# Earth's north up — used to verify the tangent patch lands correctly.
		auto_rotate = false
		var lat := deg_to_rad(46.9524)
		var lon := deg_to_rad(7.4396)
		var dir := Vector3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat))
		var vd := 1.4e7
		var dd := OS.get_environment("W3D_DIST")
		if dd != "":
			vd = float(dd)
		near = clamp(vd * 0.001, 10.0, 1000.0)
		look_at_from_position(dir * vd, Vector3.ZERO, Vector3(0, 0, 1))
		return
	_update_transform()

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_dragging = event.pressed
			auto_rotate = false
		elif event.button_index == MOUSE_BUTTON_WHEEL_UP:
			distance = max(min_distance, distance * 0.9)
			_update_transform()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			distance = min(max_distance, distance * 1.1)
			_update_transform()
	elif event is InputEventMouseMotion and _dragging:
		# Grab-style: the point under the cursor follows the mouse.
		var k := distance / EARTH_R * 0.004 # scale sensitivity with zoom
		k = clamp(k, 0.0015, 0.02)
		_lon -= event.relative.x * k
		_lat = clamp(_lat + event.relative.y * k, -1.4, 1.4)
		_update_transform()

func _process(delta: float) -> void:
	if _fly:
		_fly_t += delta
		var t: float = clamp(_fly_t / _fly_dur, 0.0, 1.0)
		# Ease toward the surface; keep rotating so new chunks stream in constantly.
		distance = lerp(_fly_from, _fly_to, t * t)
		near = clamp(distance * 0.001, 10.0, 1000.0)
		_lon += 0.15 * delta
		_update_transform()
		# Skip the first few frames (initial load) before profiling.
		if _fly_t > 0.5:
			var ms := delta * 1000.0
			_fly_frames += 1
			_fly_ms_sum += ms
			_fly_ms_max = max(_fly_ms_max, ms)
			if ms > 33.0:
				_fly_jank += 1
		if t >= 1.0:
			var avg: float = _fly_ms_sum / float(max(1, _fly_frames))
			print("[world3d] FLY profile: frames=%d avg=%.2fms max=%.2fms jank(>33ms)=%d" % [
				_fly_frames, avg, _fly_ms_max, _fly_jank])
			get_tree().quit()
		return

	if auto_rotate:
		_lon += auto_rotate_speed * delta
		_update_transform()

	# Opt-in one-shot screenshot for automated verification.
	var shot := OS.get_environment("W3D_SHOT")
	if shot != "":
		_frames += 1
		if _frames == 45:
			await RenderingServer.frame_post_draw
			var img := get_viewport().get_texture().get_image()
			img.save_png(shot)
			get_tree().quit()

func _update_transform() -> void:
	# Parameterise by geographic longitude/latitude so ECEF north (+Z) stays up.
	var dir := Vector3(
		cos(_lat) * cos(_lon),
		cos(_lat) * sin(_lon),
		sin(_lat))
	var pos := dir * distance
	look_at_from_position(pos, Vector3.ZERO, Vector3(0, 0, 1))
