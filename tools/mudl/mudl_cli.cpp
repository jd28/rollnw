#include "mudl_cli.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace mudl {

void print_usage()
{
    std::cout << "mudl - Model Viewer & Asset Pipeline Tool\n\n"
              << "Usage:\n"
              << "  mudl [view] <resref> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]\n"
              << "  mudl spell <spells.2da rowid> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]\n"
              << "  mudl spell-export <spells.2da rowid> <path> [--module <path>]\n"
              << "  mudl spell-export-live <spells.2da rowid> <path> [--module <path>]\n"
              << "  mudl spell-preview-live <spells.2da rowid> <path> [--module <path>] [--frame <count>] [--view front|top|side] [--metadata]\n"
              << "  mudl vfx <spell|resref|VFX_*|effect.json> [--stage proj|cast|conj|impact|duration|cessation] [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]\n"
              << "  mudl corpus <path/to/corpus.json> [--output <dir>] [--module <path>] [--limit <count>] [--filter <tag>] [--ledger <path>]\n"
              << "  mudl dump <resref> [--output <dir>] [--module <path>]\n"
              << "  mudl stats <resref> [--module <path>]\n"
              << "  mudl texture <resref> <output_path> [--module <path>]\n"
              << "  mudl area <area_resref> [--module <path>] [--debug] [--validate]\n"
              << "  mudl area --dump <module_path> [--output <dir>] [--skip-existing] [--limit <count>] [--debug]\n"
              << "  mudl area-screenshot <area_resref> <path> [--module <path>] [--debug]\n"
              << "  mudl creature <resref> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]\n"
              << "  mudl item <resref> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]\n"
              << "  mudl particle-preview <resref> <path> [--module <path>] [--animation <name>] [--time <seconds>] [--view front|top|side] [--metadata]\n"
              << "  mudl particle-preview-frames <resref> <dir> [--module <path>] [--animation <name>] [--duration <seconds>] [--fps <count>] [--view front|top|side] [--metadata]\n"
              << "  mudl particle-export <resref> <path> [--module <path>] [--animation <name>]\n"
              << "  mudl frames <count> [<resref>] [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--validate]\n"
              << "  mudl screenshot <resref> <path> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern]\n"
              << "  mudl turntable <resref> [frames] [--module <path>] [--output <dir>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern]\n"
              << "  mudl nwn-animation-smoke [--module <path>] [--validate]\n"
              << "  mudl compute-smoke [--validate]\n"
              << "\nturntable writes numbered PNG frames for external tools like ffmpeg.\n"
              << "--validate enables Vulkan validation layers (slow, useful for debugging).\n"
              << "--debug enables the optional debug grid overlay.\n"
              << "Press F5 in the viewer to reload shaders and rebuild render pipelines.\n"
              << "Exterior areas with a day/night cycle run through a full 60 second sun/moon loop in the viewer.\n"
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
        || command == "stats"
        || command == "texture"
        || command == "area"
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
        } else if (out.command == "stats") {
            arg_start = 3;
        } else if (out.command == "texture") {
            arg_start = 4;
        } else if (out.command == "area") {
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
        out.initial_model = argv[2];
        out.turntable_mode = true;
        if (argc >= 4 && argv[3][0] != '-') {
            out.turntable_frames = std::max(1, std::atoi(argv[3]));
            arg_start = 4;
        }
    }

    for (int i = arg_start; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc && out.command == "area" && !out.area_dump_module_path.empty()) {
            out.area_dump_output_path = argv[++i];
        } else if (arg == "--skip-existing" && out.command == "area" && !out.area_dump_module_path.empty()) {
            out.area_dump_skip_existing = true;
        } else if (arg == "--limit" && i + 1 < argc && out.command == "area" && !out.area_dump_module_path.empty()) {
            out.area_dump_limit = static_cast<size_t>(std::max(0, std::atoi(argv[++i])));
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            out.output_dir = argv[++i];
        } else if (arg == "--module" && i + 1 < argc) {
            out.module_path = argv[++i];
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
            out.dangly_mode = mode == "modern" ? nw::render::nwn::DanglyMode::modern : nw::render::nwn::DanglyMode::legacy;
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

    return std::nullopt;
}

} // namespace mudl
