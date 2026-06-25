#include "mudl_cli.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace mudl {
namespace {

void skip_spaces(const char*& cursor)
{
    while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) {
        ++cursor;
    }
}

std::optional<glm::vec3> parse_vec3_argument(const char* text)
{
    if (!text) {
        return std::nullopt;
    }

    glm::vec3 result{0.0f};
    const char* cursor = text;
    for (int i = 0; i < 3; ++i) {
        skip_spaces(cursor);
        char* end = nullptr;
        const float value = std::strtof(cursor, &end);
        if (end == cursor) {
            return std::nullopt;
        }
        result[i] = value;
        cursor = end;
        skip_spaces(cursor);
        if (i < 2) {
            if (*cursor != ',') {
                return std::nullopt;
            }
            ++cursor;
        }
    }

    skip_spaces(cursor);
    if (*cursor != '\0') {
        return std::nullopt;
    }
    return result;
}

std::optional<uint32_t> parse_positive_u32(const char*& cursor)
{
    skip_spaces(cursor);
    errno = 0;
    char* end = nullptr;
    const unsigned long value = std::strtoul(cursor, &end, 10);
    if (end == cursor || errno == ERANGE || value == 0
        || value > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }
    cursor = end;
    return static_cast<uint32_t>(value);
}

std::optional<ForwardPlusConfig> parse_forward_plus_config_argument(const char* text)
{
    if (!text) {
        return std::nullopt;
    }

    const char* cursor = text;
    const auto tile_size = parse_positive_u32(cursor);
    skip_spaces(cursor);
    if (!tile_size || *cursor != ',') {
        return std::nullopt;
    }
    ++cursor;

    const auto depth_slices = parse_positive_u32(cursor);
    skip_spaces(cursor);
    if (!depth_slices) {
        return std::nullopt;
    }

    uint32_t max_lights_per_cluster = 128u;
    if (*cursor == ',') {
        ++cursor;
        const auto max_lights = parse_positive_u32(cursor);
        if (!max_lights) {
            return std::nullopt;
        }
        max_lights_per_cluster = *max_lights;
        skip_spaces(cursor);
    }

    if (*cursor != '\0') {
        return std::nullopt;
    }

    return ForwardPlusConfig{
        .tile_size = *tile_size,
        .depth_slices = *depth_slices,
        .max_lights_per_cluster = max_lights_per_cluster,
    };
}

std::optional<AreaBenchmarkCameraMode> parse_area_benchmark_camera_mode(std::string_view value)
{
    if (value == "fit") {
        return AreaBenchmarkCameraMode::fit;
    }
    if (value == "gameplay") {
        return AreaBenchmarkCameraMode::gameplay;
    }
    if (value == "custom") {
        return AreaBenchmarkCameraMode::custom;
    }
    return std::nullopt;
}

std::optional<ForwardPlusDebugMode> parse_forward_plus_debug_mode(std::string_view value)
{
    if (value == "off") {
        return ForwardPlusDebugMode::off;
    }
    if (value == "cluster-lights" || value == "clusters" || value == "lights") {
        return ForwardPlusDebugMode::cluster_light_count;
    }
    if (value == "depth-slices" || value == "depth" || value == "slices") {
        return ForwardPlusDebugMode::depth_slice;
    }
    return std::nullopt;
}

bool is_help_argument(std::string_view value) noexcept
{
    return value == "--help" || value == "-h" || value == "help";
}

bool looks_like_option(std::string_view value) noexcept
{
    return !value.empty() && value.front() == '-';
}

std::string sanitize_output_component(std::string value)
{
    for (char& ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '_' && ch != '-' && ch != '.') {
            ch = '_';
        }
    }
    if (value.empty() || value == "." || value == "..") {
        return "turntable";
    }
    return value;
}

std::string default_turntable_output_dir(std::string_view source)
{
    const std::filesystem::path source_path{std::string{source}};
    std::string stem = source_path.stem().string();
    if (stem.empty()) {
        stem = source_path.filename().string();
    }
    return sanitize_output_component(std::move(stem)) + "-turntable";
}

} // namespace

