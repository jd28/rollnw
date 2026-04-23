#pragma once

#include "mudl_cli.hpp"
#include "mudl_commands.hpp"
#include "app_runtime.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace mudl {

int run_particle_preview_command(const std::string& model_spec, const std::filesystem::path& out_path,
    float preview_time, ParticlePreviewView view, std::string_view animation_name,
    const ParticlePreviewMetadataOptions& metadata);

int run_particle_preview_frames_command(const std::string& model_spec, const std::filesystem::path& out_dir,
    float duration, int fps, ParticlePreviewView view, std::string_view animation_name,
    const ParticlePreviewMetadataOptions& metadata);

int run_particle_export_command(const std::string& model_spec, const std::filesystem::path& out_path,
    std::string_view animation_name);

int run_spell_export_command(const VfxSequence& sequence, std::string_view spell_id,
    const std::filesystem::path& out_path);

int run_spell_export_live_command(AppState& state, const VfxSequence& sequence, std::string_view spell_id,
    const std::filesystem::path& out_path);

int run_spell_preview_live_command(AppState& state, const VfxSequence& sequence, std::string_view spell_id,
    const std::filesystem::path& out_path, int frame_index, ParticlePreviewView view,
    const ParticlePreviewMetadataOptions& metadata);

} // namespace mudl
