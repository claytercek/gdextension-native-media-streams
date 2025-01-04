extends Node

@onready var label: Label = $Label

func _ready() -> void:
	label.text = "Hello GDScript!\n"