void print_usage()
{
    std::cout << "mudl - Model Viewer & Asset Pipeline Tool\n\n"
              << "Usage:\n"
              << "  mudl [view] <resref> [--module <path>] [--animation <name>] [--pbr-environment <ktx>] [--no-pbr-ibl] [--legacy-nwn-model-path] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]\n"
              << "  mudl spell <spells.2da rowid> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]\n"
              << "  mudl spell-export <spells.2da rowid> <path> [--module <path>]\n"
              << "  mudl spell-export-live <spells.2da rowid> <path> [--module <path>]\n"
              << "  mudl spell-preview-live <spells.2da rowid> <path> [--module <path>] [--frame <count>] [--view front|top|side] [--metadata]\n"
              << "  mudl vfx <spell|resref|VFX_*|effect.json> [--stage proj|cast|conj|impact|duration|cessation] [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]\n"
              << "  mudl corpus <path/to/corpus.json> [--output <dir>] [--module <path>] [--pbr-environment <ktx>] [--no-pbr-ibl] [--limit <count>] [--filter <tag>] [--ledger <path>]\n"
              << "  mudl dump <resref> [--output <dir>] [--module <path>]\n"
              << "  mudl report <resref|path> [--module <path>] [--animation <name>]\n"
              << "  mudl stats <resref> [--module <path>]\n"
              << "  mudl texture <resref> <output_path> [--module <path>]\n"
              << "  mudl area <area_resref> [--module <path>] [--frames <count>] [--debug] [--validate]\n"
              << "  mudl area-benchmark <area_resref> [--module <path>] [--frames <count>] [--warmup <count>] [--width <px>] [--height <px>] [--camera fit|gameplay|custom] [--camera-position x,y,z] [--camera-target x,y,z] [--camera-fov degrees] [--area-time seconds] [--visible-tile-radius <tiles>] [--visible-tile-cone <tiles>] [--visible-tile-cone-angle degrees] [--no-lights] [--no-shadows] [--no-local-shadows] [--no-forward-plus] [--forward-plus-gpu-cull] [--no-forward-plus-gpu-cull] [--forward-plus-auto-config] [--forward-plus-config tile,depth[,max]] [--forward-plus-debug off|cluster-lights|depth-slices] [--screenshot <path>] [--debug] [--json <path>]\n"
              << "  mudl area-sweep <area_resref|area_list.txt> [--module <path>] [--frames <count>] [--warmup <count>] [--width <px>] [--height <px>] [--limit <count>] [--variants minimal|default|all] [--no-local-shadows] [--no-forward-plus] [--forward-plus-gpu-cull] [--no-forward-plus-gpu-cull] [--forward-plus-auto-config] [--forward-plus-config tile,depth[,max]] [--forward-plus-debug off|cluster-lights|depth-slices] [--debug] [--validate] [--json <path>]\n"
              << "  mudl area-lights <area_resref> [--module <path>]\n"
              << "  mudl area --dump <module_path> [--output <dir>] [--skip-existing] [--limit <count>] [--debug]\n"
              << "  mudl area-screenshot <area_resref> <path> [--module <path>] [--debug]\n"
              << "  mudl creature <resref> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]\n"
              << "  mudl item <resref> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]\n"
              << "  mudl particle-preview <resref> <path> [--module <path>] [--animation <name>] [--time <seconds>] [--view front|top|side] [--metadata]\n"
              << "  mudl particle-preview-frames <resref> <dir> [--module <path>] [--animation <name>] [--duration <seconds>] [--fps <count>] [--view front|top|side] [--metadata]\n"
              << "  mudl particle-export <resref> <path> [--module <path>] [--animation <name>]\n"
              << "  mudl frames <count> [<resref>] [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--validate]\n"
              << "  mudl screenshot <resref> <path> [--module <path>] [--animation <name>] [--legacy-nwn-model-path] [--dangly-scale <value>] [--dangly-mode legacy|modern]\n"
              << "  mudl turntable <resref> [frames] [--module <path>] [--output <dir>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern]\n"
              << "  mudl nwn-animation-smoke [--module <path>] [--validate]\n"
              << "  mudl compute-smoke [--validate]\n"
              << "\nturntable writes numbered PNG frames to --output, or <resref>-turntable by default.\n"
              << "--validate enables Vulkan validation layers (slow, useful for debugging).\n"
              << "--debug enables the optional debug grid overlay.\n"
              << "Forward+ GPU gather is default-on for local lights; --no-forward-plus-gpu-cull forces the CPU reference path.\n"
              << "--user <path> sets the NWN user directory root for development/override assets.\n"
              << "--pbr-environment <ktx> selects the HDR KTX source used for static PBR reference IBL.\n"
              << "--no-pbr-ibl skips generated static PBR IBL textures and uses shader fallback lighting.\n"
              << "--legacy-nwn-model-path forces NWN previews through legacy nwn::ModelInstance loading for compatibility checks.\n"
              << "Press F5 in the viewer to reload shaders and rebuild render pipelines.\n"
              << "Exterior areas with a day/night cycle run through a full 45 second sun/moon loop in the viewer.\n"
              << "Area lighting controls are available in the debug panel.\n"
              << "Spell/VFX controls: Space pause/play, . step one frame, / reset to frame 0.\n"
              << "--dangly-scale exaggerates or reduces dangly motion for tuning (default 1.0).\n"
              << "--dangly-mode selects legacy or modern foliage-style sway for dangly cards (default legacy).\n";
}

