// mudl - Model Viewer & Asset Pipeline Tool
// Main entry point

#include "app_runtime.hpp"
#include "imgui_runtime.hpp"
#include "mudl_cli.hpp"
#include "mudl_commands.hpp"
#include "particle_tools.hpp"
#include "viewer_runtime.hpp"
#include "visual_corpus.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/viewer/camera.hpp>
#include <nw/render/viewer/preview_render_resources.hpp>
#include <nw/render/viewer/preview_scene.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

namespace mudl {

using nw::render::nwn::set_dangly_debug_scale;
using nw::render::nwn::set_dangly_mode;

static constexpr std::string_view kSmokeTestModel = "c_aribeth";

static nw::render::ForwardPlusDebugMode to_render_forward_plus_debug_mode(ForwardPlusDebugMode mode) noexcept
{
    switch (mode) {
    case ForwardPlusDebugMode::cluster_light_count:
        return nw::render::ForwardPlusDebugMode::cluster_light_count;
    case ForwardPlusDebugMode::depth_slice:
        return nw::render::ForwardPlusDebugMode::depth_slice;
    case ForwardPlusDebugMode::off:
    default:
        return nw::render::ForwardPlusDebugMode::off;
    }
}

static nw::render::viewer::ForwardPlusConfig to_render_forward_plus_config(const ForwardPlusConfig& config) noexcept
{
    return nw::render::viewer::ForwardPlusConfig{
        .tile_size = config.tile_size,
        .depth_slices = config.depth_slices,
        .max_lights_per_cluster = config.max_lights_per_cluster,
    };
}

static nw::render::viewer::ForwardPlusRenderPolicy to_render_forward_plus_policy(
    const ForwardPlusRenderPolicy& policy) noexcept
{
    return nw::render::viewer::ForwardPlusRenderPolicy{
        .enabled = policy.enabled,
        .auto_configure_area = policy.auto_configure_area,
        .gpu_culling = policy.gpu_culling,
        .config = to_render_forward_plus_config(policy.config),
        .debug_mode = to_render_forward_plus_debug_mode(policy.debug_mode),
    };
}

static nw::render::nwn::DanglyMode to_render_dangly_mode(DanglyMode mode) noexcept
{
    return mode == DanglyMode::modern
        ? nw::render::nwn::DanglyMode::modern
        : nw::render::nwn::DanglyMode::legacy;
}

static nlohmann::json load_report_json(const nw::render::viewer::PreviewLoadReport& report)
{
    nlohmann::json resources = nlohmann::json::array();
    for (const auto& resource : report.resources) {
        resources.push_back({
            {"resource", resource.resource.filename()},
            {"resref", resource.resource.resref.string()},
            {"type", std::string(nw::ResourceType::to_string(resource.resource.type))},
            {"status", std::string(nw::render::viewer::preview_load_resource_status_label(resource.status))},
            {"origins", resource.origins},
        });
    }

    nlohmann::json particles = nlohmann::json::array();
    for (const auto& particle : report.particles) {
        particles.push_back({
            {"owner", particle.owner},
            {"emitter_count", particle.emitter_count},
            {"material_count", particle.material_count},
            {"alpha_material_count", particle.alpha_material_count},
            {"cutout_material_count", particle.cutout_material_count},
            {"additive_material_count", particle.additive_material_count},
            {"named_texture_count", particle.named_texture_count},
            {"missing_texture_count", particle.missing_texture_count},
            {"alpha_texture_count", particle.alpha_texture_count},
            {"opaque_texture_count", particle.opaque_texture_count},
            {"max_particles_total", particle.max_particles_total},
            {"import_warning_count", particle.import_warning_count},
            {"compile_warning_count", particle.compile_warning_count},
            {"effect_event_count", particle.effect_event_count},
        });
    }

    nlohmann::json materials = nlohmann::json::array();
    for (const auto& material : report.materials) {
        materials.push_back({
            {"owner", material.owner},
            {"material_count", material.material_count},
            {"fallback_material_count", material.fallback_material_count},
            {"plt_albedo_count", material.plt_albedo_count},
            {"plt_enabled_count", material.plt_enabled_count},
            {"emissive_material_count", material.emissive_material_count},
            {"double_sided_count", material.double_sided_count},
            {"opaque_count", material.opaque_count},
            {"cutout_count", material.cutout_count},
            {"transparent_count", material.transparent_count},
            {"water_count", material.water_count},
            {"unknown_alpha_mode_count", material.unknown_alpha_mode_count},
            {"color_key_count", material.color_key_count},
            {"albedo_source_count", material.albedo_source_count},
            {"normal_source_count", material.normal_source_count},
            {"surface_source_count", material.surface_source_count},
            {"emissive_source_count", material.emissive_source_count},
            {"albedo_bound_count", material.albedo_bound_count},
            {"normal_bound_count", material.normal_bound_count},
            {"surface_bound_count", material.surface_bound_count},
            {"emissive_bound_count", material.emissive_bound_count},
            {"invalid_scalar_count", material.invalid_scalar_count},
            {"roughness_min", material.roughness_min},
            {"roughness_max", material.roughness_max},
            {"specular_strength_min", material.specular_strength_min},
            {"specular_strength_max", material.specular_strength_max},
            {"normal_scale_min", material.normal_scale_min},
            {"normal_scale_max", material.normal_scale_max},
        });
    }

    nlohmann::json geometries = nlohmann::json::array();
    for (const auto& geometry : report.geometries) {
        geometries.push_back({
            {"owner", geometry.owner},
            {"primitive_count", geometry.primitive_count},
            {"static_primitive_count", geometry.static_primitive_count},
            {"skinned_primitive_count", geometry.skinned_primitive_count},
            {"deformed_primitive_count", geometry.deformed_primitive_count},
            {"shadow_caster_count", geometry.shadow_caster_count},
            {"vertex_count", geometry.vertex_count},
            {"index_count", geometry.index_count},
            {"node_count", geometry.node_count},
            {"socket_count", geometry.socket_count},
            {"skin_count", geometry.skin_count},
            {"skeleton_count", geometry.skeleton_count},
            {"animation_count", geometry.animation_count},
            {"deformer_count", geometry.deformer_count},
            {"particle_system_count", geometry.particle_system_count},
            {"normal_repair_count", geometry.normal_repair_count},
            {"tangent_repair_count", geometry.tangent_repair_count},
            {"skipped_empty_mesh_count", geometry.skipped_empty_mesh_count},
            {"skipped_skin_mesh_count", geometry.skipped_skin_mesh_count},
            {"primitive_overflow_count", geometry.primitive_overflow_count},
        });
    }

    nlohmann::json events = nlohmann::json::array();
    for (const auto& event : report.events) {
        events.push_back({
            {"severity", std::string(nw::render::viewer::preview_load_event_severity_label(event.severity))},
            {"category", event.category},
            {"message", event.message},
        });
    }

    return {
        {"source", report.source},
        {"kind", report.kind},
        {"model_count", report.model_names.size()},
        {"models", report.model_names},
        {"resource_count", report.resources.size()},
        {"geometry_count", report.geometries.size()},
        {"missing_resource_count", report.missing_resource_count()},
        {"warning_count", report.warning_count()},
        {"error_count", report.error_count()},
        {"resources", std::move(resources)},
        {"materials", std::move(materials)},
        {"geometries", std::move(geometries)},
        {"particles", std::move(particles)},
        {"events", std::move(events)},
    };
}

static int run_kernel_command(std::string_view module_path, std::string_view user_path, const std::function<int()>& fn)
{
    if (!init_kernel_services(module_path, user_path)) {
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
    if (!args.pbr_environment_path.empty()) {
        state.static_pbr_environment_path = args.pbr_environment_path;
    }
    state.static_pbr_ibl_requested = args.pbr_ibl_enabled;
    state.dangly_scale = args.dangly_scale;
    state.dangly_mode = to_render_dangly_mode(args.dangly_mode);
    state.preview_scene_load_options = nw::render::viewer::default_preview_scene_load_options();
    if (args.legacy_nwn_model_path) {
        state.preview_scene_load_options.nwn_model_path = nw::render::viewer::NwnModelPreviewPath::legacy;
    }
    if (args.command == "area-benchmark" || args.command == "area-sweep") {
        state.window_width = args.benchmark_width;
        state.window_height = args.benchmark_height;
    }
}

static bool prepare_turntable_output_dir(std::string_view output_dir)
{
    const std::filesystem::path dir{std::string{output_dir}};
    if (dir.empty()) {
        LOG_F(ERROR, "Turntable output directory is empty");
        return false;
    }

    std::error_code ec;
    const bool exists = std::filesystem::exists(dir, ec);
    if (ec) {
        LOG_F(ERROR, "Failed to inspect turntable output directory '{}': {}", dir.string(), ec.message());
        return false;
    }

    if (exists) {
        const bool is_directory = std::filesystem::is_directory(dir, ec);
        if (ec) {
            LOG_F(ERROR, "Failed to inspect turntable output directory '{}': {}", dir.string(), ec.message());
            return false;
        }
        if (!is_directory) {
            LOG_F(ERROR, "Turntable output path is not a directory: {}", dir.string());
            return false;
        }
        return true;
    }

    std::filesystem::create_directories(dir, ec);
    if (ec) {
        LOG_F(ERROR, "Failed to create turntable output directory '{}': {}", dir.string(), ec.message());
        return false;
    }
    LOG_F(INFO, "Created turntable output directory: {}", dir.string());
    return true;
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

    if (args.command == "report") {
        return run_kernel_command(args.module_path, args.user_path, [&] {
            const auto report = nw::render::viewer::build_preview_load_report(args.initial_model, args.animation_override);
            std::cout << load_report_json(report).dump(2) << '\n'
                      << std::flush;
            return report.error_count() == 0 ? 0 : 1;
        });
    }

    if (args.command == "particle-preview") {
        return run_kernel_command(args.module_path, args.user_path, [&] {
            return run_particle_preview_command(args.initial_model, args.screenshot_path,
                args.particle_preview_time, args.particle_preview_view, args.animation_override,
                args.particle_preview_metadata);
        });
    }

    if (args.command == "particle-preview-frames") {
        return run_kernel_command(args.module_path, args.user_path, [&] {
            return run_particle_preview_frames_command(args.initial_model, args.output_dir,
                args.particle_preview_duration, args.particle_preview_fps, args.particle_preview_view,
                args.animation_override, args.particle_preview_metadata);
        });
    }

    if (args.command == "particle-export") {
        return run_kernel_command(args.module_path, args.user_path, [&] {
            return run_particle_export_command(args.initial_model, args.screenshot_path, args.animation_override);
        });
    }

    if (args.command == "spell-export") {
        return run_kernel_command(args.module_path, args.user_path, [&] {
            auto sequence = resolve_spell_sequence(args.initial_model);
            if (!sequence) {
                LOG_F(ERROR, "Failed to resolve spell row: {}", args.initial_model);
                return 1;
            }
            return run_spell_export_command(*sequence, args.initial_model, args.screenshot_path);
        });
    }

    if (args.command == "spell-export-live") {
        return run_kernel_command(args.module_path, args.user_path, [&] {
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
        return run_kernel_command(args.module_path, args.user_path, [&] {
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
            args.module_path, args.user_path, args.pbr_environment_path,
            args.pbr_ibl_enabled, args.corpus_limit, args.corpus_filter, args.corpus_ledger_path);
    }

    if (args.command == "dump") {
        return run_dump_command(args.initial_model, args.output_dir, args.module_path, args.user_path);
    } else if (args.command == "stats") {
        return run_stats_command(args.initial_model, args.module_path, args.user_path);
    } else if (args.command == "area-lights") {
        return run_area_lights_command(args.initial_model, args.module_path, args.user_path);
    } else if (args.command == "area" && !args.area_dump_module_path.empty()) {
        return run_area_dump_command(args.area_dump_module_path, args.area_dump_output_path,
            args.user_path, args.area_dump_skip_existing, args.area_dump_limit, args.show_debug_overlay);
    } else if (args.command == "texture") {
        return run_texture_command(args.initial_model, args.texture_output_path, args.module_path, args.user_path);
    } else if (args.command == "nwn-animation-smoke") {
        return run_nwn_animation_smoke_command(args.module_path, args.user_path);
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

    state.camera = std::make_unique<nw::render::viewer::Camera>();
    state.camera->set_aspect_ratio(1280.0f / 720.0f);
    if (window) { init_imgui(state, window); }

    if (!init_kernel_services(args.module_path, args.user_path)) {
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

    if (!init_viewer_renderers(state)) {
        state.shader_provider.reset();
        shutdown_graphics(state);
        if (window) { SDL_DestroyWindow(window); }
        nw::kernel::services().shutdown();
        return 1;
    }
    set_dangly_debug_scale(state.dangly_scale);
    set_dangly_mode(state.dangly_mode);

    if (args.command == "area-benchmark") {
        const auto forward_plus_policy = to_render_forward_plus_policy(args.benchmark_forward_plus_policy);
        const int rc = run_area_benchmark_command(state, args.initial_model,
            args.module_path, args.user_path,
            args.benchmark_frames, args.benchmark_warmup_frames,
            args.benchmark_lights_enabled, args.benchmark_shadows_enabled,
            args.benchmark_local_shadows_enabled, args.show_debug_overlay,
            forward_plus_policy,
            args.benchmark_camera, args.benchmark_area_time_seconds, args.benchmark_visible_tile_radius,
            args.benchmark_visibility_mode, args.benchmark_visible_tile_cone_half_angle,
            args.benchmark_output_path, args.benchmark_screenshot_path);
        reset_viewer_renderers(state);
        state.shader_provider.reset();
        shutdown_graphics(state);
        if (window) { SDL_DestroyWindow(window); }
        nw::kernel::services().shutdown();
        return rc;
    }

    if (args.command == "area-sweep") {
        const auto forward_plus_policy = to_render_forward_plus_policy(args.benchmark_forward_plus_policy);
        const int rc = run_area_sweep_command(state, args.initial_model,
            args.module_path, args.user_path,
            args.benchmark_frames, args.benchmark_warmup_frames,
            args.area_sweep_limit, args.area_sweep_variants,
            args.benchmark_local_shadows_enabled, args.show_debug_overlay,
            forward_plus_policy,
            args.benchmark_output_path);
        reset_viewer_renderers(state);
        state.shader_provider.reset();
        shutdown_graphics(state);
        if (window) { SDL_DestroyWindow(window); }
        nw::kernel::services().shutdown();
        return rc;
    }

    if (args.command == "screenshot" || args.command == "area-screenshot") {
        const int rc = run_viewer_screenshot_command(
            state,
            args.initial_model,
            args.screenshot_path,
            args.command == "area-screenshot");
        reset_viewer_renderers(state);
        state.shader_provider.reset();
        shutdown_graphics(state);
        if (window) { SDL_DestroyWindow(window); }
        nw::kernel::services().shutdown();
        return rc;
    }

    if (args.command == "compute-smoke") {
        const bool ok = run_compute_smoke(state);
        reset_viewer_renderers(state);
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

    if (args.command == "turntable") {
        if (!state.current_scene) {
            LOG_F(ERROR, "Turntable source '{}' produced no renderable scene", initial_source);
            reset_viewer_renderers(state);
            state.shader_provider.reset();
            shutdown_graphics(state);
            if (window) { SDL_DestroyWindow(window); }
            nw::kernel::services().shutdown();
            return 1;
        }
        if (!prepare_turntable_output_dir(state.output_dir)) {
            reset_viewer_renderers(state);
            state.shader_provider.reset();
            shutdown_graphics(state);
            if (window) { SDL_DestroyWindow(window); }
            nw::kernel::services().shutdown();
            return 1;
        }
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
    reset_viewer_renderers(state);
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
