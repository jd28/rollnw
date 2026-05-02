#pragma once

#include "app_runtime.hpp"
#include "mudl_commands.hpp"

#include <SDL3/SDL_events.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace mudl {

void load_model(AppState& state, std::string_view resref);
void load_vfx_sequence(AppState& state, const VfxSequence& sequence);
void load_area(AppState& state, std::string_view resref);
void update_viewer(AppState& state, float dt);
void handle_key_down(AppState& state, const SDL_KeyboardEvent& key);
void toggle_scene_playback(AppState& state);
void stop_scene_playback(AppState& state, bool log_action = true);
void reset_scene_playback(AppState& state);
void step_scene_playback(AppState& state, int32_t dt_ms = 33);
void render_frame(AppState& state);
void set_vfx_sequence_time(AppState& state, int time_ms, bool stop_after_seek = true);
bool has_area_day_night_controls(const AppState& state);
bool supports_gltf_animation_controls(const AppState& state);
bool supports_vfx_sequence_controls(const AppState& state);
bool scene_uses_shared_nwn_animation_source(const PreviewScene& scene);
void set_area_day_night_elapsed_seconds(AppState& state, float elapsed_seconds, bool log_transition = false);
void reset_area_day_night_cycle(AppState& state);
void reload_current_scene(AppState& state);
bool select_model_animation(AppState& state, size_t model_index, std::string_view animation_name);
void set_gltf_animation_clip(AppState& state, uint32_t clip_index);
int run_area_dump_command(const std::filesystem::path& module_path, const std::filesystem::path& output_dir,
    std::string_view user_path, bool skip_existing, size_t limit, bool show_debug_overlay);

} // namespace mudl
