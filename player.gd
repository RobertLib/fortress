extends CharacterBody3D

# Character movement parameters.
const WALK_SPEED := 5.0
const SPRINT_SPEED := 8.0
const JUMP_VELOCITY := 4.5
const SENSITIVITY := 0.003

# Headbobbing parameters.
const BOB_FREQ := 2.0
const BOB_AMP := 0.08

# Camera parameters.
const BASE_FOV := 75.0
const FOV_CHANGE := 1.5

var speed := 0.0
var t_bob := 0.0

# Get the gravity from the project settings to be synced with RigidBody nodes.
var gravity: float = ProjectSettings.get_setting("physics/3d/default_gravity")

# Bullets.
var bullet := preload("res://bullet.tscn")
var bullet_instance: Bullet

@onready var head := $Head as Node3D
@onready var camera := $Head/Camera3D as Camera3D
@onready var gun_anim := $Head/Camera3D/Gun/AnimationPlayer as AnimationPlayer
@onready var gun_barrel := $Head/Camera3D/Gun/RayCast3D as RayCast3D


func _ready():
	# Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)
	pass


func _unhandled_input(event: InputEvent):
	if event is InputEventMouseMotion:
		head.rotate_y(-event.relative.x * SENSITIVITY)
		camera.rotate_x(-event.relative.y * SENSITIVITY)
		camera.rotation.x = clampf(camera.rotation.x, deg_to_rad(-40), deg_to_rad(60))


func _physics_process(delta: float):
	# Add the gravity.
	if not is_on_floor():
		velocity.y -= gravity * delta

	# Handle jump.
	if Input.is_action_just_pressed("jump") and is_on_floor():
		velocity.y = JUMP_VELOCITY

	# Handle sprint.
	if Input.is_action_pressed("sprint"):
		speed = SPRINT_SPEED
	else:
		speed = WALK_SPEED

	# Get the input direction and handle the movement/deceleration.
	# As good practice, you should replace UI actions with custom gameplay actions.
	var input_dir := Input.get_vector("left", "right", "up", "down")
	var direction := (head.transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()

	if is_on_floor():
		if direction:
			velocity.x = direction.x * speed
			velocity.z = direction.z * speed
		else:
			velocity.x = lerp(velocity.x, direction.x * speed, 7.0 * delta)
			velocity.z = lerp(velocity.z, direction.z * speed, 7.0 * delta)
	else:
		velocity.x = lerp(velocity.x, direction.x * speed, 3.0 * delta)
		velocity.z = lerp(velocity.z, direction.z * speed, 3.0 * delta)

	# Apply headbob.
	t_bob += velocity.length() * float(is_on_floor()) * delta
	camera.transform.origin = _headbob(t_bob)

	# Apply FOV change based on the player's speed.
	var velocity_clamped := clampf(velocity.length(), 0.5, SPRINT_SPEED * 2)
	var target_fov := BASE_FOV + FOV_CHANGE * velocity_clamped
	camera.fov = lerp(camera.fov, target_fov, delta * 8.0)

	# Shoot the gun.
	if Input.is_action_pressed("shoot"):
		if !gun_anim.is_playing():
			gun_anim.play("shoot")
			bullet_instance = bullet.instantiate()
			bullet_instance.position = gun_barrel.global_position
			bullet_instance.transform.basis = gun_barrel.global_transform.basis
			get_parent().add_child(bullet_instance)

	# Check for interaction.
	if Input.is_action_just_pressed("action"):
		_check_and_open_door()

	move_and_slide()


func _headbob(time: float) -> Vector3:
	var pos := Vector3.ZERO
	pos.y = sin(time * BOB_FREQ) * BOB_AMP
	pos.x = cos(time * BOB_FREQ / 2) * BOB_AMP
	return pos


func _check_and_open_door():
	var space_state := get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.new()

	query.from = camera.global_transform.origin
	query.to = camera.global_transform.origin - camera.global_transform.basis.z * 2.0
	query.collision_mask = 1

	var result := space_state.intersect_ray(query)

	if result and result.collider:
		var collider = result.collider

		while collider:
			if collider.has_method("open_door"):
				collider.open_door()
				break

			collider = collider.get_parent()