bool is_subcommand(std::string_view command)
{
    return command == "view"
        || command == "spell"
        || command == "spell-export"
        || command == "spell-export-live"
        || command == "spell-preview-live"
        || command == "vfx"
        || command == "corpus"
        || command == "dump"
        || command == "report"
        || command == "stats"
        || command == "texture"
        || command == "area"
        || command == "area-benchmark"
        || command == "area-sweep"
        || command == "area-lights"
        || command == "area-screenshot"
        || command == "creature"
        || command == "item"
        || command == "particle-preview"
        || command == "particle-preview-frames"
        || command == "particle-export"
        || command == "frames"
        || command == "screenshot"
        || command == "turntable"
        || command == "nwn-animation-smoke"
        || command == "compute-smoke"
        || command == "help"
        || command == "--help"
        || command == "-h";
}

std::optional<int> parse_args(int argc, char* argv[], ParsedArgs& out)
{
    int arg_start = 1;
    bool output_dir_explicit = false;
    out.command = argc >= 2 ? argv[1] : "";
    out.command_mode = is_subcommand(out.command);

    if (out.command_mode) {
        if (out.command == "view") {
            arg_start = 2;
        } else if (out.command == "spell") {
            arg_start = 3;
        } else if (out.command == "spell-export") {
            arg_start = 4;
        } else if (out.command == "spell-export-live") {
            arg_start = 4;
        } else if (out.command == "spell-preview-live") {
            arg_start = 4;
        } else if (out.command == "vfx") {
            arg_start = 3;
        } else if (out.command == "corpus") {
            arg_start = 3;
        } else if (out.command == "dump") {
            arg_start = 3;
        } else if (out.command == "report") {
            arg_start = 3;
        } else if (out.command == "stats") {
            arg_start = 3;
        } else if (out.command == "texture") {
            arg_start = 4;
        } else if (out.command == "area") {
            arg_start = 3;
        } else if (out.command == "area-benchmark") {
            arg_start = 3;
        } else if (out.command == "area-sweep") {
            arg_start = 3;
        } else if (out.command == "area-lights") {
            arg_start = 3;
        } else if (out.command == "area-screenshot") {
            arg_start = 4;
        } else if (out.command == "creature" || out.command == "item") {
            arg_start = 3;
        } else if (out.command == "particle-preview") {
            arg_start = 4;
        } else if (out.command == "particle-preview-frames") {
            arg_start = 4;
        } else if (out.command == "particle-export") {
            arg_start = 4;
        } else if (out.command == "frames") {
            arg_start = 3;
        } else if (out.command == "screenshot") {
            arg_start = 4;
        } else if (out.command == "turntable") {
            arg_start = 3;
        } else if (out.command == "nwn-animation-smoke") {
            arg_start = 2;
        } else if (out.command == "compute-smoke") {
            arg_start = 2;
        }
        if (out.command == "help" || out.command == "--help" || out.command == "-h") {
            print_usage();
            return 0;
        }
    }

    if (out.command == "spell") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
    } else if (out.command == "spell-export-live") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.live_export_output_path = argv[3];
    } else if (out.command == "spell-preview-live") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.screenshot_path = argv[3];
    } else if (out.command == "spell-export") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.screenshot_path = argv[3];
    } else if (out.command == "vfx") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
    } else if (out.command == "corpus") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.corpus_path = argv[2];
    } else if (out.command == "dump") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
    } else if (out.command == "report") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
    } else if (out.command == "stats") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
    } else if (out.command == "area") {
        if (argc >= 4 && std::string_view(argv[2]) == "--dump") {
            out.area_dump_module_path = argv[3];
            arg_start = 4;
        } else if (argc < 3) {
            print_usage();
            return 1;
        } else {
            out.initial_model = argv[2];
        }
    } else if (out.command == "area-benchmark") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
    } else if (out.command == "area-sweep") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.benchmark_frames = 2;
        out.benchmark_warmup_frames = 0;
    } else if (out.command == "area-lights") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
    } else if (out.command == "area-screenshot") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.screenshot_path = argv[3];
    } else if (out.command == "texture") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.texture_output_path = argv[3];
    } else if (out.command == "frames") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.max_frames = std::max(0, std::atoi(argv[2]));
    } else if (out.command == "creature" || out.command == "item") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
    } else if (out.command == "particle-preview") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.screenshot_path = argv[3];
    } else if (out.command == "particle-preview-frames") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.output_dir = argv[3];
    } else if (out.command == "particle-export") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.screenshot_path = argv[3];
    } else if (out.command == "screenshot") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.screenshot_path = argv[3];
        out.max_frames = 4;
    } else if (out.command == "turntable") {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        const std::string_view model_arg = argv[2];
        if (is_help_argument(model_arg)) {
            print_usage();
            return 0;
        }
        if (looks_like_option(model_arg)) {
            std::cerr << "turntable requires a model resref or path before options.\n";
            print_usage();
            return 1;
        }
        out.initial_model = argv[2];
        out.turntable_mode = true;
        if (argc >= 4 && argv[3][0] != '-') {
            out.turntable_frames = std::max(1, std::atoi(argv[3]));
            arg_start = 4;
        }
    }

    for (int i = arg_start; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--output" && i + 1 < argc && out.command == "area" && !out.area_dump_module_path.empty()) {
            out.area_dump_output_path = argv[++i];
        } else if (arg == "--skip-existing" && out.command == "area" && !out.area_dump_module_path.empty()) {
            out.area_dump_skip_existing = true;
        } else if (arg == "--limit" && i + 1 < argc && out.command == "area" && !out.area_dump_module_path.empty()) {
            out.area_dump_limit = static_cast<size_t>(std::max(0, std::atoi(argv[++i])));
        } else if (arg == "--frames" && i + 1 < argc && out.command == "area") {
            out.max_frames = std::max(0, std::atoi(argv[++i]));
        } else if (arg == "--frames" && i + 1 < argc
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            out.benchmark_frames = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--warmup" && i + 1 < argc
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            out.benchmark_warmup_frames = std::max(0, std::atoi(argv[++i]));
        } else if (arg == "--width" && i + 1 < argc
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            out.benchmark_width = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            out.benchmark_height = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--camera" && i + 1 < argc && out.command == "area-benchmark") {
            const auto mode = parse_area_benchmark_camera_mode(argv[++i]);
            if (!mode) {
                std::cerr << "Invalid area benchmark camera mode. Use fit, gameplay, or custom.\n";
                return 1;
            }
            out.benchmark_camera.mode = *mode;
        } else if ((arg == "--camera-position" || arg == "--camera-pos") && i + 1 < argc
            && out.command == "area-benchmark") {
            const auto position = parse_vec3_argument(argv[++i]);
            if (!position) {
                std::cerr << "Invalid --camera-position. Expected x,y,z.\n";
                return 1;
            }
            out.benchmark_camera.position = *position;
            if (out.benchmark_camera.mode == AreaBenchmarkCameraMode::fit) {
                out.benchmark_camera.mode = AreaBenchmarkCameraMode::custom;
            }
        } else if (arg == "--camera-target" && i + 1 < argc && out.command == "area-benchmark") {
            const auto target = parse_vec3_argument(argv[++i]);
            if (!target) {
                std::cerr << "Invalid --camera-target. Expected x,y,z.\n";
                return 1;
            }
            out.benchmark_camera.target = *target;
            if (out.benchmark_camera.mode == AreaBenchmarkCameraMode::fit) {
                out.benchmark_camera.mode = AreaBenchmarkCameraMode::custom;
            }
        } else if (arg == "--camera-fov" && i + 1 < argc && out.command == "area-benchmark") {
            out.benchmark_camera.fov_degrees = std::clamp(std::strtof(argv[++i], nullptr), 10.0f, 120.0f);
        } else if (arg == "--area-time" && i + 1 < argc && out.command == "area-benchmark") {
            out.benchmark_area_time_seconds = std::max(0.0f, std::strtof(argv[++i], nullptr));
        } else if (arg == "--visible-tile-radius" && i + 1 < argc && out.command == "area-benchmark") {
            out.benchmark_visible_tile_radius = std::max(0, std::atoi(argv[++i]));
            out.benchmark_visibility_mode = AreaBenchmarkVisibilityMode::radius;
        } else if (arg == "--visible-tile-cone" && i + 1 < argc && out.command == "area-benchmark") {
            out.benchmark_visible_tile_radius = std::max(0, std::atoi(argv[++i]));
            out.benchmark_visibility_mode = AreaBenchmarkVisibilityMode::view_cone;
        } else if (arg == "--visible-tile-cone-angle" && i + 1 < argc && out.command == "area-benchmark") {
            out.benchmark_visible_tile_cone_half_angle = std::clamp(std::strtof(argv[++i], nullptr), 1.0f, 179.0f);
        } else if (arg == "--no-lights" && out.command == "area-benchmark") {
            out.benchmark_lights_enabled = false;
        } else if (arg == "--no-shadows" && out.command == "area-benchmark") {
            out.benchmark_shadows_enabled = false;
        } else if (arg == "--no-local-shadows"
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            out.benchmark_local_shadows_enabled = false;
        } else if (arg == "--json" && i + 1 < argc
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            out.benchmark_output_path = argv[++i];
        } else if (arg == "--screenshot" && i + 1 < argc && out.command == "area-benchmark") {
            out.benchmark_screenshot_path = argv[++i];
        } else if (arg == "--no-forward-plus"
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            out.benchmark_forward_plus_policy.enabled = false;
        } else if (arg == "--forward-plus-gpu-cull"
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            out.benchmark_forward_plus_policy.gpu_culling = true;
        } else if (arg == "--no-forward-plus-gpu-cull"
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            out.benchmark_forward_plus_policy.gpu_culling = false;
        } else if (arg == "--forward-plus-auto-config"
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            out.benchmark_forward_plus_policy.auto_configure_area = true;
        } else if (arg == "--forward-plus-config" && i + 1 < argc
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            const auto config = parse_forward_plus_config_argument(argv[++i]);
            if (!config) {
                std::cerr << "Invalid Forward+ config. Expected tile_size,depth_slices[,max_lights_per_cluster].\n";
                return 1;
            }
            out.benchmark_forward_plus_policy.config = *config;
            out.benchmark_forward_plus_policy.auto_configure_area = false;
        } else if (arg == "--forward-plus-debug" && i + 1 < argc
            && (out.command == "area-benchmark" || out.command == "area-sweep")) {
            const auto mode = parse_forward_plus_debug_mode(argv[++i]);
            if (!mode) {
                std::cerr << "Invalid Forward+ debug mode. Use off, cluster-lights, or depth-slices.\n";
                return 1;
            }
            out.benchmark_forward_plus_policy.debug_mode = *mode;
        } else if (arg == "--limit" && i + 1 < argc && out.command == "area-sweep") {
            out.area_sweep_limit = static_cast<size_t>(std::max(0, std::atoi(argv[++i])));
        } else if (arg == "--variants" && i + 1 < argc && out.command == "area-sweep") {
            out.area_sweep_variants = argv[++i];
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            out.output_dir = argv[++i];
            output_dir_explicit = true;
        } else if (arg == "--output" || arg == "-o") {
            std::cerr << "--output requires a directory.\n";
            return 1;
        } else if (arg == "--module" && i + 1 < argc) {
            out.module_path = argv[++i];
        } else if (arg == "--user" && i + 1 < argc) {
            out.user_path = argv[++i];
        } else if (arg == "--pbr-environment" && i + 1 < argc) {
            out.pbr_environment_path = argv[++i];
        } else if (arg == "--pbr-environment") {
            std::cerr << "--pbr-environment requires a KTX path.\n";
            return 1;
        } else if (arg == "--no-pbr-ibl") {
            out.pbr_ibl_enabled = false;
        } else if (arg == "--legacy-gltf-animation-state" || arg == "--unified-gltf-animation-state") {
            std::cerr << arg << " was removed; RenderModel animation state is always stored per instance.\n";
            return 1;
        } else if (arg == "--legacy-gltf-model-path" || arg == "--model-asset-gltf-model-path") {
            std::cerr << arg << " was removed; glTF previews always use ModelAsset -> RenderModel.\n";
            return 1;
        } else if (arg == "--legacy-render-model-draws" || arg == "--prepared-render-model-draws") {
            std::cerr << arg << " was removed; RenderModel surfaces always use the common prepared draw path.\n";
            return 1;
        } else if (arg == "--legacy-nwn-draws" || arg == "--prepared-nwn-draws") {
            std::cerr << arg << " was removed; non-area NWN sidecars always use the common prepared draw path.\n";
            return 1;
        } else if (arg == "--legacy-nwn-model-path") {
            out.legacy_nwn_model_path = true;
        } else if (arg == "--render-model-nwn-model-path") {
            std::cerr << arg << " was removed; NWN previews use ModelAsset -> RenderModel by default.\n";
            return 1;
        } else if (arg == "--limit" && i + 1 < argc && out.command == "corpus") {
            out.corpus_limit = static_cast<size_t>(std::max(0, std::atoi(argv[++i])));
        } else if (arg == "--filter" && i + 1 < argc && out.command == "corpus") {
            out.corpus_filter = argv[++i];
        } else if (arg == "--ledger" && i + 1 < argc && out.command == "corpus") {
            out.corpus_ledger_path = argv[++i];
        } else if (arg == "--validate") {
            out.enable_validation = true;
        } else if (arg == "--debug") {
            out.show_debug_overlay = true;
        } else if (arg == "--animation" && i + 1 < argc) {
            out.animation_override = argv[++i];
        } else if (arg == "--stage" && i + 1 < argc && out.command == "vfx") {
            out.vfx_stage = argv[++i];
        } else if (arg == "--dangly-scale" && i + 1 < argc) {
            out.dangly_scale = std::max(0.0f, std::strtof(argv[++i], nullptr));
        } else if (arg == "--dangly-mode" && i + 1 < argc) {
            const std::string mode = argv[++i];
            out.dangly_mode = mode == "modern" ? DanglyMode::modern : DanglyMode::legacy;
        } else if (arg == "--time" && i + 1 < argc && out.command == "particle-preview") {
            out.particle_preview_time = std::max(0.0f, std::strtof(argv[++i], nullptr));
        } else if (arg == "--duration" && i + 1 < argc && out.command == "particle-preview-frames") {
            out.particle_preview_duration = std::max(0.0f, std::strtof(argv[++i], nullptr));
        } else if (arg == "--fps" && i + 1 < argc && out.command == "particle-preview-frames") {
            out.particle_preview_fps = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--frame" && i + 1 < argc && out.command == "spell-preview-live") {
            out.particle_preview_frame = std::max(0, std::atoi(argv[++i]));
        } else if (arg == "--metadata"
            && (out.command == "particle-preview" || out.command == "particle-preview-frames"
                || out.command == "spell-preview-live")) {
            out.particle_preview_metadata.write = true;
        } else if (arg == "--view" && i + 1 < argc
            && (out.command == "particle-preview" || out.command == "particle-preview-frames"
                || out.command == "spell-preview-live")) {
            const std::string view = argv[++i];
            if (view == "top") {
                out.particle_preview_view = ParticlePreviewView::top;
            } else if (view == "side") {
                out.particle_preview_view = ParticlePreviewView::side;
            } else {
                out.particle_preview_view = ParticlePreviewView::front;
            }
        } else if (arg[0] != '-' && out.initial_model.empty()) {
            out.initial_model = arg;
        }
    }

    if (out.command == "turntable" && !output_dir_explicit) {
        out.output_dir = default_turntable_output_dir(out.initial_model);
    }

    if (out.command == "area-benchmark"
        && out.benchmark_camera.mode == AreaBenchmarkCameraMode::custom
        && !out.benchmark_camera.position
        && !out.benchmark_camera.target) {
        std::cerr << "Area benchmark custom camera needs --camera-position or --camera-target.\n";
        return 1;
    }

    return std::nullopt;
}

} // namespace mudl
