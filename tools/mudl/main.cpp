// mudl - Model Viewer & Asset Pipeline Tool
// Main entry point

#include "app_runtime.hpp"
#include "camera.hpp"
#include "imgui_runtime.hpp"
#include "mudl_cli.hpp"
#include "mudl_commands.hpp"
#include "particle_tools.hpp"
#include "renderer.hpp"
#include "visual_corpus.hpp"
#include "viewer_runtime.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/render/nwn/model_loader.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>

namespace mudl {

using nw::render::nwn::DanglyMode;
using nw::render::nwn::set_dangly_debug_scale;
using nw::render::nwn::set_dangly_mode;

static constexpr std::string_view kSmokeTestModel = "c_aribeth";

static int run_kernel_command(std::string_view module_path, const std::function<int()>& fn)
{
    if (!init_kernel_services(module_path)) {
        return 1;
    }
    const int rc = fn();
    nw::kernel::services().shutdown();
    return rc;
}

static void configure_app_state(AppState& state, const ParsedArgs& args)
{
    state.turntable_mode = args.turntable_mode;
    state.turntable_frames = args.turntable_frames;
    state.turntable_capture = args.command == "turntable";
    state.output_dir = args.output_dir;
    state.screenshot_path = args.screenshot_path;
    state.screenshot_requested = !args.screenshot_path.empty();
    state.screenshot_delay_frames = state.screenshot_requested ? 2 : 0;
    state.animation_override = args.animation_override;
    state.enable_validation = args.enable_validation;
    state.show_debug_overlay = args.show_debug_overlay;
    state.dangly_scale = args.dangly_scale;
    state.dangly_mode = args.dangly_mode;
}

} // namespace mudl

