extends Camera3D

# Simple orbit camera for the static globe demo. The Earth is centred at the ECEF
# origin (0,0,0) with radius ~6.378e6 m. Drag to rotate, wheel to zoom.

const EARTH_R := 6378137.0

@export var distance := 2.2e7
@export var min_distance := 6.6e6
@export var max_distance := 6.0e7
@export var auto_rotate := true
@export var auto_rotate_speed := 0.05 # rad/s

var _yaw := 0.6
var _pitch := 0.4
var _dragging := false
var _frames := 0

func _ready() -> void:
	near = 1000.0
	far = 2.0e8
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
		_yaw -= event.relative.x * 0.005
		_pitch = clamp(_pitch - event.relative.y * 0.005, -1.5, 1.5)
		_update_transform()

func _process(delta: float) -> void:
	if auto_rotate:
		_yaw += auto_rotate_speed * delta
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
	var dir := Vector3(
		cos(_pitch) * sin(_yaw),
		sin(_pitch),
		cos(_pitch) * cos(_yaw))
	var pos := dir * distance
	look_at_from_position(pos, Vector3.ZERO, Vector3.UP)
