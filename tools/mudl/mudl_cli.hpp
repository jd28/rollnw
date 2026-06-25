#pragma once

#include "mudl_commands.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mudl {

enum class ParticlePreviewView {
    front,
    top,
    side,
};

struct ParticlePreviewMetadataOptions {
    bool write = false;
};

enum class ForwardPlusDebugMode {
    off,
    cluster_light_count,
    depth_slice,
};

struct ForwardPlusConfig {
    uint32_t tile_size = 64;
    uint32_t depth_slices = 8;
    uint32_t max_lights_per_cluster = 128;
};

struct ForwardPlusRenderPolicy {
    bool enabled = true;
    bool auto_configure_area = true;
    bool gpu_culling = true;
    ForwardPlusConfig config{};
    ForwardPlusDebugMode debug_mode = ForwardPlusDebugMode::off;
};

enum class DanglyMode {
    legacy,
    modern,
};

struct ParsedArgs {
    std::string command;
    bool command_mode = false;

    std::string initial_model;
    std::string texture_output_path;
    std::string area_dump_module_path;
    std::string area_dump_output_path;
    bool area_dump_skip_existing = false;
    size_t area_dump_limit = 0;
    bool turntable_mode = false;
    int turntable_frames = 36;
    std::string output_dir = ".";
    int max_frames = 0;
    std::string screenshot_path;
    std::string animation_override;
    std::string vfx_stage;
    std::string corpus_filter;
    std::string corpus_path;
    std::string corpus_ledger_path;
    std::string module_path;
    std::string user_path;
    std::string pbr_environment_path;
    bool pbr_ibl_enabled = true;
    bool legacy_nwn_model_path = false;
    std::string live_export_output_path;
    std::string benchmark_output_path;
    std::string benchmark_screenshot_path;
    std::string area_sweep_variants = "default";
    size_t area_sweep_limit = 0;
    bool enable_validation = false;
    bool show_debug_overlay = false;
    bool benchmark_lights_enabled = true;
    bool benchmark_shadows_enabled = true;
    bool benchmark_local_shadows_enabled = true;
    ForwardPlusRenderPolicy benchmark_forward_plus_policy;
    AreaBenchmarkCameraOptions benchmark_camera;
    size_t corpus_limit = 0;
    int benchmark_frames = 60;
    int benchmark_warmup_frames = 5;
    int benchmark_width = 1280;
    int benchmark_height = 720;
    int benchmark_visible_tile_radius = -1;
    float benchmark_visible_tile_cone_half_angle = 75.0f;
    std::optional<float> benchmark_area_time_seconds;
    AreaBenchmarkVisibilityMode benchmark_visibility_mode = AreaBenchmarkVisibilityMode::radius;
    float dangly_scale = 1.0f;
    DanglyMode dangly_mode = DanglyMode::legacy;
    float particle_preview_time = 1.0f;
    float particle_preview_duration = 1.0f;
    int particle_preview_fps = 24;
    int particle_preview_frame = 0;
    ParticlePreviewView particle_preview_view = ParticlePreviewView::front;
    ParticlePreviewMetadataOptions particle_preview_metadata;
};

void print_usage();
bool is_subcommand(std::string_view command);
std::optional<int> parse_args(int argc, char* argv[], ParsedArgs& out);

} // namespace mudl
