extends Node3D

@onready var animation_player = $AnimationPlayer as AnimationPlayer


# Called when the node enters the scene tree for the first time.
func _ready():
	pass  # Replace with function body.


# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta: float):
	pass


func open_door():
	animation_player.play("door_opening")