int main(int argc, char* argv[])
{
    using namespace mudl;

    nw::init_logger(argc, argv);

    ParsedArgs args;
    if (auto exit_code = parse_args(argc, argv, args)) {
        return *exit_code;
    }

    LOG_F(INFO, "mudl starting...");

    if (args.command == "particle-preview") {
        return run_kernel_command(args.module_path, [&] {
            return run_particle_preview_command(args.initial_model, args.screenshot_path,
                args.particle_preview_time, args.particle_preview_view, args.animation_override,
                args.particle_preview_metadata);
        });
    }

    if (args.command == "particle-preview-frames") {
        return run_kernel_command(args.module_path, [&] {
            return run_particle_preview_frames_command(args.initial_model, args.output_dir,
                args.particle_preview_duration, args.particle_preview_fps, args.particle_preview_view,
                args.animation_override, args.particle_preview_metadata);
        });
    }

    if (args.command == "particle-export") {
        return run_kernel_command(args.module_path, [&] {
            return run_particle_export_command(args.initial_model, args.screenshot_path, args.animation_override);
        });
    }

    if (args.command == "spell-export") {
        return run_kernel_command(args.module_path, [&] {
            auto sequence = resolve_spell_sequence(args.initial_model);
            if (!sequence) {
                LOG_F(ERROR, "Failed to resolve spell row: {}", args.initial_model);
                return 1;
            }
            return run_spell_export_command(*sequence, args.initial_model, args.screenshot_path);
        });
    }

    if (args.command == "spell-export-live") {
        return run_kernel_command(args.module_path, [&] {
            auto sequence = resolve_spell_sequence(args.initial_model);
            if (!sequence) {
                LOG_F(ERROR, "Failed to resolve spell row: {}", args.initial_model);
                return 1;
            }
            AppState state{};
            return run_spell_export_live_command(state, *sequence, args.initial_model, args.live_export_output_path);
        });
    }

    if (args.command == "spell-preview-live") {
        return run_kernel_command(args.module_path, [&] {
            auto sequence = resolve_spell_sequence(args.initial_model);
            if (!sequence) {
                LOG_F(ERROR, "Failed to resolve spell row: {}", args.initial_model);
                return 1;
            }
            AppState state{};
            return run_spell_preview_live_command(state, *sequence, args.initial_model, args.screenshot_path,
                args.particle_preview_frame, args.particle_preview_view, args.particle_preview_metadata);
        });
    }

    if (args.command == "corpus") {
        return run_visual_corpus_command(args.corpus_path, args.output_dir,
            args.module_path, args.corpus_limit, args.corpus_filter, args.corpus_ledger_path);
    }

    if (args.command == "stats") {
        return run_stats_command(args.initial_model, args.module_path);
    } else if (args.command == "area" && !args.area_dump_module_path.empty()) {
        return run_area_dump_command(args.area_dump_module_path, args.area_dump_output_path,
            args.area_dump_skip_existing, args.area_dump_limit, args.show_debug_overlay);
    } else if (args.command == "texture") {
        return run_texture_command(args.initial_model, args.texture_output_path, args.module_path);
    } else if (args.command == "nwn-animation-smoke") {
        return run_nwn_animation_smoke_command(args.module_path);
    }

    const bool headless_command = command_is_headless(args.command)
        || args.max_frames > 0;

    if (headless_command) {
        // Headless capture still needs SDL's Vulkan hooks, which the dummy driver does not provide.
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_F(ERROR, "SDL_Init failed: {}", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    if (!headless_command) {
        window = SDL_CreateWindow("mudl",
            1280, 720,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
        if (!window) {
            LOG_F(ERROR, "Failed to create window: {}", SDL_GetError());
            return 1;
        }
    }

    AppState state{};
    configure_app_state(state, args);

    if (!init_graphics(state, window)) {
        LOG_F(ERROR, "Failed to initialize graphics");
        if (window) { SDL_DestroyWindow(window); }
        return 1;
    }

    state.camera = std::make_unique<Camera>();
    state.camera->set_aspect_ratio(1280.0f / 720.0f);
    if (window) { init_imgui(state, window); }

    if (!init_kernel_services(args.module_path)) {
        shutdown_graphics(state);
        if (window) { SDL_DestroyWindow(window); }
        return 1;
    }

    std::string initial_source = args.initial_model;
    std::optional<VfxSequence> initial_vfx_sequence;
    if (args.command == "spell") {
        initial_vfx_sequence = resolve_spell_sequence(initial_source);
        if (!initial_vfx_sequence) {
            LOG_F(ERROR, "Failed to resolve spell row: {}", initial_source);
            nw::kernel::services().shutdown();
            shutdown_graphics(state);
            if (window) {
                SDL_DestroyWindow(window);
            }
            return 1;
        }
    } else if (args.command == "vfx") {
        if (args.vfx_stage.empty()) {
            initial_vfx_sequence = resolve_vfx_sequence(initial_source);
        }
        if (!initial_vfx_sequence) {
            auto resolved = resolve_vfx_source(initial_source, args.vfx_stage);
            if (!resolved) {
                LOG_F(ERROR, "Failed to resolve VFX: {}", initial_source);
                nw::kernel::services().shutdown();
                shutdown_graphics(state);
                if (window) {
                    SDL_DestroyWindow(window);
                }
                return 1;
            }
            initial_source = std::move(*resolved);
        }
    }

    if (!init_render_runtime(state)) {
        nw::kernel::services().shutdown();
        shutdown_graphics(state);
        if (window) { SDL_DestroyWindow(window); }
        return 1;
    }

    state.renderer = std::make_unique<Renderer>(state.gfx_context);
    if (!state.renderer->initialize(state.shader_provider.get())) {
        LOG_F(ERROR, "Failed to initialize renderer");
        state.renderer.reset();
        state.shader_provider.reset();
        shutdown_graphics(state);
        if (window) { SDL_DestroyWindow(window); }
        nw::kernel::services().shutdown();
        return 1;
    }
    set_dangly_debug_scale(state.dangly_scale);
    set_dangly_mode(state.dangly_mode);

    if (args.command == "compute-smoke") {
        const bool ok = run_compute_smoke(state);
        state.renderer.reset();
        state.shader_provider.reset();
        shutdown_graphics(state);
        if (window) { SDL_DestroyWindow(window); }
        nw::kernel::services().shutdown();
        return ok ? 0 : 1;
    }

    if (!initial_source.empty()) {
        if (args.command == "area" || args.command == "area-screenshot") {
            load_area(state, initial_source);
        } else if (initial_vfx_sequence) {
            load_vfx_sequence(state, *initial_vfx_sequence);
        } else {
            load_model(state, initial_source);
        }
    } else if (args.max_frames > 0) {
        load_model(state, kSmokeTestModel);
    }

    Uint64 last_tick = SDL_GetTicks();
    int frame_count = 0;
    while (state.running) {
        Uint64 current_tick = SDL_GetTicks();
        float dt = static_cast<float>(current_tick - last_tick) / 1000.0f;
        last_tick = current_tick;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            imgui_process_event(&event);
            switch (event.type) {
            case SDL_EVENT_QUIT:
                state.running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!imgui_wants_keyboard()) {
                    handle_key_down(state, event.key);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!imgui_wants_mouse()) {
                    state.last_mouse_x = event.button.x;
                    state.last_mouse_y = event.button.y;
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        state.drag_mode = DragMode::orbit;
                    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                        state.drag_mode = DragMode::pan;
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (!imgui_wants_mouse()) {
                    if ((event.button.button == SDL_BUTTON_LEFT && state.drag_mode == DragMode::orbit)
                        || (event.button.button == SDL_BUTTON_MIDDLE && state.drag_mode == DragMode::pan)) {
                        state.drag_mode = DragMode::none;
                    }
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (!imgui_wants_mouse()) {
                    if (state.camera && state.drag_mode != DragMode::none) {
                        float dx = event.motion.xrel;
                        float dy = event.motion.yrel;
                        if (state.drag_mode == DragMode::orbit) {
                            if (state.area_navigation) {
                                state.camera->yaw(-dx * 0.35f);
                                state.camera->pitch(-dy * 0.25f);
                            } else {
                                state.camera->orbit(-dx * 0.35f, -dy * 0.25f);
                            }
                        } else {
                            float pan_scale = state.camera->pan_units_per_pixel(static_cast<float>(state.window_height));
                            state.camera->pan(-dx * pan_scale, dy * pan_scale);
                        }
                    }
                    state.last_mouse_x = event.motion.x;
                    state.last_mouse_y = event.motion.y;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (!imgui_wants_mouse()) {
                    if (state.camera) {
                        if (event.wheel.y != 0) {
                            if (state.area_navigation && !state.camera->is_orthographic()) {
                                state.camera->move_forward(event.wheel.y > 0 ? 4.0f : -4.0f, true);
                            } else {
                                float zoom_factor = event.wheel.y > 0 ? 1.0f / 1.12f : 1.12f;
                                state.camera->zoom(zoom_factor);
                            }
                        }
                    }
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                state.window_width = event.window.data1;
                state.window_height = event.window.data2;
                nw::gfx::resize_context(state.gfx_context,
                    static_cast<uint32_t>(state.window_width),
                    static_cast<uint32_t>(state.window_height));
                state.camera->set_aspect_ratio(
                    static_cast<float>(state.window_width) / static_cast<float>(state.window_height));
                break;
            }
        }

        update_viewer(state, dt);
        render_frame(state);

        if (args.max_frames > 0 && ++frame_count >= args.max_frames) {
            state.running = false;
        }
    }

    // Shutdown order: GPU resources first, then context, then kernel
    if (state.gfx_context) {
        nw::gfx::wait_idle(state.gfx_context);
    }
    state.current_scene.reset();
    state.renderer.reset();
    state.shader_provider.reset();
    state.camera.reset();
    shutdown_imgui();

    shutdown_graphics(state);
    if (window) {
        SDL_DestroyWindow(window);
    }

    nw::kernel::services().shutdown();

    LOG_F(INFO, "mudl exiting");
    return 0;
}
