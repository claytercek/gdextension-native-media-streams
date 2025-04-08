#pragma once

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

// Main module initialization functions
void initialize_native_media_streams_module(ModuleInitializationLevel p_level);
void uninitialize_native_media_streams_module(ModuleInitializationLevel p_level);