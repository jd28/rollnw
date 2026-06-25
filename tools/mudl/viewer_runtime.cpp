#include "viewer_runtime.hpp"

#include "benchmark_report.hpp"
#include "imgui_runtime.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Module.hpp>
#include <nw/render/model_renderer.hpp>
#include <nw/render/render_service.hpp>
#include <nw/render/viewer/area_lighting.hpp>
#include <nw/render/viewer/preview_model_animation.hpp>
#include <nw/render/viewer/preview_model_draws.hpp>
#include <nw/render/viewer/preview_scene.hpp>
#include <nw/render/viewer/scene_particles.hpp>
#include <nw/render/viewer/scene_shadow.hpp>
#include <nw/render/viewer/session.hpp>
#include <nw/util/game_install.hpp>

#include <SDL3/SDL.h>

#include <glm/gtx/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace mudl {
namespace {

static constexpr int32_t kVfxSequenceStepMs = 33;
constexpr int32_t kAreaDumpScreenshotWarmupFrames = 2;
constexpr int32_t kAreaDumpScreenshotMaxFrames = 8;
constexpr std::array<std::string_view, 8> kPreferredRenderModelHoldAnimations{{
    "cpause1",
    "pause1",
    "closed",
    "opened1",
    "open",
    "default",
    "on",
    "impact",
}};
void fit_area_navigation_camera(nw::render::viewer::Camera& camera, const nw::render::viewer::Bounds& bounds);

using json = nlohmann::json;

double seconds_to_ms(float seconds)
{
    return static_cast<double>(seconds) * 1000.0;
}

json command_stats_json(const nw::gfx::CommandStats& stats)
{
    return {
        {"render_passes", stats.render_pass_count},
        {"draws", stats.draw_count},
        {"indexed_draws", stats.indexed_draw_count},
        {"nonindexed_draws", stats.nonindexed_draw_count},
        {"indirect_draw_calls", stats.indirect_draw_call_count},
        {"draw_instances", stats.draw_instance_count},
        {"draw_indices", stats.draw_index_count},
        {"draw_vertices", stats.draw_vertex_count},
        {"dispatches", stats.dispatch_count},
        {"pipeline_binds", stats.pipeline_bind_count},
        {"pipeline_binds_skipped", stats.pipeline_bind_skipped_count},
        {"vertex_buffer_binds", stats.vertex_buffer_bind_count},
        {"vertex_buffer_binds_skipped", stats.vertex_buffer_bind_skipped_count},
        {"index_buffer_binds", stats.index_buffer_bind_count},
        {"index_buffer_binds_skipped", stats.index_buffer_bind_skipped_count},
        {"resource_binds", stats.resource_bind_count},
        {"resource_binds_skipped", stats.resource_bind_skipped_count},
        {"descriptor_buffer_binds", stats.descriptor_buffer_bind_count},
        {"descriptor_buffer_binds_skipped", stats.descriptor_buffer_bind_skipped_count},
        {"uniform_allocations", stats.uniform_allocation_count},
        {"uniform_allocation_bytes", stats.uniform_allocation_bytes},
        {"vertex_allocations", stats.vertex_allocation_count},
        {"vertex_allocation_bytes", stats.vertex_allocation_bytes},
    };
}

struct SceneLocalLightSourceCounts {
    size_t authored_model = 0;
    size_t tile_model = 0;
    size_t placeable_table = 0;
};

struct AreaSweepVariant {
    std::string name;
    bool lights_enabled = true;
    bool shadows_enabled = true;
    bool local_shadows_enabled = true;
    int visible_tile_radius = -1;
    AreaBenchmarkVisibilityMode visibility_mode = AreaBenchmarkVisibilityMode::radius;
    float visibility_cone_half_angle_degrees = 75.0f;
    nw::render::ForwardPlusDebugMode forward_plus_debug_mode = nw::render::ForwardPlusDebugMode::off;
};

struct PreviewSessionSetup {
    bool lights_enabled = true;
    bool shadows_enabled = true;
    bool local_shadows_enabled = true;
    bool debug_enabled = false;
    const nw::render::viewer::ForwardPlusRenderPolicy* forward_plus_policy = nullptr;
};

void configure_preview_session(nw::render::viewer::ViewerSession& session, const PreviewSessionSetup& setup)
{
    session.set_area_lights_enabled(setup.lights_enabled);
    session.set_area_shadows_enabled(setup.shadows_enabled);
    session.set_local_shadows_enabled(setup.local_shadows_enabled);
    session.set_area_debug_enabled(setup.debug_enabled);
    session.set_area_triggers_enabled(setup.debug_enabled);
    session.set_area_encounters_enabled(setup.debug_enabled);
    session.set_area_day_night_autoplay(false);
    session.set_playing(false);
    if (setup.forward_plus_policy) {
        session.set_forward_plus_policy(*setup.forward_plus_policy);
    }
}

bool run_viewer_session_frames(
    AppState& state,
    nw::render::viewer::ViewerSession& session,
    const nw::render::viewer::ViewerViewport& viewport,
    int warmup_frames,
    int render_frames,
    std::vector<nw::render::viewer::ViewerFrameStats>* samples,
    std::string* failure,
    bool wait_for_idle = true)
{
    warmup_frames = std::max(warmup_frames, 0);
    render_frames = std::max(render_frames, 0);
    const int total_frames = warmup_frames + render_frames;

    if (samples && render_frames > 0) {
        samples->reserve(samples->size() + static_cast<size_t>(render_frames));
    }

    for (int frame_index = 0; frame_index < total_frames; ++frame_index) {
        auto* cmd = nw::gfx::begin_frame(state.gfx_context);
        if (!cmd) {
            if (failure) {
                *failure = "begin_frame failed";
            }
            return false;
        }

        session.tick(0);
        session.render(cmd, viewport);
        if (samples && frame_index >= warmup_frames) {
            samples->push_back(session.last_frame_stats());
        }
        nw::gfx::end_frame(state.gfx_context);
    }

    if (wait_for_idle) {
        nw::gfx::wait_idle(state.gfx_context);
    }
    return true;
}

bool capture_viewer_session_frame(
    AppState& state,
    nw::render::viewer::ViewerSession& session,
    const nw::render::viewer::ViewerViewport& viewport,
    std::string_view screenshot_path,
    std::string* failure,
    bool wait_for_idle = true)
{
    auto* cmd = nw::gfx::begin_frame(state.gfx_context);
    if (!cmd) {
        if (failure) {
            *failure = "begin_frame failed";
        }
        return false;
    }

    session.tick(0);
    session.render(cmd, viewport);
    const std::string screenshot_output{screenshot_path};
    const bool captured = nw::gfx::capture_screenshot(
        state.gfx_context,
        cmd,
        screenshot_output.c_str());
    if (wait_for_idle) {
        nw::gfx::wait_idle(state.gfx_context);
    }
    if (!captured && failure) {
        *failure = "capture failed";
    }
    return captured;
}

bool run_screenshot_capture_impl(
    AppState& state,
    const std::filesystem::path& screenshot_path,
    int warmup_frames,
    int max_frames)
{
    warmup_frames = std::max(warmup_frames, 0);
    max_frames = std::max(max_frames, 0);
    state.screenshot_path = screenshot_path.string();
    state.screenshot_requested = true;
    state.screenshot_captured = false;
    state.screenshot_delay_frames = warmup_frames;
    state.running = true;

    for (int frame_index = 0; frame_index < max_frames; ++frame_index) {
        render_frame(state);
        if (!state.screenshot_requested) {
            break;
        }
    }

    state.screenshot_requested = false;
    state.running = false;
    return state.screenshot_captured;
}

SceneLocalLightSourceCounts count_scene_local_light_sources(const nw::render::viewer::PreviewScene& scene)
{
    SceneLocalLightSourceCounts result{};
    for (const auto& light : scene.local_lights) {
        if (light.source == nw::render::viewer::SceneLocalLightSource::tile_model) {
            ++result.tile_model;
        } else if (light.source == nw::render::viewer::SceneLocalLightSource::placeable_table) {
            ++result.placeable_table;
        } else {
            ++result.authored_model;
        }
    }
    return result;
}

std::string trim_copy(std::string_view value)
{
    auto first = value.begin();
    auto last = value.end();
    while (first != last && std::isspace(static_cast<unsigned char>(*first))) {
        ++first;
    }
    while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1)))) {
        --last;
    }
    return std::string{first, last};
}

std::vector<std::string> collect_area_sweep_areas(std::string_view source, size_t limit)
{
    std::vector<std::string> result;
    const std::string source_string{source};
    std::error_code ec;
    const std::filesystem::path source_path{source_string};
    if (!source_string.empty() && std::filesystem::is_regular_file(source_path, ec)) {
        std::ifstream in{source_path};
        std::string line;
        while (std::getline(in, line)) {
            auto area = trim_copy(line);
            if (area.empty() || area.front() == '#') {
                continue;
            }
            result.push_back(std::move(area));
            if (limit > 0 && result.size() >= limit) {
                break;
            }
        }
        return result;
    }

    auto area = trim_copy(source);
    if (!area.empty()) {
        result.push_back(std::move(area));
    }
    return result;
}

std::vector<AreaSweepVariant> make_area_sweep_variants(std::string_view variant_set)
{
    const auto full = AreaSweepVariant{.name = "full"};
    if (variant_set == "minimal") {
        return {full};
    }

    std::vector<AreaSweepVariant> result{
        full,
        AreaSweepVariant{.name = "no-shadows", .shadows_enabled = false},
        AreaSweepVariant{.name = "no-lights", .lights_enabled = false},
        AreaSweepVariant{.name = "visibility-radius", .visible_tile_radius = 8},
    };

    if (variant_set == "default" || variant_set.empty()) {
        return result;
    }

    if (variant_set == "all") {
        result.push_back(AreaSweepVariant{
            .name = "no-lights-no-shadows",
            .lights_enabled = false,
            .shadows_enabled = false,
        });
        result.push_back(AreaSweepVariant{
            .name = "no-local-shadows",
            .local_shadows_enabled = false,
        });
        result.push_back(AreaSweepVariant{
            .name = "visibility-cone",
            .visible_tile_radius = 8,
            .visibility_mode = AreaBenchmarkVisibilityMode::view_cone,
        });
        result.push_back(AreaSweepVariant{
            .name = "forward-plus-clusters",
            .forward_plus_debug_mode = nw::render::ForwardPlusDebugMode::cluster_light_count,
        });
        result.push_back(AreaSweepVariant{
            .name = "forward-plus-depth",
            .forward_plus_debug_mode = nw::render::ForwardPlusDebugMode::depth_slice,
        });
        return result;
    }

    return {};
}

json optional_string_json(const std::string& value)
{
    return value.empty() ? json(nullptr) : json(value);
}

const char* area_benchmark_camera_mode_label(AreaBenchmarkCameraMode mode)
{
    switch (mode) {
    case AreaBenchmarkCameraMode::fit:
        return "fit";
    case AreaBenchmarkCameraMode::gameplay:
        return "gameplay";
    case AreaBenchmarkCameraMode::custom:
        return "custom";
    }
    return "fit";
}

const char* area_benchmark_visibility_mode_label(AreaBenchmarkVisibilityMode mode)
{
    switch (mode) {
    case AreaBenchmarkVisibilityMode::radius:
        return "radius";
    case AreaBenchmarkVisibilityMode::view_cone:
        return "view-cone";
    }
    return "radius";
}

const char* forward_plus_debug_mode_label(nw::render::ForwardPlusDebugMode mode)
{
    switch (mode) {
    case nw::render::ForwardPlusDebugMode::off:
        return "off";
    case nw::render::ForwardPlusDebugMode::cluster_light_count:
        return "cluster-lights";
    case nw::render::ForwardPlusDebugMode::depth_slice:
        return "depth-slices";
    }
    return "off";
}

json forward_plus_policy_json(const nw::render::viewer::ForwardPlusRenderPolicy& policy)
{
    return {
        {"enabled", policy.enabled},
        {"auto_configure_area", policy.auto_configure_area},
        {"gpu_culling", policy.gpu_culling},
        {"tile_size", policy.config.tile_size},
        {"depth_slices", policy.config.depth_slices},
        {"max_lights_per_cluster", policy.config.max_lights_per_cluster},
        {"debug", forward_plus_debug_mode_label(policy.debug_mode)},
    };
}

json vec3_json(const glm::vec3& value)
{
    return {
        {"x", value.x},
        {"y", value.y},
        {"z", value.z},
    };
}

nw::render::viewer::Bounds benchmark_bounds_for_scene(const nw::render::viewer::PreviewScene& scene)
{
    if (scene.area_render_scene && scene.area_render_scene->has_scene_bounds()) {
        return scene.area_render_scene->scene_bounds();
    }
    return scene.current_bounds();
}

void apply_area_benchmark_camera(nw::render::viewer::ViewerSession& session,
    const nw::render::viewer::ViewerViewport& viewport,
    const AreaBenchmarkCameraOptions& options)
{
    auto* scene = session.scene();
    if (!scene) {
        return;
    }

    session.update_viewport(viewport);
    auto& camera = session.camera();
    if (options.fov_degrees > 0.0f) {
        camera.set_fov(options.fov_degrees);
    }

    if (options.mode == AreaBenchmarkCameraMode::gameplay) {
        camera.set_area_gameplay_view(
            benchmark_bounds_for_scene(*scene),
            options.fov_degrees > 0.0f ? options.fov_degrees : 65.0f);
    }

    if (options.position || options.target) {
        const glm::vec3 position = options.position.value_or(camera.get_position());
        const glm::vec3 target = options.target.value_or(camera.get_target());
        camera.set_free_view(position, target, nw::render::viewer::Camera::ProjectionMode::perspective);
    }
}

void apply_area_visibility_settings(
    nw::render::viewer::ViewerSession& session,
    int visible_tile_radius,
    AreaBenchmarkVisibilityMode visibility_mode,
    float visibility_cone_half_angle_degrees)
{
    if (visible_tile_radius < 0) {
        return;
    }
    if (visibility_mode == AreaBenchmarkVisibilityMode::view_cone) {
        session.set_area_visibility_view_cone_tiles(visible_tile_radius, visibility_cone_half_angle_degrees);
    } else {
        session.set_area_visibility_radius_tiles(visible_tile_radius);
    }
}

bool prepare_area_session_for_benchmark(
    nw::render::viewer::ViewerSession& session,
    std::string_view area_resref,
    const nw::render::viewer::ViewerViewport& viewport,
    const AreaBenchmarkCameraOptions& camera_options,
    int visible_tile_radius,
    AreaBenchmarkVisibilityMode visibility_mode,
    float visibility_cone_half_angle_degrees)
{
    if (!session.load_area(area_resref)) {
        return false;
    }
    session.fit_to_scene(viewport);
    apply_area_benchmark_camera(session, viewport, camera_options);
    apply_area_visibility_settings(
        session, visible_tile_radius, visibility_mode, visibility_cone_half_angle_degrees);
    return true;
}

json area_render_cache_stats_json(const nw::render::viewer::AreaRenderSceneStats& stats)
{
    return {
        {"records", stats.record_count},
        {"static_records", stats.static_record_count},
        {"dynamic_records", stats.dynamic_record_count},
        {"disabled_records", stats.disabled_record_count},
        {"prepared_draws", stats.prepared_draw_count},
        {"shadow_prepared_draws", stats.shadow_prepared_surface_count},
        {"static_geometry_meshes", stats.static_geometry_mesh_count},
        {"static_geometry_vertices", stats.static_geometry_vertex_count},
        {"static_geometry_indices", stats.static_geometry_index_count},
        {"static_geometry_bytes", stats.static_geometry_bytes},
        {"local_lights", stats.local_light_count},
        {"max_prepared_draws_per_record", stats.max_prepared_draws_per_record},
        {"max_shadow_prepared_draws_per_record", stats.max_shadow_prepared_surfaces_per_record},
        {"light_indices", stats.light_index_count},
        {"max_light_indices_per_record", stats.max_light_indices_per_record},
        {"chunk_light_indices", stats.chunk_light_index_count},
        {"max_light_indices_per_chunk", stats.max_light_indices_per_chunk},
        {"pass_records", {
                             {"opaque_cutout", stats.opaque_cutout_record_count},
                             {"water", stats.water_record_count},
                             {"transparent", stats.transparent_record_count},
                             {"shadow_casters", stats.shadow_caster_record_count},
                         }},
        {"chunks", {
                       {"width", stats.chunk_width},
                       {"height", stats.chunk_height},
                       {"count", stats.chunk_count},
                       {"nonempty", stats.nonempty_chunk_count},
                       {"max_records", stats.max_records_per_chunk},
                       {"light_indices", stats.chunk_light_index_count},
                       {"max_light_indices", stats.max_light_indices_per_chunk},
                   }},
        {"source_records", {
                               {"tiles", stats.tile_record_count},
                               {"creatures", stats.creature_record_count},
                               {"doors", stats.door_record_count},
                               {"items", stats.item_record_count},
                               {"placeables", stats.placeable_record_count},
                               {"unknown", stats.unknown_record_count},
                           }},
    };
}

json current_render_service_stats_json()
{
    auto* render_service = nw::kernel::services().get_mut<nw::render::RenderService>();
    if (!render_service) {
        return json::object();
    }
    return render_service->stats();
}

json render_service_stats_member_json(const json& service_stats, const char* name)
{
    const auto it = service_stats.find(name);
    if (it == service_stats.end() || !it->is_object()) {
        return json::object();
    }
    return *it;
}

json render_asset_cache_stats_json(const json& service_stats)
{
    return render_service_stats_member_json(service_stats, "asset_cache");
}

json model_loader_resource_cache_stats_json(const json& service_stats)
{
    return render_service_stats_member_json(service_stats, "nwn_model_loader_resource_cache");
}

json prepared_model_draw_stats_json(const nw::render::PreparedModelDrawStats& stats)
{
    return {
        {"handles", stats.handle_count},
        {"visible_instances", stats.visible_instance_count},
        {"hidden_instances", stats.hidden_instance_count},
        {"stale_handles", stats.stale_handle_count},
        {"missing_assets", stats.missing_asset_count},
        {"invalid_draws", stats.invalid_draw_count},
        {"render_model_instances", stats.render_model_instance_count},
        {"nwn_legacy_instances", stats.nwn_legacy_instance_count},
        {"render_model_draws", stats.render_model_draw_count},
        {"nwn_legacy_draws", stats.nwn_legacy_draw_count},
        {"material_fallback_draws", stats.material_fallback_draw_count},
        {"render_model_material_fallback_draws", stats.render_model_material_fallback_draw_count},
        {"nwn_legacy_material_fallback_draws", stats.nwn_legacy_material_fallback_draw_count},
    };
}

json prepared_model_draw_material_stats_json(
    const nw::render::viewer::PreviewPreparedModelDrawMaterialStats& stats)
{
    return {
        {"opaque", stats.opaque_count},
        {"cutout", stats.cutout_count},
        {"transparent", stats.transparent_count},
        {"water", stats.water_count},
        {"invalid_modes", stats.invalid_mode_count},
        {"total", stats.total()},
    };
}

json prepared_model_draw_validation_json(
    bool enabled,
    const nw::render::viewer::PreviewPreparedModelDrawValidation& validation)
{
    return {
        {"enabled", enabled},
        {"passed", validation.valid()},
        {"mismatches", validation.protocol_mismatch_count},
        {"prepared_draws", validation.prepared_draw_count},
        {"nwn_sidecar_draws", validation.nwn_sidecar_draw_count},
        {"instance_offsets", validation.instance_offset_count},
        {"expected_stats", prepared_model_draw_stats_json(validation.expected_stats)},
        {"prepared_stats", prepared_model_draw_stats_json(validation.prepared_stats)},
        {"expected_materials", prepared_model_draw_material_stats_json(validation.expected_materials)},
        {"prepared_materials", prepared_model_draw_material_stats_json(validation.prepared_materials)},
    };
}

json prepared_model_surface_stats_json(const nw::render::PreparedModelSurfaceDrawStats& stats)
{
    return {
        {"ranges", stats.range_count},
        {"draws", stats.draw_count},
        {"render_model_draws", stats.render_model_draw_count},
        {"nwn_legacy_draws", stats.nwn_legacy_draw_count},
        {"shadow_casters", stats.shadow_caster_draw_count},
        {"invalid_ranges", stats.invalid_range_count},
        {"range_mismatches", stats.range_mismatch_count},
    };
}

json prepared_model_surface_material_stats_json(
    const nw::render::PreparedModelSurfaceMaterialStats& stats)
{
    return {
        {"opaque", stats.opaque_count},
        {"cutout", stats.cutout_count},
        {"water", stats.water_count},
        {"transparent", stats.transparent_count},
        {"invalid_modes", stats.invalid_mode_count},
        {"total", stats.total()},
    };
}

json prepared_model_surface_material_binding_stats_json(
    const nw::render::PreparedModelSurfaceMaterialBindingStats& stats)
{
    return {
        {"passed", stats.valid()},
        {"surfaces", stats.surface_count},
        {"bound_surfaces", stats.bound_surface_count},
        {"invalid_ranges", stats.invalid_range_count},
        {"invalid_draw_indices", stats.invalid_draw_index_count},
        {"range_mismatches", stats.range_mismatch_count},
        {"payload_mismatches", stats.payload_mismatch_count},
        {"material_mismatches", stats.material_mismatch_count},
        {"material_fallbacks", stats.material_fallback_count},
        {"render_model_material_fallbacks", stats.render_model_material_fallback_count},
        {"nwn_legacy_material_fallbacks", stats.nwn_legacy_material_fallback_count},
        {"fallback_material_payloads", stats.fallback_material_payload_count},
        {"render_model_fallback_material_payloads", stats.render_model_fallback_material_payload_count},
        {"nwn_legacy_fallback_material_payloads", stats.nwn_legacy_fallback_material_payload_count},
        {"shadow_mismatches", stats.shadow_mismatch_count},
        {"materials", prepared_model_surface_material_stats_json(stats.materials)},
    };
}

json prepared_render_model_skin_table_stats_json(
    const nw::render::PreparedRenderModelSkinTableStats& stats)
{
    return {
        {"passed", stats.valid()},
        {"input_surfaces", stats.input_surface_count},
        {"render_model_skinned_surfaces", stats.render_model_skinned_surface_count},
        {"assigned_surfaces", stats.assigned_surface_count},
        {"reused_surfaces", stats.reused_surface_count},
        {"bind_pose_fallback_surfaces", stats.bind_pose_fallback_surface_count},
        {"unskinned_surfaces", stats.unskinned_surface_count},
        {"non_render_model_surfaces", stats.non_render_model_surface_count},
        {"stale_instances", stats.stale_instance_count},
        {"invalid_skin_indices", stats.invalid_skin_index_count},
        {"invalid_matrix_ranges", stats.invalid_matrix_range_count},
        {"table_entries", stats.table_entry_count},
        {"matrices", stats.matrix_count},
    };
}

json model_instance_animation_sample_stats_json(
    const nw::render::ModelInstanceAnimationSampleStats& stats)
{
    return {
        {"inputs", stats.input_count},
        {"sampled", stats.sampled_count},
        {"null_inputs", stats.null_input_count},
        {"disabled", stats.disabled_count},
        {"missing_asset_data", stats.missing_asset_data_count},
        {"invalid_skeletons", stats.invalid_skeleton_count},
        {"failed_samples", stats.failed_sample_count},
    };
}

json preview_scene_runtime_sync_stats_json(
    const nw::render::viewer::PreviewSceneRuntimeSyncStats& stats)
{
    return {
        {"nwn_models", stats.nwn_model_count},
        {"render_models", stats.render_model_count},
        {"nwn_attachment_bindings", stats.nwn_attachment_binding_count},
        {"nwn_attachment_roots_resolved", stats.nwn_attachment_root_resolved_count},
        {"nwn_attachment_roots_failed", stats.nwn_attachment_root_failed_count},
        {"render_model_attachment_bindings", stats.render_model_attachment_binding_count},
        {"render_model_attachment_roots_resolved", stats.render_model_attachment_root_resolved_count},
        {"render_model_attachment_roots_failed", stats.render_model_attachment_root_failed_count},
    };
}

json prepared_model_surface_report_json(
    bool enabled,
    const nw::render::PreparedModelSurfaceDrawStats& stats,
    const nw::render::PreparedModelSurfaceMaterialStats& materials,
    const nw::render::PreparedModelSurfaceMaterialBindingStats& material_bindings,
    const nw::render::PreparedRenderModelSkinTableStats& skin_table)
{
    return {
        {"enabled", enabled},
        {"passed", stats.valid()},
        {"stats", prepared_model_surface_stats_json(stats)},
        {"materials", prepared_model_surface_material_stats_json(materials)},
        {"material_bindings", prepared_model_surface_material_binding_stats_json(material_bindings)},
        {"render_model_skin_table", prepared_render_model_skin_table_stats_json(skin_table)},
    };
}

json prepared_render_model_surface_submission_stats_json(
    const nw::render::PreparedRenderModelSurfaceSubmissionStats& stats)
{
    return {
        {"passed", stats.valid()},
        {"submitted_surfaces", stats.submitted_surface_count},
        {"dropped_invalid_surfaces", stats.dropped_invalid_surface_count},
        {"submitted_runs", stats.submitted_run_count},
        {"skin_bindings", {
                              {"passed", stats.skin_bindings.valid()},
                              {"skinned_surfaces", stats.skin_bindings.skinned_surface_count},
                              {"table_bound_surfaces", stats.skin_bindings.table_bound_surface_count},
                              {"bind_pose_fallback_surfaces", stats.skin_bindings.bind_pose_fallback_surface_count},
                              {"missing_table_surfaces", stats.skin_bindings.missing_table_surface_count},
                              {"invalid_table_index_surfaces", stats.skin_bindings.invalid_table_index_surface_count},
                              {"invalid_table_range_surfaces", stats.skin_bindings.invalid_table_range_surface_count},
                              {"fallback_surfaces", stats.skin_bindings.fallback_surface_count()},
                          }},
        {"submitted_runs_by_pass", {
                                       {"opaque_cutout", stats.opaque_cutout_submitted_run_count},
                                       {"water", stats.water_submitted_run_count},
                                       {"transparent", stats.transparent_submitted_run_count},
                                       {"all", stats.all_submitted_run_count},
                                   }},
    };
}

bool prepared_nwn_legacy_surface_submission_valid(
    const nw::render::viewer::ViewerFrameStats& stats) noexcept
{
    return stats.prepared_nwn_legacy_missing_sidecar_draw_count == 0
        && stats.prepared_nwn_legacy_invalid_sidecar_draw_count == 0;
}

json prepared_nwn_legacy_surface_submission_stats_json(
    const nw::render::viewer::ViewerFrameStats& stats)
{
    const uint64_t dropped_draws = static_cast<uint64_t>(stats.prepared_nwn_legacy_missing_sidecar_draw_count)
        + static_cast<uint64_t>(stats.prepared_nwn_legacy_invalid_sidecar_draw_count);
    return {
        {"passed", prepared_nwn_legacy_surface_submission_valid(stats)},
        {"selected_draws", stats.prepared_nwn_legacy_selected_draw_count},
        {"missing_sidecars", stats.prepared_nwn_legacy_missing_sidecar_draw_count},
        {"invalid_sidecars", stats.prepared_nwn_legacy_invalid_sidecar_draw_count},
        {"dropped_draws", dropped_draws},
    };
}

json prepared_model_surface_submission_stats_json(
    const nw::render::viewer::ViewerFrameStats& stats)
{
    return {
        {"nwn_legacy", prepared_nwn_legacy_surface_submission_stats_json(stats)},
        {"render_model", prepared_render_model_surface_submission_stats_json(stats.prepared_render_model_surface_submission)},
    };
}

json area_direct_model_submission_stats_json(
    const nw::render::viewer::AreaDirectModelSubmissionStats& stats)
{
    return {
        {"passed", stats.valid()},
        {"input_records", stats.input_record_count},
        {"selected_legacy_records", stats.selected_legacy_record_count},
        {"selected_render_model_records", stats.selected_render_model_record_count},
        {"skipped_render_model_records", stats.skipped_render_model_record_count},
        {"dropped_invalid_records", stats.dropped_invalid_record_count},
        {"dropped_invalid_sources", stats.dropped_invalid_source_count},
        {"dropped_unsupported_kinds", stats.dropped_unsupported_kind_count},
        {"dropped_records", stats.dropped_record_count()},
    };
}

json area_prepared_surface_sidecar_stats_json(
    const nw::render::viewer::AreaPreparedSurfaceSidecarStats& stats)
{
    return {
        {"passed", stats.valid()},
        {"input_surfaces", stats.input_surface_count},
        {"selected_draws", stats.selected_draw_count},
        {"dropped_non_nwn_surfaces", stats.dropped_non_nwn_surface_count},
        {"dropped_invalid_surface_indices", stats.dropped_invalid_surface_index_count},
        {"dropped_missing_sidecars", stats.dropped_missing_sidecar_draw_count},
        {"dropped_invalid_sidecars", stats.dropped_invalid_sidecar_draw_count},
        {"dropped_surfaces", stats.dropped_surface_count()},
    };
}

void log_prepared_model_surface_submission_stats(
    const nw::render::viewer::ViewerFrameStats& stats)
{
    const auto& submission = stats.prepared_render_model_surface_submission;
    LOG_F(INFO,
        "Prepared model surface submission: nwn_enabled={} nwn_selected={} nwn_missing_sidecars={} "
        "nwn_invalid_sidecars={} render_model_enabled={} render_model_submitted_surfaces={} "
        "render_model_dropped_invalid_surfaces={} render_model_submitted_runs={} "
        "render_model_opaque_cutout_runs={} render_model_water_runs={} "
        "render_model_transparent_runs={} render_model_all_runs={} "
        "prepared_surface_opaque={} prepared_surface_cutout={} prepared_surface_water={} "
        "prepared_surface_transparent={} prepared_surface_invalid_material_modes={} "
        "prepared_surface_material_mismatches={} prepared_surface_material_fallbacks={} "
        "prepared_surface_fallback_payloads={} "
        "render_model_skin_table_skinned_surfaces={} render_model_skin_table_assigned_surfaces={} "
        "render_model_skin_table_entries={} render_model_skin_table_matrices={} "
        "render_model_skin_skinned_surfaces={} render_model_skin_table_bound_surfaces={} "
        "render_model_skin_bind_pose_fallback_surfaces={} render_model_skin_missing_table_surfaces={} "
        "render_model_skin_invalid_table_index_surfaces={} render_model_skin_invalid_table_range_surfaces={}",
        stats.prepared_nwn_legacy_draws_enabled,
        stats.prepared_nwn_legacy_selected_draw_count,
        stats.prepared_nwn_legacy_missing_sidecar_draw_count,
        stats.prepared_nwn_legacy_invalid_sidecar_draw_count,
        stats.prepared_render_model_draws_enabled,
        submission.submitted_surface_count,
        submission.dropped_invalid_surface_count,
        submission.submitted_run_count,
        submission.opaque_cutout_submitted_run_count,
        submission.water_submitted_run_count,
        submission.transparent_submitted_run_count,
        submission.all_submitted_run_count,
        stats.prepared_model_surface_materials.opaque_count,
        stats.prepared_model_surface_materials.cutout_count,
        stats.prepared_model_surface_materials.water_count,
        stats.prepared_model_surface_materials.transparent_count,
        stats.prepared_model_surface_materials.invalid_mode_count,
        stats.prepared_model_surface_material_bindings.material_mismatch_count,
        stats.prepared_model_surface_material_bindings.material_fallback_count,
        stats.prepared_model_surface_material_bindings.fallback_material_payload_count,
        stats.prepared_render_model_skin_table_stats.render_model_skinned_surface_count,
        stats.prepared_render_model_skin_table_stats.assigned_surface_count,
        stats.prepared_render_model_skin_table_stats.table_entry_count,
        stats.prepared_render_model_skin_table_stats.matrix_count,
        submission.skin_bindings.skinned_surface_count,
        submission.skin_bindings.table_bound_surface_count,
        submission.skin_bindings.bind_pose_fallback_surface_count,
        submission.skin_bindings.missing_table_surface_count,
        submission.skin_bindings.invalid_table_index_surface_count,
        submission.skin_bindings.invalid_table_range_surface_count);
}

void log_preview_scene_runtime_sync_stats(
    const nw::render::viewer::ViewerFrameStats& stats)
{
    const auto& sync = stats.runtime_sync_stats;
    LOG_F(INFO,
        "Preview runtime sync: nwn_models={} render_models={} "
        "nwn_attachment_bindings={} nwn_attachment_roots_resolved={} nwn_attachment_roots_failed={} "
        "render_model_attachment_bindings={} render_model_attachment_roots_resolved={} "
        "render_model_attachment_roots_failed={}",
        sync.nwn_model_count,
        sync.render_model_count,
        sync.nwn_attachment_binding_count,
        sync.nwn_attachment_root_resolved_count,
        sync.nwn_attachment_root_failed_count,
        sync.render_model_attachment_binding_count,
        sync.render_model_attachment_root_resolved_count,
        sync.render_model_attachment_root_failed_count);
}

json frame_stats_json(const nw::render::viewer::ViewerFrameStats& stats)
{
    return {
        {"timings_ms", {
                           {"tick", seconds_to_ms(stats.tick_seconds)},
                           {"setup", seconds_to_ms(stats.setup_seconds)},
                           {"shadow", seconds_to_ms(stats.shadow_seconds)},
                           {"opaque", seconds_to_ms(stats.opaque_seconds)},
                           {"water", seconds_to_ms(stats.water_seconds)},
                           {"transparent", seconds_to_ms(stats.transparent_seconds)},
                           {"particles", seconds_to_ms(stats.particles_seconds)},
                           {"debug", seconds_to_ms(stats.debug_seconds)},
                           {"area_prepare", seconds_to_ms(stats.area_prepare_seconds)},
                           {"local_shadow", seconds_to_ms(stats.local_shadow_seconds)},
                           {"forward_plus_prepare", seconds_to_ms(stats.forward_plus_prepare_seconds)},
                           {"forward_plus_light_cull", seconds_to_ms(stats.forward_plus_light_cull_seconds)},
                           {"forward_plus_cluster_header", seconds_to_ms(stats.forward_plus_cluster_header_seconds)},
                           {"forward_plus_cluster_index", seconds_to_ms(stats.forward_plus_cluster_index_seconds)},
                           {"forward_plus_upload", seconds_to_ms(stats.forward_plus_upload_seconds)},
                           {"total_render", seconds_to_ms(stats.total_render_seconds)},
                       }},
        {"gpu_timings_ms", {
                               {"shadow", seconds_to_ms(stats.gpu_shadow_seconds)},
                               {"opaque", seconds_to_ms(stats.gpu_opaque_seconds)},
                               {"water", seconds_to_ms(stats.gpu_water_seconds)},
                               {"transparent", seconds_to_ms(stats.gpu_transparent_seconds)},
                               {"particles", seconds_to_ms(stats.gpu_particles_seconds)},
                               {"debug", seconds_to_ms(stats.gpu_debug_seconds)},
                               {"forward_plus_cull", seconds_to_ms(stats.gpu_forward_plus_cull_seconds)},
                           }},
        {"counts", {
                       {"gpu_timers", stats.gpu_timer_count},
                       {"models", stats.model_count},
                       {"static_models", stats.static_model_count},
                       {"particle_systems", stats.particle_system_count},
                       {"render_model_animation_sample_inputs", stats.render_model_animation_sample_stats.input_count},
                       {"render_model_animation_samples", stats.render_model_animation_sample_stats.sampled_count},
                       {"render_model_animation_disabled_samples", stats.render_model_animation_sample_stats.disabled_count},
                       {"render_model_animation_missing_asset_data", stats.render_model_animation_sample_stats.missing_asset_data_count},
                       {"render_model_animation_invalid_skeletons", stats.render_model_animation_sample_stats.invalid_skeleton_count},
                       {"render_model_animation_failed_samples", stats.render_model_animation_sample_stats.failed_sample_count},
                       {"runtime_sync_nwn_models", stats.runtime_sync_stats.nwn_model_count},
                       {"runtime_sync_render_models", stats.runtime_sync_stats.render_model_count},
                       {"runtime_sync_nwn_attachment_bindings", stats.runtime_sync_stats.nwn_attachment_binding_count},
                       {"runtime_sync_nwn_attachment_roots_resolved", stats.runtime_sync_stats.nwn_attachment_root_resolved_count},
                       {"runtime_sync_nwn_attachment_roots_failed", stats.runtime_sync_stats.nwn_attachment_root_failed_count},
                       {"runtime_sync_render_model_attachment_bindings", stats.runtime_sync_stats.render_model_attachment_binding_count},
                       {"runtime_sync_render_model_attachment_roots_resolved", stats.runtime_sync_stats.render_model_attachment_root_resolved_count},
                       {"runtime_sync_render_model_attachment_roots_failed", stats.runtime_sync_stats.render_model_attachment_root_failed_count},
                       {"prepared_render_model_draws", stats.prepared_render_model_draw_count},
                       {"prepared_nwn_legacy_draws", stats.prepared_nwn_legacy_draw_count},
                       {"prepared_nwn_legacy_selected_draws", stats.prepared_nwn_legacy_selected_draw_count},
                       {"prepared_nwn_legacy_missing_sidecars", stats.prepared_nwn_legacy_missing_sidecar_draw_count},
                       {"prepared_nwn_legacy_invalid_sidecars", stats.prepared_nwn_legacy_invalid_sidecar_draw_count},
                       {"prepared_model_draw_material_fallbacks", stats.prepared_model_draw_material_fallback_count},
                       {"prepared_model_draw_render_model_material_fallbacks", stats.prepared_model_draw_render_model_material_fallback_count},
                       {"prepared_model_draw_nwn_legacy_material_fallbacks", stats.prepared_model_draw_nwn_legacy_material_fallback_count},
                       {"prepared_model_surface_ranges", stats.prepared_model_surface_stats.range_count},
                       {"prepared_model_surfaces", stats.prepared_model_surface_stats.draw_count},
                       {"prepared_model_surface_render_model_draws", stats.prepared_model_surface_stats.render_model_draw_count},
                       {"prepared_model_surface_nwn_legacy_draws", stats.prepared_model_surface_stats.nwn_legacy_draw_count},
                       {"prepared_model_surface_shadow_casters", stats.prepared_model_surface_stats.shadow_caster_draw_count},
                       {"prepared_model_surface_invalid_ranges", stats.prepared_model_surface_stats.invalid_range_count},
                       {"prepared_model_surface_range_mismatches", stats.prepared_model_surface_stats.range_mismatch_count},
                       {"prepared_render_model_skin_table_skinned_surfaces", stats.prepared_render_model_skin_table_stats.render_model_skinned_surface_count},
                       {"prepared_render_model_skin_table_assigned_surfaces", stats.prepared_render_model_skin_table_stats.assigned_surface_count},
                       {"prepared_render_model_skin_table_entries", stats.prepared_render_model_skin_table_stats.table_entry_count},
                       {"prepared_render_model_skin_table_matrices", stats.prepared_render_model_skin_table_stats.matrix_count},
                       {"prepared_render_model_skin_table_bind_pose_fallbacks", stats.prepared_render_model_skin_table_stats.bind_pose_fallback_surface_count},
                       {"prepared_render_model_skin_table_invalid_skin_indices", stats.prepared_render_model_skin_table_stats.invalid_skin_index_count},
                       {"prepared_model_surface_bound_materials", stats.prepared_model_surface_material_bindings.bound_surface_count},
                       {"prepared_model_surface_material_binding_invalid_ranges", stats.prepared_model_surface_material_bindings.invalid_range_count},
                       {"prepared_model_surface_material_binding_invalid_draws", stats.prepared_model_surface_material_bindings.invalid_draw_index_count},
                       {"prepared_model_surface_material_binding_mismatches", stats.prepared_model_surface_material_bindings.material_mismatch_count},
                       {"prepared_model_surface_material_fallbacks", stats.prepared_model_surface_material_bindings.material_fallback_count},
                       {"prepared_model_surface_render_model_material_fallbacks", stats.prepared_model_surface_material_bindings.render_model_material_fallback_count},
                       {"prepared_model_surface_nwn_legacy_material_fallbacks", stats.prepared_model_surface_material_bindings.nwn_legacy_material_fallback_count},
                       {"prepared_model_surface_fallback_material_payloads", stats.prepared_model_surface_material_bindings.fallback_material_payload_count},
                       {"prepared_model_surface_render_model_fallback_material_payloads", stats.prepared_model_surface_material_bindings.render_model_fallback_material_payload_count},
                       {"prepared_model_surface_nwn_legacy_fallback_material_payloads", stats.prepared_model_surface_material_bindings.nwn_legacy_fallback_material_payload_count},
                       {"prepared_render_model_surface_submitted_surfaces", stats.prepared_render_model_surface_submission.submitted_surface_count},
                       {"prepared_render_model_surface_dropped_invalid_surfaces", stats.prepared_render_model_surface_submission.dropped_invalid_surface_count},
                       {"prepared_render_model_surface_submitted_runs", stats.prepared_render_model_surface_submission.submitted_run_count},
                       {"prepared_render_model_surface_opaque_cutout_runs", stats.prepared_render_model_surface_submission.opaque_cutout_submitted_run_count},
                       {"prepared_render_model_surface_water_runs", stats.prepared_render_model_surface_submission.water_submitted_run_count},
                       {"prepared_render_model_surface_transparent_runs", stats.prepared_render_model_surface_submission.transparent_submitted_run_count},
                       {"prepared_render_model_surface_all_runs", stats.prepared_render_model_surface_submission.all_submitted_run_count},
                       {"prepared_render_model_surface_skin_skinned_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.skinned_surface_count},
                       {"prepared_render_model_surface_skin_table_bound_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.table_bound_surface_count},
                       {"prepared_render_model_surface_skin_bind_pose_fallback_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.bind_pose_fallback_surface_count},
                       {"prepared_render_model_surface_skin_missing_table_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.missing_table_surface_count},
                       {"prepared_render_model_surface_skin_invalid_table_index_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.invalid_table_index_surface_count},
                       {"prepared_render_model_surface_skin_invalid_table_range_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.invalid_table_range_surface_count},
                       {"particle_submitted_systems", stats.particle_submitted_system_count},
                       {"particle_culled_systems", stats.particle_culled_system_count},
                       {"particle_invalid_material_packets", stats.particle_invalid_material_packet_count},
                       {"particle_mesh_packets", stats.particle_mesh_packet_count},
                       {"particle_mesh_particles", stats.particle_mesh_particle_count},
                       {"particle_mesh_submitted_particles", stats.particle_mesh_submitted_particle_count},
                       {"particle_mesh_dropped_particles", stats.particle_mesh_dropped_particle_count},
                       {"particle_mesh_missing_resref_packets", stats.particle_mesh_missing_resref_packet_count},
                       {"particle_mesh_missing_model_packets", stats.particle_mesh_missing_model_packet_count},
                       {"particle_mesh_invalid_particle_indices", stats.particle_mesh_invalid_particle_index_count},
                       {"area_cache_records", stats.area_cache_record_count},
                       {"area_cache_static_records", stats.area_cache_static_record_count},
                       {"area_cache_dynamic_records", stats.area_cache_dynamic_record_count},
                       {"area_cache_opaque_records", stats.area_cache_opaque_record_count},
                       {"area_cache_water_records", stats.area_cache_water_record_count},
                       {"area_cache_transparent_records", stats.area_cache_transparent_record_count},
                       {"area_cache_shadow_caster_records", stats.area_cache_shadow_caster_record_count},
                       {"area_cache_prepared_draws", stats.area_cache_prepared_draw_count},
                       {"area_cache_shadow_prepared_draws", stats.area_cache_shadow_prepared_surface_count},
                       {"area_cache_static_geometry_meshes", stats.area_cache_static_geometry_mesh_count},
                       {"area_cache_static_geometry_vertices", stats.area_cache_static_geometry_vertex_count},
                       {"area_cache_static_geometry_indices", stats.area_cache_static_geometry_index_count},
                       {"area_cache_static_geometry_bytes", stats.area_cache_static_geometry_bytes},
                       {"area_cache_light_indices", stats.area_cache_light_index_count},
                       {"area_cache_local_lights", stats.area_cache_local_light_count},
                       {"area_cache_max_light_indices_per_record", stats.area_cache_max_light_indices_per_record},
                       {"area_cache_max_shadow_prepared_draws_per_record", stats.area_cache_max_shadow_prepared_surfaces_per_record},
                       {"area_cache_chunk_light_indices", stats.area_cache_chunk_light_index_count},
                       {"area_cache_max_light_indices_per_chunk", stats.area_cache_max_light_indices_per_chunk},
                       {"area_cache_chunks", stats.area_cache_chunk_count},
                       {"area_cache_nonempty_chunks", stats.area_cache_nonempty_chunk_count},
                       {"area_cache_max_records_per_chunk", stats.area_cache_max_records_per_chunk},
                       {"area_frame_visible_records", stats.area_frame_visible_record_count},
                       {"area_frame_visible_static_records", stats.area_frame_visible_static_record_count},
                       {"area_frame_visible_dynamic_records", stats.area_frame_visible_dynamic_record_count},
                       {"area_frame_visible_chunks", stats.area_frame_visible_chunk_count},
                       {"area_frame_culled_records", stats.area_frame_culled_record_count},
                       {"area_frame_culled_chunks", stats.area_frame_culled_chunk_count},
                       {"area_frame_visibility_culled_records", stats.area_frame_visibility_culled_record_count},
                       {"area_frame_visibility_culled_chunks", stats.area_frame_visibility_culled_chunk_count},
                       {"area_frame_opaque_records", stats.area_frame_opaque_record_count},
                       {"area_frame_water_records", stats.area_frame_water_record_count},
                       {"area_frame_transparent_records", stats.area_frame_transparent_record_count},
                       {"area_frame_shadow_caster_records", stats.area_frame_shadow_caster_record_count},
                       {"area_frame_visible_prepared_draws", stats.area_frame_visible_prepared_surface_count},
                       {"area_frame_visible_lights", stats.area_frame_visible_light_count},
                       {"area_frame_uses_cached_draw_lists", stats.area_frame_uses_cached_draw_lists},
                       {"area_frame_uses_sorted_static_draw_lists", stats.area_frame_uses_sorted_static_draw_lists},
                       {"area_frame_material_indirect_sidecar",
                           area_prepared_surface_sidecar_stats_json(stats.area_frame_material_indirect_sidecar_bridge)},
                       {"area_frame_static_material_indirect_sidecar",
                           area_prepared_surface_sidecar_stats_json(stats.area_frame_static_material_indirect_sidecar_bridge)},
                       {"area_static_material_draw_data_sidecar",
                           area_prepared_surface_sidecar_stats_json(stats.area_static_material_draw_data_sidecar_bridge)},
                       {"area_static_material_sidecar", area_prepared_surface_sidecar_stats_json(stats.area_static_material_sidecar_submission)},
                       {"area_static_material_sidecar_input_surfaces", stats.area_static_material_sidecar_submission.input_surface_count},
                       {"area_static_material_sidecar_selected_draws", stats.area_static_material_sidecar_submission.selected_draw_count},
                       {"area_static_material_sidecar_dropped_surfaces", stats.area_static_material_sidecar_submission.dropped_surface_count()},
                       {"area_static_material_sidecar_dropped_non_nwn_surfaces", stats.area_static_material_sidecar_submission.dropped_non_nwn_surface_count},
                       {"area_static_material_sidecar_dropped_invalid_surface_indices", stats.area_static_material_sidecar_submission.dropped_invalid_surface_index_count},
                       {"area_static_material_sidecar_dropped_missing_sidecars", stats.area_static_material_sidecar_submission.dropped_missing_sidecar_draw_count},
                       {"area_static_material_sidecar_dropped_invalid_sidecars", stats.area_static_material_sidecar_submission.dropped_invalid_sidecar_draw_count},
                       {"area_direct_model_input_records", stats.area_direct_model_submission.input_record_count},
                       {"area_direct_model_selected_legacy_records", stats.area_direct_model_submission.selected_legacy_record_count},
                       {"area_direct_model_selected_render_model_records", stats.area_direct_model_submission.selected_render_model_record_count},
                       {"area_direct_model_skipped_render_model_records", stats.area_direct_model_submission.skipped_render_model_record_count},
                       {"area_direct_model_dropped_invalid_records", stats.area_direct_model_submission.dropped_invalid_record_count},
                       {"area_direct_model_dropped_invalid_sources", stats.area_direct_model_submission.dropped_invalid_source_count},
                       {"area_direct_model_dropped_unsupported_kinds", stats.area_direct_model_submission.dropped_unsupported_kind_count},
                       {"area_direct_model_dropped_records", stats.area_direct_model_submission.dropped_record_count()},
                       {"local_lights", stats.local_light_count},
                       {"local_lights_authored_model", stats.local_light_authored_model_count},
                       {"local_lights_tile_model", stats.local_light_tile_model_count},
                       {"local_lights_placeable_table", stats.local_light_placeable_table_count},
                       {"local_lights_colored", stats.local_light_colored_count},
                       {"forward_plus_prepared", stats.forward_plus_prepared},
                       {"forward_plus_active", stats.forward_plus_active},
                       {"forward_plus_empty", stats.forward_plus_empty},
                       {"forward_plus_lights", stats.forward_plus_light_count},
                       {"forward_plus_clusters", stats.forward_plus_cluster_count},
                       {"forward_plus_active_clusters", stats.forward_plus_active_cluster_count},
                       {"forward_plus_cluster_light_indices", stats.forward_plus_cluster_light_index_count},
                       {"forward_plus_cluster_light_index_capacity", stats.forward_plus_cluster_light_index_capacity},
                       {"forward_plus_max_lights_per_cluster", stats.forward_plus_max_lights_per_cluster},
                       {"forward_plus_overflow_clusters", stats.forward_plus_overflow_cluster_count},
                       {"forward_plus_overflow_lights", stats.forward_plus_overflow_light_count},
                       {"forward_plus_upload_bytes", stats.forward_plus_upload_bytes},
                       {"forward_plus_tile_size", stats.forward_plus_tile_size},
                       {"forward_plus_depth_slices", stats.forward_plus_depth_slices},
                       {"shadow_cascades", stats.shadow_cascade_count},
                       {"shadow_resolution", stats.shadow_resolution},
                       {"shadow_caster_models", stats.shadow_caster_model_count},
                       {"shadow_no_caster_models", stats.shadow_no_caster_model_count},
                       {"shadow_submitted_models", stats.shadow_submitted_model_count},
                       {"shadow_culled_models", stats.shadow_culled_model_count},
                       {"shadow_prepared_surface_shadow_ranges", stats.shadow_prepared_surface_shadow_range_count},
                       {"shadow_prepared_surface_invalid_ranges", stats.shadow_prepared_surface_invalid_range_count},
                       {"shadow_area_indirect_sidecar",
                           area_prepared_surface_sidecar_stats_json(stats.shadow_area_indirect_sidecar_bridge)},
                       {"shadow_area_sidecar", area_prepared_surface_sidecar_stats_json(stats.shadow_area_sidecar_bridge)},
                       {"local_shadow_caster_lights", stats.local_shadow_caster_light_count},
                       {"local_shadow_submitted_models", stats.local_shadow_submitted_model_count},
                       {"local_shadow_culled_models", stats.local_shadow_culled_model_count},
                       {"local_shadow_area_sidecar", area_prepared_surface_sidecar_stats_json(stats.local_shadow_area_sidecar_bridge)},
                       {"static_batch_material_batches", stats.static_batch_material_batch_count},
                       {"static_batch_material_input_draws", stats.static_batch_material_input_draw_count},
                       {"static_batch_material_instances", stats.static_batch_material_instance_count},
                       {"static_batch_material_draw_calls", stats.static_batch_material_draw_call_count},
                       {"static_batch_material_fallback_draws", stats.static_batch_material_fallback_draw_count},
                       {"static_batch_material_failed_batch_attempt_draws", stats.static_batch_material_failed_batch_attempt_draw_count},
                       {"static_batch_material_indirect_calls", stats.static_batch_material_indirect_call_count},
                       {"static_batch_material_indirect_commands", stats.static_batch_material_indirect_command_count},
                       {"static_batch_material_max_instances_per_draw", stats.static_batch_material_max_instances_per_draw},
                       {"static_batch_material_indirect_command_upload_bytes", stats.static_batch_material_indirect_command_upload_bytes},
                       {"static_batch_material_cached_indirect_command_bytes", stats.static_batch_material_cached_indirect_command_bytes},
                       {"static_batch_material_draw_data_bytes", stats.static_batch_material_draw_data_bytes},
                       {"static_batch_material_draw_data_cache_hits", stats.static_batch_material_draw_data_cache_hit_count},
                       {"static_batch_material_cached_draw_data_bytes", stats.static_batch_material_cached_draw_data_bytes},
                       {"static_batch_shadow_batches", stats.static_batch_shadow_batch_count},
                       {"static_batch_shadow_input_draws", stats.static_batch_shadow_input_draw_count},
                       {"static_batch_shadow_instances", stats.static_batch_shadow_instance_count},
                       {"static_batch_shadow_draw_calls", stats.static_batch_shadow_draw_call_count},
                       {"static_batch_shadow_failed_batch_attempt_draws", stats.static_batch_shadow_failed_batch_attempt_draw_count},
                       {"static_batch_shadow_indirect_calls", stats.static_batch_shadow_indirect_call_count},
                       {"static_batch_shadow_indirect_commands", stats.static_batch_shadow_indirect_command_count},
                       {"static_batch_shadow_max_instances_per_draw", stats.static_batch_shadow_max_instances_per_draw},
                       {"static_batch_shadow_indirect_command_upload_bytes", stats.static_batch_shadow_indirect_command_upload_bytes},
                       {"static_batch_shadow_cached_indirect_command_bytes", stats.static_batch_shadow_cached_indirect_command_bytes},
                       {"static_batch_shadow_draw_data_bytes", stats.static_batch_shadow_draw_data_bytes},
                       {"static_batch_shadow_draw_data_cache_hits", stats.static_batch_shadow_draw_data_cache_hit_count},
                       {"static_batch_shadow_cached_draw_data_bytes", stats.static_batch_shadow_cached_draw_data_bytes},
                       {"main_passes", stats.main_pass_count},
                   }},
        {"flags", {
                      {"shadows_rendered", stats.shadows_rendered},
                      {"local_shadows_rendered", stats.local_shadows_rendered},
                      {"water_rendered", stats.water_rendered},
                      {"forward_plus_prepared", stats.forward_plus_prepared},
                      {"forward_plus_active", stats.forward_plus_active},
                      {"forward_plus_empty", stats.forward_plus_empty},
                      {"forward_plus_gpu_culling", stats.forward_plus_gpu_culling},
                      {"prepared_model_draw_validation_enabled", stats.prepared_model_draw_validation_enabled},
                      {"prepared_model_draw_validation_passed", stats.prepared_model_draw_validation.valid()},
                      {"prepared_render_model_draws_enabled", stats.prepared_render_model_draws_enabled},
                      {"prepared_nwn_legacy_draws_enabled", stats.prepared_nwn_legacy_draws_enabled},
                      {"prepared_model_surface_stats_enabled", stats.prepared_model_surface_stats_enabled},
                      {"prepared_model_surface_stats_passed", stats.prepared_model_surface_stats.valid()},
                      {"prepared_model_surface_material_bindings_passed", stats.prepared_model_surface_material_bindings.valid()},
                      {"prepared_nwn_legacy_surface_submission_passed", prepared_nwn_legacy_surface_submission_valid(stats)},
                      {"prepared_render_model_surface_submission_passed", stats.prepared_render_model_surface_submission.valid()},
                      {"area_direct_model_submission_passed", stats.area_direct_model_submission.valid()},
                  }},
        {"prepared_model_draw_validation", prepared_model_draw_validation_json(stats.prepared_model_draw_validation_enabled, stats.prepared_model_draw_validation)},
        {"render_model_animation_samples", model_instance_animation_sample_stats_json(stats.render_model_animation_sample_stats)},
        {"runtime_sync", preview_scene_runtime_sync_stats_json(stats.runtime_sync_stats)},
        {"prepared_model_surfaces", prepared_model_surface_report_json(stats.prepared_model_surface_stats_enabled, stats.prepared_model_surface_stats, stats.prepared_model_surface_materials, stats.prepared_model_surface_material_bindings, stats.prepared_render_model_skin_table_stats)},
        {"prepared_render_model_skin_table", prepared_render_model_skin_table_stats_json(stats.prepared_render_model_skin_table_stats)},
        {"prepared_model_surface_submission", prepared_model_surface_submission_stats_json(stats)},
        {"prepared_render_model_surface_submission", prepared_render_model_surface_submission_stats_json(stats.prepared_render_model_surface_submission)},
        {"area_direct_model_submission", area_direct_model_submission_stats_json(stats.area_direct_model_submission)},
        {"commands", command_stats_json(stats.total_command_stats)},
        {"pass_commands", {
                              {"shadow", command_stats_json(stats.shadow_command_stats)},
                              {"local_shadow", command_stats_json(stats.local_shadow_command_stats)},
                              {"opaque", command_stats_json(stats.opaque_command_stats)},
                              {"water", command_stats_json(stats.water_command_stats)},
                              {"transparent", command_stats_json(stats.transparent_command_stats)},
                              {"particles", command_stats_json(stats.particle_command_stats)},
                              {"debug", command_stats_json(stats.debug_command_stats)},
                          }},
    };
}

json area_sweep_frame_stats_json(const nw::render::viewer::ViewerFrameStats& stats)
{
    return {
        {"timings_ms", {
                           {"setup", seconds_to_ms(stats.setup_seconds)},
                           {"shadow", seconds_to_ms(stats.shadow_seconds)},
                           {"opaque", seconds_to_ms(stats.opaque_seconds)},
                           {"water", seconds_to_ms(stats.water_seconds)},
                           {"transparent", seconds_to_ms(stats.transparent_seconds)},
                           {"particles", seconds_to_ms(stats.particles_seconds)},
                           {"local_shadow", seconds_to_ms(stats.local_shadow_seconds)},
                           {"forward_plus_prepare", seconds_to_ms(stats.forward_plus_prepare_seconds)},
                           {"total_render", seconds_to_ms(stats.total_render_seconds)},
                       }},
        {"counts", {
                       {"models", stats.model_count},
                       {"particle_systems", stats.particle_system_count},
                       {"local_lights", stats.local_light_count},
                       {"forward_plus_lights", stats.forward_plus_light_count},
                       {"forward_plus_clusters", stats.forward_plus_cluster_count},
                       {"forward_plus_active_clusters", stats.forward_plus_active_cluster_count},
                       {"forward_plus_cluster_light_index_capacity", stats.forward_plus_cluster_light_index_capacity},
                       {"forward_plus_max_lights_per_cluster", stats.forward_plus_max_lights_per_cluster},
                       {"forward_plus_tile_size", stats.forward_plus_tile_size},
                       {"forward_plus_depth_slices", stats.forward_plus_depth_slices},
                       {"area_cache_records", stats.area_cache_record_count},
                       {"area_frame_visible_records", stats.area_frame_visible_record_count},
                       {"area_frame_visible_chunks", stats.area_frame_visible_chunk_count},
                       {"area_frame_visibility_culled_records", stats.area_frame_visibility_culled_record_count},
                       {"area_frame_shadow_caster_records", stats.area_frame_shadow_caster_record_count},
                       {"area_frame_material_indirect_sidecar",
                           area_prepared_surface_sidecar_stats_json(stats.area_frame_material_indirect_sidecar_bridge)},
                       {"area_frame_static_material_indirect_sidecar",
                           area_prepared_surface_sidecar_stats_json(stats.area_frame_static_material_indirect_sidecar_bridge)},
                       {"area_static_material_draw_data_sidecar",
                           area_prepared_surface_sidecar_stats_json(stats.area_static_material_draw_data_sidecar_bridge)},
                       {"area_static_material_sidecar",
                           area_prepared_surface_sidecar_stats_json(stats.area_static_material_sidecar_submission)},
                       {"area_direct_model_input_records", stats.area_direct_model_submission.input_record_count},
                       {"area_direct_model_selected_legacy_records", stats.area_direct_model_submission.selected_legacy_record_count},
                       {"area_direct_model_selected_render_model_records", stats.area_direct_model_submission.selected_render_model_record_count},
                       {"area_direct_model_skipped_render_model_records", stats.area_direct_model_submission.skipped_render_model_record_count},
                       {"area_direct_model_dropped_invalid_records", stats.area_direct_model_submission.dropped_invalid_record_count},
                       {"area_direct_model_dropped_invalid_sources", stats.area_direct_model_submission.dropped_invalid_source_count},
                       {"area_direct_model_dropped_unsupported_kinds", stats.area_direct_model_submission.dropped_unsupported_kind_count},
                       {"area_direct_model_dropped_records", stats.area_direct_model_submission.dropped_record_count()},
                       {"static_batch_material_draw_calls", stats.static_batch_material_draw_call_count},
                       {"static_batch_material_indirect_calls", stats.static_batch_material_indirect_call_count},
                       {"static_batch_material_indirect_commands", stats.static_batch_material_indirect_command_count},
                       {"static_batch_shadow_draw_calls", stats.static_batch_shadow_draw_call_count},
                       {"static_batch_shadow_indirect_calls", stats.static_batch_shadow_indirect_call_count},
                       {"static_batch_shadow_indirect_commands", stats.static_batch_shadow_indirect_command_count},
                       {"local_shadow_caster_lights", stats.local_shadow_caster_light_count},
                       {"local_shadow_submitted_models", stats.local_shadow_submitted_model_count},
                       {"local_shadow_culled_models", stats.local_shadow_culled_model_count},
                       {"shadow_prepared_surface_shadow_ranges", stats.shadow_prepared_surface_shadow_range_count},
                       {"shadow_prepared_surface_invalid_ranges", stats.shadow_prepared_surface_invalid_range_count},
                       {"shadow_area_indirect_sidecar",
                           area_prepared_surface_sidecar_stats_json(stats.shadow_area_indirect_sidecar_bridge)},
                       {"shadow_area_sidecar", area_prepared_surface_sidecar_stats_json(stats.shadow_area_sidecar_bridge)},
                       {"local_shadow_area_sidecar", area_prepared_surface_sidecar_stats_json(stats.local_shadow_area_sidecar_bridge)},
                       {"prepared_render_model_draws", stats.prepared_render_model_draw_count},
                       {"prepared_nwn_legacy_draws", stats.prepared_nwn_legacy_draw_count},
                       {"prepared_nwn_legacy_selected_draws", stats.prepared_nwn_legacy_selected_draw_count},
                       {"prepared_nwn_legacy_missing_sidecars", stats.prepared_nwn_legacy_missing_sidecar_draw_count},
                       {"prepared_nwn_legacy_invalid_sidecars", stats.prepared_nwn_legacy_invalid_sidecar_draw_count},
                       {"prepared_model_draw_material_fallbacks", stats.prepared_model_draw_material_fallback_count},
                       {"prepared_model_draw_render_model_material_fallbacks", stats.prepared_model_draw_render_model_material_fallback_count},
                       {"prepared_model_draw_nwn_legacy_material_fallbacks", stats.prepared_model_draw_nwn_legacy_material_fallback_count},
                       {"prepared_model_surface_ranges", stats.prepared_model_surface_stats.range_count},
                       {"prepared_model_surfaces", stats.prepared_model_surface_stats.draw_count},
                       {"prepared_model_surface_render_model_draws", stats.prepared_model_surface_stats.render_model_draw_count},
                       {"prepared_model_surface_nwn_legacy_draws", stats.prepared_model_surface_stats.nwn_legacy_draw_count},
                       {"prepared_model_surface_shadow_casters", stats.prepared_model_surface_stats.shadow_caster_draw_count},
                       {"prepared_model_surface_invalid_ranges", stats.prepared_model_surface_stats.invalid_range_count},
                       {"prepared_model_surface_range_mismatches", stats.prepared_model_surface_stats.range_mismatch_count},
                       {"prepared_render_model_skin_table_skinned_surfaces", stats.prepared_render_model_skin_table_stats.render_model_skinned_surface_count},
                       {"prepared_render_model_skin_table_assigned_surfaces", stats.prepared_render_model_skin_table_stats.assigned_surface_count},
                       {"prepared_render_model_skin_table_entries", stats.prepared_render_model_skin_table_stats.table_entry_count},
                       {"prepared_render_model_skin_table_matrices", stats.prepared_render_model_skin_table_stats.matrix_count},
                       {"prepared_render_model_skin_table_bind_pose_fallbacks", stats.prepared_render_model_skin_table_stats.bind_pose_fallback_surface_count},
                       {"prepared_render_model_skin_table_invalid_skin_indices", stats.prepared_render_model_skin_table_stats.invalid_skin_index_count},
                       {"prepared_model_surface_bound_materials", stats.prepared_model_surface_material_bindings.bound_surface_count},
                       {"prepared_model_surface_material_binding_invalid_ranges", stats.prepared_model_surface_material_bindings.invalid_range_count},
                       {"prepared_model_surface_material_binding_invalid_draws", stats.prepared_model_surface_material_bindings.invalid_draw_index_count},
                       {"prepared_model_surface_material_binding_mismatches", stats.prepared_model_surface_material_bindings.material_mismatch_count},
                       {"prepared_model_surface_material_fallbacks", stats.prepared_model_surface_material_bindings.material_fallback_count},
                       {"prepared_model_surface_render_model_material_fallbacks", stats.prepared_model_surface_material_bindings.render_model_material_fallback_count},
                       {"prepared_model_surface_nwn_legacy_material_fallbacks", stats.prepared_model_surface_material_bindings.nwn_legacy_material_fallback_count},
                       {"prepared_model_surface_fallback_material_payloads", stats.prepared_model_surface_material_bindings.fallback_material_payload_count},
                       {"prepared_model_surface_render_model_fallback_material_payloads", stats.prepared_model_surface_material_bindings.render_model_fallback_material_payload_count},
                       {"prepared_model_surface_nwn_legacy_fallback_material_payloads", stats.prepared_model_surface_material_bindings.nwn_legacy_fallback_material_payload_count},
                       {"prepared_render_model_surface_submitted_surfaces", stats.prepared_render_model_surface_submission.submitted_surface_count},
                       {"prepared_render_model_surface_dropped_invalid_surfaces", stats.prepared_render_model_surface_submission.dropped_invalid_surface_count},
                       {"prepared_render_model_surface_submitted_runs", stats.prepared_render_model_surface_submission.submitted_run_count},
                       {"prepared_render_model_surface_opaque_cutout_runs", stats.prepared_render_model_surface_submission.opaque_cutout_submitted_run_count},
                       {"prepared_render_model_surface_water_runs", stats.prepared_render_model_surface_submission.water_submitted_run_count},
                       {"prepared_render_model_surface_transparent_runs", stats.prepared_render_model_surface_submission.transparent_submitted_run_count},
                       {"prepared_render_model_surface_all_runs", stats.prepared_render_model_surface_submission.all_submitted_run_count},
                       {"prepared_render_model_surface_skin_skinned_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.skinned_surface_count},
                       {"prepared_render_model_surface_skin_table_bound_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.table_bound_surface_count},
                       {"prepared_render_model_surface_skin_bind_pose_fallback_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.bind_pose_fallback_surface_count},
                       {"prepared_render_model_surface_skin_missing_table_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.missing_table_surface_count},
                       {"prepared_render_model_surface_skin_invalid_table_index_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.invalid_table_index_surface_count},
                       {"prepared_render_model_surface_skin_invalid_table_range_surfaces", stats.prepared_render_model_surface_submission.skin_bindings.invalid_table_range_surface_count},
                       {"prepared_model_draws", stats.prepared_model_draw_validation.prepared_draw_count},
                       {"prepared_model_draw_validation_mismatches", stats.prepared_model_draw_validation.protocol_mismatch_count},
                   }},
        {"flags", {
                      {"shadows_rendered", stats.shadows_rendered},
                      {"local_shadows_rendered", stats.local_shadows_rendered},
                      {"water_rendered", stats.water_rendered},
                      {"forward_plus_prepared", stats.forward_plus_prepared},
                      {"forward_plus_active", stats.forward_plus_active},
                      {"forward_plus_gpu_culling", stats.forward_plus_gpu_culling},
                      {"area_frame_uses_cached_draw_lists", stats.area_frame_uses_cached_draw_lists},
                      {"area_frame_uses_sorted_static_draw_lists", stats.area_frame_uses_sorted_static_draw_lists},
                      {"prepared_model_draw_validation_enabled", stats.prepared_model_draw_validation_enabled},
                      {"prepared_model_draw_validation_passed", stats.prepared_model_draw_validation.valid()},
                      {"prepared_render_model_draws_enabled", stats.prepared_render_model_draws_enabled},
                      {"prepared_nwn_legacy_draws_enabled", stats.prepared_nwn_legacy_draws_enabled},
                      {"prepared_model_surface_stats_enabled", stats.prepared_model_surface_stats_enabled},
                      {"prepared_model_surface_stats_passed", stats.prepared_model_surface_stats.valid()},
                      {"prepared_model_surface_material_bindings_passed", stats.prepared_model_surface_material_bindings.valid()},
                      {"prepared_render_model_surface_submission_passed", stats.prepared_render_model_surface_submission.valid()},
                      {"area_direct_model_submission_passed", stats.area_direct_model_submission.valid()},
                  }},
        {"commands", {
                         {"draws", stats.total_command_stats.draw_count},
                         {"local_shadow_draws", stats.local_shadow_command_stats.draw_count},
                         {"indirect_draw_calls", stats.total_command_stats.indirect_draw_call_count},
                         {"pipeline_binds", stats.total_command_stats.pipeline_bind_count},
                         {"resource_binds", stats.total_command_stats.resource_bind_count},
                         {"uniform_allocations", stats.total_command_stats.uniform_allocation_count},
                         {"uniform_allocation_bytes", stats.total_command_stats.uniform_allocation_bytes},
                     }},
    };
}

void fit_static_model_camera(nw::render::viewer::Camera& camera, const nw::render::viewer::Bounds& bounds)
{
    const float radius = std::max(bounds.radius(), 1.0f);
    camera.set_orbit_view(bounds.center(), radius * 2.25f, -90.0f, 12.0f, nw::render::viewer::Camera::ProjectionMode::perspective);
}

bool scene_uses_static_model_camera(const nw::render::viewer::PreviewScene& scene)
{
    for (const auto& model : scene.static_models) {
        if (model && model->source_kind != nw::render::ModelAssetSourceKind::nwn_legacy) {
            return true;
        }
    }
    return false;
}

void fit_model_preview_camera(nw::render::viewer::Camera& camera, const nw::render::viewer::PreviewScene& scene)
{
    if (scene_uses_static_model_camera(scene)) {
        fit_static_model_camera(camera, scene.current_bounds());
    } else {
        camera.fit_to_bounds(scene.current_bounds());
    }
}

void sync_area_day_night_state(AppState& state, bool log_transition)
{
    if (!state.current_scene || !nw::render::viewer::supports_area_day_night_cycle(*state.current_scene)) {
        return;
    }

    const bool day_night_changed = nw::render::viewer::sync_area_day_night_state(
        *state.current_scene, state.area_day_night_elapsed, log_transition);
    if (day_night_changed && state.preview_resources && state.current_scene->area_render_scene) {
        state.current_scene->area_render_scene->refresh_light_indices(*state.current_scene);
    }
}

void set_area_day_night_elapsed(AppState& state, float elapsed_seconds, bool log_transition)
{
    if (!state.current_scene || !nw::render::viewer::supports_area_day_night_cycle(*state.current_scene)) {
        state.area_day_night_elapsed = 0.0f;
        return;
    }

    state.area_day_night_elapsed = nw::render::viewer::normalize_area_day_night_elapsed_seconds(elapsed_seconds);
    sync_area_day_night_state(state, log_transition);
}

bool supports_area_day_night_controls_impl(const AppState& state)
{
    return state.current_scene && nw::render::viewer::supports_area_day_night_cycle(*state.current_scene);
}

bool supports_gltf_animation_controls_impl(const AppState& state)
{
    return state.current_scene && nw::render::viewer::supports_render_model_animations(*state.current_scene);
}

bool supports_vfx_sequence_controls_impl(const AppState& state)
{
    return state.current_scene && !state.vfx_sequence_steps.empty() && state.vfx_sequence_loop_ms > 0;
}

size_t render_model_skin_matrix_row_count(const nw::render::ModelInstance& instance) noexcept
{
    size_t result = 0;
    for (const auto& skin_matrices : instance.animation.skin_matrices) {
        result += skin_matrices.size();
    }
    return result;
}

size_t render_model_skinned_primitive_count(const nw::render::RenderModel& model) noexcept
{
    size_t result = 0;
    for (const auto& primitive : model.primitives) {
        if (primitive.skinned) {
            ++result;
        }
    }
    return result;
}

void log_render_model_animation_runtime_state(
    AppState& state,
    const nw::render::ModelInstanceAnimationSampleStats& sample_stats)
{
    if (state.render_model_animation_report_logged || !state.current_scene
        || state.current_scene->static_models.empty()) {
        return;
    }

    const auto& skin_table = state.prepared_model_surfaces.render_model_skins.stats;
    LOG_F(INFO,
        "RenderModel animation runtime: sample_inputs={} sampled={} disabled={} "
        "missing_asset_data={} invalid_skeletons={} failed_samples={} skinned_surfaces={} "
        "assigned_skin_surfaces={} skin_table_entries={} skin_table_matrices={} bind_pose_fallbacks={}",
        sample_stats.input_count,
        sample_stats.sampled_count,
        sample_stats.disabled_count,
        sample_stats.missing_asset_data_count,
        sample_stats.invalid_skeleton_count,
        sample_stats.failed_sample_count,
        skin_table.render_model_skinned_surface_count,
        skin_table.assigned_surface_count,
        skin_table.table_entry_count,
        skin_table.matrix_count,
        skin_table.bind_pose_fallback_surface_count);

    for (size_t model_index = 0; model_index < state.current_scene->static_models.size(); ++model_index) {
        const auto& model = state.current_scene->static_models[model_index];
        const auto* instance = state.current_scene->static_model_instance(model_index);
        if (!model || !instance || model->animations.empty()) {
            continue;
        }

        const uint32_t clip_index = instance->animation.clip % static_cast<uint32_t>(model->animations.size());
        const auto& clip = model->animations[clip_index];
        LOG_F(INFO,
            "RenderModel animation instance {} '{}': clip={} '{}' time={:.3f} enabled={} backend={} "
            "skinned_primitives={} skin_matrix_tables={} skin_matrix_rows={}",
            model_index,
            model->name,
            clip_index,
            clip.name.empty() ? "<unnamed>" : clip.name,
            instance->animation.time,
            instance->animation.enabled,
            instance->animation.backend ? "yes" : "no",
            render_model_skinned_primitive_count(*model),
            instance->animation.skin_matrices.size(),
            render_model_skin_matrix_row_count(*instance));
    }

    state.render_model_animation_report_logged = true;
}

void set_gltf_instance_animation_time(AppState& state, float time)
{
    if (!state.current_scene) {
        return;
    }

    state.gltf_animation_time = std::max(0.0f, time);
    nw::render::viewer::set_render_model_animation_time(*state.current_scene, state.gltf_animation_time);
}

void set_gltf_instance_animation_clip(AppState& state, uint32_t clip_index, float time)
{
    if (!state.current_scene) {
        return;
    }

    state.gltf_animation_clip = clip_index;
    state.gltf_animation_time = std::max(0.0f, time);
    nw::render::viewer::set_render_model_animation_clip(*state.current_scene, clip_index, state.gltf_animation_time);
    state.current_scene->rebuild_particles();
    state.render_model_animation_report_logged = false;
}

bool select_render_model_animation(AppState& state, std::string_view animation_name)
{
    if (!state.current_scene || animation_name.empty()) {
        return false;
    }

    uint32_t selected_clip = 0;
    bool found = false;
    for (const auto& model : state.current_scene->static_models) {
        if (!model) {
            continue;
        }

        for (uint32_t clip_index = 0; clip_index < static_cast<uint32_t>(model->animations.size()); ++clip_index) {
            if (model->animations[clip_index].name == animation_name) {
                selected_clip = clip_index;
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    if (!found) {
        return false;
    }

    const bool selected = nw::render::viewer::set_render_model_animation_clip_by_name(
        *state.current_scene,
        animation_name,
        0.0f);
    if (!selected) {
        return false;
    }

    state.gltf_animation_clip = selected_clip;
    state.gltf_animation_time = 0.0f;
    state.render_model_animation_report_logged = false;
    return true;
}

bool select_preferred_render_model_animation(AppState& state)
{
    if (!state.current_scene) {
        return false;
    }

    const auto animation_name = nw::render::viewer::find_first_render_model_animation_clip_name(
        *state.current_scene,
        kPreferredRenderModelHoldAnimations);
    if (!animation_name.empty()) {
        return select_render_model_animation(state, animation_name);
    }

    if (!nw::render::viewer::set_default_render_model_animation_clip(
            *state.current_scene,
            kPreferredRenderModelHoldAnimations,
            0.0f)) {
        return false;
    }
    state.gltf_animation_clip = 0;
    state.gltf_animation_time = 0.0f;
    state.render_model_animation_report_logged = false;
    return true;
}

void advance_gltf_instance_animation_times(AppState& state, float dt)
{
    if (!state.current_scene) {
        return;
    }

    dt = std::max(0.0f, dt);
    state.gltf_animation_time += dt;
    nw::render::viewer::advance_render_model_animation_times(*state.current_scene, dt);
}

std::vector<std::string> collect_model_animation_names(const nw::render::viewer::ModelInstance& model)
{
    std::vector<std::string> result;
    if (!model.scene_animation_enabled) {
        return result;
    }

    const nw::model::Mdl* source = model.animation_source_ ? model.animation_source_ : model.mdl_;
    while (source) {
        for (const auto& animation : source->model.animations) {
            if (!animation) {
                continue;
            }

            const std::string name = animation->name.c_str();
            if (!name.empty() && std::find(result.begin(), result.end(), name) == result.end()) {
                result.push_back(name);
            }
        }
        source = source->model.supermodel.get();
    }

    return result;
}

std::vector<std::string> collect_gltf_animation_names(const nw::render::RenderModel& model)
{
    return nw::render::viewer::collect_render_model_animation_names(model);
}

void rebuild_scene_animation_lists(AppState& state)
{
    state.model_animation_names.clear();
    state.gltf_animation_names.clear();
    if (!state.current_scene) {
        return;
    }

    state.model_animation_names.resize(state.current_scene->models.size());
    for (size_t i = 0; i < state.current_scene->models.size(); ++i) {
        const auto& model = state.current_scene->models[i];
        if (!model) {
            continue;
        }
        state.model_animation_names[i] = collect_model_animation_names(*model);
    }

    state.gltf_animation_names.resize(state.current_scene->static_models.size());
    for (size_t i = 0; i < state.current_scene->static_models.size(); ++i) {
        const auto& model = state.current_scene->static_models[i];
        if (!model) {
            continue;
        }
        state.gltf_animation_names[i] = collect_gltf_animation_names(*model);
    }
}

void rebuild_gltf_animation_instances(AppState& state)
{
    if (!state.current_scene) return;

    nw::render::viewer::rebuild_render_model_animation_instances(
        *state.current_scene,
        state.gltf_animation_clip,
        state.gltf_animation_time);
}

void rebuild_scene_animation_state(AppState& state)
{
    rebuild_scene_animation_lists(state);
    state.gltf_animation_clip = 0;
    state.gltf_animation_time = 0.0f;
    state.render_model_animation_report_logged = false;
    rebuild_gltf_animation_instances(state);
    set_gltf_instance_animation_clip(state, state.gltf_animation_clip, state.gltf_animation_time);
}

nw::render::viewer::Lighting resolve_scene_lighting(const AppState& state, const nw::render::viewer::PreviewScene& scene)
{
    return nw::render::viewer::resolve_preview_scene_lighting(scene, state.area_day_night_elapsed);
}

nw::render::viewer::SceneFog resolve_scene_fog(const AppState& state, const nw::render::viewer::PreviewScene& scene)
{
    return nw::render::viewer::resolve_preview_scene_fog(
        scene, state.area_day_night_elapsed, state.show_authored_area_fog);
}

nw::render::viewer::LightingSpace resolve_scene_lighting_space(const nw::render::viewer::PreviewScene& scene)
{
    return nw::render::viewer::resolve_preview_scene_lighting_space(scene);
}

nw::render::viewer::SceneEnvironment resolve_scene_environment(
    const AppState& state, const nw::render::viewer::PreviewScene& scene)
{
    return nw::render::viewer::resolve_preview_scene_environment(scene, state.area_day_night_elapsed);
}

void update_area_day_night(AppState& state, float dt)
{
    if (!supports_area_day_night_controls_impl(state)) {
        state.area_day_night_elapsed = 0.0f;
        return;
    }

    if (!state.area_day_night_autoplay) {
        sync_area_day_night_state(state, false);
        return;
    }

    set_area_day_night_elapsed(state, state.area_day_night_elapsed + std::max(dt, 0.0f), true);
}

std::pair<float, float> camera_clip_planes(const nw::render::viewer::Camera& camera, const nw::render::viewer::Bounds& bounds)
{
    if (!camera.is_orthographic()) {
        const auto center = bounds.center();
        const float radius = std::max(bounds.radius(), 1.0f);
        const float distance = glm::length(camera.get_position() - center);
        const float near_plane = std::max(0.25f, std::min(distance * 0.25f, std::max(0.25f, distance - radius * 1.5f)));
        const float far_plane = std::max(near_plane + 50.0f, distance + radius * 2.5f);
        return {near_plane, far_plane};
    }

    const glm::mat4 view = camera.get_view_matrix();
    const std::array<glm::vec3, 8> corners{{
        {bounds.min.x, bounds.min.y, bounds.min.z},
        {bounds.min.x, bounds.min.y, bounds.max.z},
        {bounds.min.x, bounds.max.y, bounds.min.z},
        {bounds.min.x, bounds.max.y, bounds.max.z},
        {bounds.max.x, bounds.min.y, bounds.min.z},
        {bounds.max.x, bounds.min.y, bounds.max.z},
        {bounds.max.x, bounds.max.y, bounds.min.z},
        {bounds.max.x, bounds.max.y, bounds.max.z},
    }};

    float min_distance = std::numeric_limits<float>::max();
    float max_distance = 0.0f;
    for (const auto& corner : corners) {
        const glm::vec4 view_corner = view * glm::vec4(corner, 1.0f);
        const float distance = -view_corner.z;
        min_distance = std::min(min_distance, distance);
        max_distance = std::max(max_distance, distance);
    }

    const float padding = std::max(bounds.radius() * 0.1f, 25.0f);
    const float near_plane = std::max(0.1f, min_distance - padding);
    const float far_plane = std::max(near_plane + 100.0f, max_distance + padding);
    return {near_plane, far_plane};
}

float keyboard_speed_scale(const SDL_KeyboardEvent& key)
{
    return (key.mod & SDL_KMOD_SHIFT) ? 3.0f : 1.0f;
}

std::string current_capture_path(const AppState& state)
{
    if (state.turntable_capture) {
        auto dir = state.output_dir.empty() ? std::filesystem::path{"turntable"} : std::filesystem::path{state.output_dir};
        char file_name[32];
        std::snprintf(file_name, sizeof(file_name), "%04d.png", state.capture_index);
        return (dir / file_name).string();
    }
    return state.screenshot_path;
}

void clear_vfx_sequence(AppState& state)
{
    vfx_sequence_clear_state(state);
}

void reset_vfx_sequence(AppState& state)
{
    vfx_sequence_reset_runtime(state);
}

void update_vfx_sequence(AppState& state, int32_t dt_ms)
{
    vfx_sequence_update_runtime(state, dt_ms);
}

void advance_scene_playback(AppState& state, int32_t dt_ms, bool force_step)
{
    if (!state.current_scene) {
        return;
    }

    dt_ms = std::max(dt_ms, 0);
    const float dt = static_cast<float>(dt_ms) * 0.001f;
    state.scene_time_seconds += dt;
    const bool supports_vfx = supports_vfx_sequence_controls_impl(state);
    const bool tick_vfx = force_step || !supports_vfx || state.vfx_sequence_autoplay;
    const int32_t scene_dt_ms = force_step ? dt_ms : (tick_vfx ? dt_ms : 0);

    if (tick_vfx && supports_vfx) {
        update_vfx_sequence(state, dt_ms);
    }

    state.current_scene->update(scene_dt_ms);

    if (supports_gltf_animation_controls_impl(state) && (force_step || state.gltf_animation_autoplay)) {
        advance_gltf_instance_animation_times(state, dt);
    }

    if (force_step || state.area_day_night_autoplay) {
        update_area_day_night(state, dt);
    }
}

void fit_area_navigation_camera(nw::render::viewer::Camera& camera, const nw::render::viewer::Bounds& bounds)
{
    camera.fit_to_bounds(bounds);
    camera.set_free_view(camera.get_position(), camera.get_target(), nw::render::viewer::Camera::ProjectionMode::perspective);
}

void apply_static_pbr_environment_policy(AppState& state, const nw::render::viewer::PreviewScene& scene)
{
    if (!scene.static_models.empty()) {
        state.static_pbr_environment_policy = StaticPbrEnvironmentPolicy::reference_ibl;
        state.static_pbr_ibl_strength = kStaticPbrReferenceIblStrength;
        state.static_pbr_exposure = kStaticPbrReferenceExposure;
    } else {
        state.static_pbr_environment_policy = StaticPbrEnvironmentPolicy::scene_authored;
        state.static_pbr_ibl_strength = 1.0f;
        state.static_pbr_exposure = 1.0f;
    }
}

void clear_scene_render_scratch(AppState& state)
{
    state.forward_plus_frame.clear();
    state.prepared_model_draws.clear();
    state.prepared_model_surfaces.clear();
    state.nwn_prepared_draw_items.clear();
    state.prepared_draw_scratch.clear();
    state.render_model_animation_samples.clear();
}

void release_current_scene_and_cached_assets(AppState& state)
{
    if (state.gfx_context && state.current_scene) {
        nw::gfx::wait_idle(state.gfx_context);
    }

    state.current_scene.reset();
    clear_scene_render_scratch(state);

    if (auto* render_service = nw::kernel::services().get_mut<nw::render::RenderService>()) {
        render_service->clear_asset_cache();
    }
}

} // namespace

std::string_view static_pbr_environment_policy_name(StaticPbrEnvironmentPolicy policy) noexcept
{
    switch (policy) {
    case StaticPbrEnvironmentPolicy::scene_authored:
        return "scene_authored";
    case StaticPbrEnvironmentPolicy::reference_ibl:
        return "reference_ibl";
    }
    return "scene_authored";
}

bool scene_uses_shared_nwn_animation_source(const nw::render::viewer::PreviewScene& scene)
{
    const nw::model::Mdl* shared_source = nullptr;
    bool saw_animatable = false;
    for (const auto& model : scene.models) {
        if (!model || !model->scene_animation_enabled) {
            continue;
        }

        const auto* source = model->animation_source_ ? model->animation_source_ : model->mdl_;
        if (!source) {
            continue;
        }

        if (!shared_source) {
            shared_source = source;
        } else if (shared_source != source) {
            return false;
        }
        saw_animatable = true;
    }
    return saw_animatable;
}

bool run_screenshot_capture(AppState& state,
    const std::filesystem::path& screenshot_path,
    int warmup_frames,
    int max_frames)
{
    return run_screenshot_capture_impl(
        state,
        screenshot_path,
        warmup_frames,
        max_frames);
}

bool has_area_day_night_controls(const AppState& state)
{
    return supports_area_day_night_controls_impl(state);
}

bool supports_gltf_animation_controls(const AppState& state)
{
    return supports_gltf_animation_controls_impl(state);
}

bool supports_vfx_sequence_controls(const AppState& state)
{
    return supports_vfx_sequence_controls_impl(state);
}

void set_area_day_night_elapsed_seconds(AppState& state, float elapsed_seconds, bool log_transition)
{
    set_area_day_night_elapsed(state, elapsed_seconds, log_transition);
}

void reset_area_day_night_cycle(AppState& state)
{
    if (!state.current_scene || !supports_area_day_night_controls_impl(state)) {
        return;
    }
    set_area_day_night_elapsed(
        state, nw::render::viewer::initial_area_day_night_elapsed_seconds(*state.current_scene), false);
}

void replace_current_scene_after_gpu_idle(
    AppState& state, std::unique_ptr<nw::render::viewer::PreviewScene> scene)
{
    if (state.gfx_context && state.current_scene) {
        nw::gfx::wait_idle(state.gfx_context);
    }
    state.current_scene = std::move(scene);
    clear_scene_render_scratch(state);
}

void load_model(AppState& state, std::string_view resref)
{
    LOG_F(INFO, "Loading model: {}", resref);
    clear_vfx_sequence(state);
    state.loaded_scene_kind = LoadedSceneKind::model;
    state.loaded_scene_source = std::string{resref};
    state.loaded_vfx_sequence.reset();
    state.scene_time_seconds = 0.0f;
    state.render_model_animation_report_logged = false;

    release_current_scene_and_cached_assets(state);
    auto scene = nw::render::viewer::load_preview_scene(
        *state.preview_resources, resref, state.preview_scene_load_options);
    replace_current_scene_after_gpu_idle(state, std::move(scene));
    rebuild_scene_animation_state(state);

    if (state.current_scene) {
        apply_static_pbr_environment_policy(state, *state.current_scene);
        if (state.static_pbr_environment_policy == StaticPbrEnvironmentPolicy::reference_ibl) {
            ensure_static_pbr_ibl_textures(state);
        }

        LOG_F(INFO, "Model loaded: {} vertices, {} indices",
            state.current_scene->vertex_count,
            state.current_scene->index_count);
        const auto bounds = state.current_scene->current_bounds();
        LOG_F(INFO, "Model bounds: min=({}, {}, {}) max=({}, {}, {}) center=({}, {}, {}) radius={}",
            bounds.min.x, bounds.min.y, bounds.min.z,
            bounds.max.x, bounds.max.y, bounds.max.z,
            bounds.center().x, bounds.center().y, bounds.center().z,
            bounds.radius());

        bool has_idle = false;
        if (!state.current_scene->models.empty()) {
            auto try_scene_animation = [&](std::string_view animation) {
                if (!has_idle && !animation.empty()) {
                    has_idle = state.current_scene->load_animation(animation);
                }
            };
            try_scene_animation(state.animation_override);
            if (const auto* mdl = state.current_scene->models.front()->mdl_) {
                try_scene_animation(nw::render::viewer::preferred_model_animation_name(*mdl, nw::render::viewer::PreferredModelAnimationContext::hold));
            }
            try_scene_animation("default");
            try_scene_animation("on");
            try_scene_animation("cast01");
            try_scene_animation("impact");
            try_scene_animation("pause1");
            try_scene_animation("cpause1");
        }
        if (!has_idle && !state.current_scene->static_models.empty()) {
            has_idle = select_render_model_animation(state, state.animation_override);
            if (!has_idle) {
                has_idle = select_preferred_render_model_animation(state);
            }
        }

        if (has_idle) {
            state.current_scene->update(33);
        }
        if (!state.current_scene->particles.empty()) {
            const int32_t particle_prime_ms = nw::render::viewer::compute_particle_prime_ms(*state.current_scene, !state.animation_override.empty());
            state.current_scene->update(particle_prime_ms);
            size_t live_particles = 0;
            for (const auto& scene_particles : state.current_scene->particles) {
                live_particles += scene_particles.system.particles.core.position.size();
            }
            LOG_F(INFO, "Primed {} particle systems ({} live particles, {} ms)",
                state.current_scene->particles.size(), live_particles, particle_prime_ms);
        }

        if (state.camera) {
            state.camera->set_fov(60.0f);
            fit_model_preview_camera(*state.camera, *state.current_scene);
        }
    } else {
        LOG_F(ERROR, "Failed to load model: {}", resref);
    }

    state.area_navigation = false;
    state.area_day_night_autoplay = true;
    state.area_day_night_elapsed = 0.0f;
    state.gltf_animation_autoplay = true;
    state.vfx_sequence_autoplay = true;
    state.scene_playing = true;
}

void load_vfx_sequence(AppState& state, const VfxSequence& sequence)
{
    LOG_F(INFO, "Loading VFX sequence: {}", sequence.label);
    clear_vfx_sequence(state);
    state.loaded_scene_kind = LoadedSceneKind::vfx_sequence;
    state.loaded_scene_source.clear();
    state.loaded_vfx_sequence = sequence;
    state.scene_time_seconds = 0.0f;

    release_current_scene_and_cached_assets(state);

    std::vector<std::string> sources;
    const bool use_source_target_layout = sequence.source_target_layout;
    const bool use_spell_actors = sequence.use_spell_actors;
    sources.reserve(sequence.steps.size() + (use_spell_actors ? 2u : 0u));
    if (use_spell_actors) {
        sources.emplace_back(kVfxSequenceCasterModel);
        sources.emplace_back(kVfxSequenceTargetModel);
    }
    for (const auto& step : sequence.steps) {
        sources.push_back(step.source);
    }

    auto scene = nw::render::viewer::load_preview_scene(*state.preview_resources, sources);
    replace_current_scene_after_gpu_idle(state, std::move(scene));
    rebuild_scene_animation_state(state);
    if (!state.current_scene) {
        LOG_F(ERROR, "Failed to load VFX sequence: {}", sequence.label);
        return;
    }
    apply_static_pbr_environment_policy(state, *state.current_scene);

    const auto layout = vfx_sequence_prepare_scene(
        state, *state.current_scene, sequence, state.animation_override);

    bool has_idle = false;
    auto try_sequence_animation = [&](std::string_view animation) {
        if (animation.empty()) {
            return;
        }
        has_idle = state.current_scene->load_animation(animation) || has_idle;
    };
    try_sequence_animation(state.animation_override);
    if (!use_source_target_layout) {
        try_sequence_animation("default");
        try_sequence_animation("on");
        try_sequence_animation("cast01");
        try_sequence_animation("pause1");
    }

    if (state.camera) {
        if (use_source_target_layout) {
            const glm::vec3 camera_target_pos = vfx_sequence_resolve_target_point(
                layout.target, layout.target_root_pos, VfxTargetPointKind::center);
            const glm::vec3 center = 0.5f * (layout.source_pos + camera_target_pos) + glm::vec3{0.0f, 0.0f, 1.2f};
            state.camera->set_fov(45.0f);
            state.camera->set_orbit_view(
                center, layout.distance * 1.18f, -90.0f, 20.0f, nw::render::viewer::Camera::ProjectionMode::perspective);
        } else {
            state.camera->set_fov(60.0f);
            state.camera->fit_to_bounds(state.current_scene->current_bounds());
        }
    }

    reset_vfx_sequence(state);
    update_vfx_sequence(state, 0);
    LOG_F(INFO, "Loaded VFX sequence '{}' with {} steps over {} ms",
        sequence.label, state.vfx_sequence_steps.size(), state.vfx_sequence_loop_ms);
    state.area_navigation = false;
    state.area_day_night_autoplay = true;
    state.area_day_night_elapsed = 0.0f;
    state.gltf_animation_autoplay = true;
    state.vfx_sequence_autoplay = true;
    state.scene_playing = true;
}

void load_area(AppState& state, std::string_view resref)
{
    LOG_F(INFO, "Loading area: {}", resref);
    state.loaded_scene_kind = LoadedSceneKind::area;
    state.loaded_scene_source = std::string{resref};
    state.loaded_vfx_sequence.reset();
    state.scene_time_seconds = 0.0f;

    release_current_scene_and_cached_assets(state);
    auto scene = nw::render::viewer::load_area_scene(*state.preview_resources, resref, state.preview_scene_load_options);
    replace_current_scene_after_gpu_idle(state, std::move(scene));
    rebuild_scene_animation_state(state);
    if (state.current_scene) {
        apply_static_pbr_environment_policy(state, *state.current_scene);
        LOG_F(INFO, "Area loaded: {} vertices, {} indices",
            state.current_scene->vertex_count,
            state.current_scene->index_count);
        const auto area_bounds = state.current_scene->current_bounds();
        LOG_F(INFO, "Area bounds: min=({}, {}, {}) max=({}, {}, {})",
            area_bounds.min.x, area_bounds.min.y, area_bounds.min.z,
            area_bounds.max.x, area_bounds.max.y, area_bounds.max.z);

        if (state.camera) {
            state.camera->set_fov(45.0f);
            fit_area_navigation_camera(*state.camera, area_bounds);
        }
        state.area_navigation = true;
        state.area_day_night_autoplay = true;
        state.gltf_animation_autoplay = true;
        state.vfx_sequence_autoplay = true;
        state.scene_playing = true;
        set_area_day_night_elapsed(
            state, nw::render::viewer::initial_area_day_night_elapsed_seconds(*state.current_scene), false);
    } else {
        LOG_F(ERROR, "Failed to load area: {}", resref);
        state.area_navigation = false;
        state.area_day_night_autoplay = true;
        state.area_day_night_elapsed = 0.0f;
        state.gltf_animation_autoplay = true;
        state.vfx_sequence_autoplay = true;
        state.scene_playing = true;
    }
}

void reload_current_scene(AppState& state)
{
    switch (state.loaded_scene_kind) {
    case LoadedSceneKind::model:
        if (!state.loaded_scene_source.empty()) {
            load_model(state, state.loaded_scene_source);
        }
        break;
    case LoadedSceneKind::vfx_sequence:
        if (state.loaded_vfx_sequence) {
            load_vfx_sequence(state, *state.loaded_vfx_sequence);
        }
        break;
    case LoadedSceneKind::area:
        if (!state.loaded_scene_source.empty()) {
            load_area(state, state.loaded_scene_source);
        }
        break;
    case LoadedSceneKind::none:
        break;
    }
}

bool select_model_animation(AppState& state, size_t model_index, std::string_view animation_name)
{
    if (!state.current_scene || animation_name.empty() || model_index >= state.current_scene->models.size()) {
        return false;
    }

    auto& model = state.current_scene->models[model_index];
    if (!model || !model->scene_animation_enabled) {
        return false;
    }

    bool loaded = false;
    for (auto& candidate : state.current_scene->models) {
        if (!candidate || !candidate->scene_animation_enabled) {
            continue;
        }

        candidate->anim_cursor_ = 0;
        loaded = candidate->load_animation(animation_name) || loaded;
    }

    if (!loaded) {
        return false;
    }

    state.current_scene->update(0);
    if (!state.current_scene->particles.empty()) {
        state.current_scene->rebuild_particles();
        const int32_t particle_prime_ms = nw::render::viewer::compute_particle_prime_ms(*state.current_scene, true);
        state.current_scene->update(particle_prime_ms);

        size_t live_particles = 0;
        for (const auto& scene_particles : state.current_scene->particles) {
            live_particles += scene_particles.system.particles.core.position.size();
        }
        LOG_F(INFO, "Primed {} particle systems after switching to {} ({} live particles, {} ms)",
            state.current_scene->particles.size(), animation_name, live_particles, particle_prime_ms);
    }

    return true;
}

void set_gltf_animation_clip(AppState& state, uint32_t clip_index)
{
    if (!state.current_scene) {
        return;
    }

    set_gltf_instance_animation_clip(state, clip_index, 0.0f);

    for (const auto& model : state.current_scene->static_models) {
        if (!model || model->animations.empty()) {
            continue;
        }

        const auto& clip = model->animations[clip_index % model->animations.size()];
        LOG_F(INFO, "RenderModel animation clip {} on {}: {}",
            clip_index, model->name, clip.name.empty() ? "<unnamed>" : clip.name);
        break;
    }
}

void update_viewer(AppState& state, float dt)
{
    if (state.camera) {
        state.camera->update(dt);
    }

    if (state.current_scene && state.scene_playing) {
        advance_scene_playback(state, static_cast<int32_t>(dt * 1000.f), false);
    }

    if (state.turntable_mode && state.current_scene) {
        float angle = static_cast<float>(state.capture_index) / static_cast<float>(state.turntable_frames) * 360.0f;
        state.camera->set_orbit_angle(angle);
    }
}

void resume_scene_playback(AppState& state)
{
    state.scene_playing = true;
    if (supports_vfx_sequence_controls_impl(state) && !state.vfx_sequence_autoplay) {
        state.vfx_sequence_autoplay = true;
    }
    if (supports_gltf_animation_controls_impl(state) && !state.gltf_animation_autoplay) {
        state.gltf_animation_autoplay = true;
    }
    if (supports_area_day_night_controls_impl(state) && !state.area_day_night_autoplay) {
        state.area_day_night_autoplay = true;
    }
    LOG_F(INFO, "Scene playing");
}

void toggle_scene_playback(AppState& state)
{
    if (state.scene_playing) {
        state.scene_playing = false;
        LOG_F(INFO, "Scene paused");
    } else {
        resume_scene_playback(state);
    }
}

void stop_scene_playback(AppState& state, bool log_action)
{
    state.scene_playing = false;
    if (supports_vfx_sequence_controls_impl(state)) {
        state.vfx_sequence_autoplay = false;
    }
    if (supports_gltf_animation_controls_impl(state)) {
        state.gltf_animation_autoplay = false;
    }
    if (supports_area_day_night_controls_impl(state)) {
        state.area_day_night_autoplay = false;
    }
    if (log_action) {
        LOG_F(INFO, "Scene stopped");
    }
}

void reset_scene_playback(AppState& state)
{
    if (!state.current_scene) {
        return;
    }

    if (supports_vfx_sequence_controls_impl(state)) {
        reset_vfx_sequence(state);
        update_vfx_sequence(state, 0);
    }
    if (supports_gltf_animation_controls_impl(state)) {
        set_gltf_instance_animation_time(state, 0.0f);
    }
    if (supports_area_day_night_controls_impl(state)) {
        reset_area_day_night_cycle(state);
    }

    stop_scene_playback(state, false);
    LOG_F(INFO, "Scene reset");
}

void step_scene_playback(AppState& state, int32_t dt_ms)
{
    if (!state.current_scene) {
        return;
    }

    advance_scene_playback(state, dt_ms, true);
    stop_scene_playback(state, false);
    LOG_F(INFO, "Scene stepped by {} ms", std::max(dt_ms, 0));
}

void handle_key_down(AppState& state, const SDL_KeyboardEvent& key)
{
    if (key.key == SDLK_ESCAPE) {
        state.running = false;
        return;
    }

    if (!state.camera) {
        return;
    }

    float scale = keyboard_speed_scale(key);
    const float move_speed = 2.5f * scale;
    const float rotate_speed = 6.0f * scale;
    switch (key.key) {
    case SDLK_F5:
        request_renderer_reload(state);
        break;
    case SDLK_P:
        toggle_scene_playback(state);
        break;
    case SDLK_SPACE:
        if (supports_vfx_sequence_controls_impl(state)) {
            state.vfx_sequence_autoplay = !state.vfx_sequence_autoplay;
            if (state.vfx_sequence_autoplay) {
                state.scene_playing = true;
            }
            LOG_F(INFO, "VFX sequence autoplay {}", state.vfx_sequence_autoplay ? "enabled" : "disabled");
        } else if (supports_gltf_animation_controls_impl(state)) {
            state.gltf_animation_autoplay = !state.gltf_animation_autoplay;
            if (state.gltf_animation_autoplay) {
                state.scene_playing = true;
            }
            LOG_F(INFO, "RenderModel animation autoplay {}", state.gltf_animation_autoplay ? "enabled" : "disabled");
        } else if (supports_area_day_night_controls_impl(state)) {
            state.area_day_night_autoplay = !state.area_day_night_autoplay;
            if (state.area_day_night_autoplay) {
                state.scene_playing = true;
            }
            LOG_F(INFO, "Area day/night autoplay {}", state.area_day_night_autoplay ? "enabled" : "disabled");
        }
        break;
    case SDLK_PERIOD:
        if (state.current_scene) {
            step_scene_playback(state, kVfxSequenceStepMs);
        }
        break;
    case SDLK_SLASH:
        if (state.current_scene) {
            reset_scene_playback(state);
        }
        break;
    case SDLK_TAB:
        if (supports_gltf_animation_controls_impl(state)) {
            set_gltf_animation_clip(state, state.gltf_animation_clip + 1);
        }
        break;
    case SDLK_LEFTBRACKET:
        if (supports_gltf_animation_controls_impl(state)) {
            set_gltf_instance_animation_time(state, state.gltf_animation_time - 0.25f * scale);
            LOG_F(INFO, "RenderModel animation time {:.2f}", state.gltf_animation_time);
        }
        break;
    case SDLK_RIGHTBRACKET:
        if (supports_gltf_animation_controls_impl(state)) {
            set_gltf_instance_animation_time(state, state.gltf_animation_time + 0.25f * scale);
            LOG_F(INFO, "RenderModel animation time {:.2f}", state.gltf_animation_time);
        }
        break;
    case SDLK_G:
        state.show_authored_area_fog = !state.show_authored_area_fog;
        LOG_F(INFO, "Authored area fog {}", state.show_authored_area_fog ? "enabled" : "disabled");
        if (state.show_authored_area_fog && state.current_scene) {
            const auto fog = resolve_scene_fog(state, *state.current_scene);
            if (fog.enabled) {
                LOG_F(INFO,
                    "Resolved area fog: amount={:.3f} start={:.2f} end={:.2f} color=({:.3f}, {:.3f}, {:.3f}) authored_clip={:.2f} authored_sun={} authored_moon={}",
                    fog.amount,
                    fog.start_distance,
                    fog.end_distance,
                    fog.color.r,
                    fog.color.g,
                    fog.color.b,
                    state.current_scene->area_weather.fog_clip_distance,
                    state.current_scene->area_weather.fog_sun_amount,
                    state.current_scene->area_weather.fog_moon_amount);
            } else {
                LOG_F(INFO,
                    "Resolved area fog disabled: sun_color=0x{:06x} moon_color=0x{:06x} clip={:.2f} sun_amount={} moon_amount={}",
                    state.current_scene->area_weather.color_sun_fog & 0x00ffffffu,
                    state.current_scene->area_weather.color_moon_fog & 0x00ffffffu,
                    state.current_scene->area_weather.fog_clip_distance,
                    state.current_scene->area_weather.fog_sun_amount,
                    state.current_scene->area_weather.fog_moon_amount);
            }
        }
        break;
    case SDLK_H:
        state.shadow_debug_mode = (state.shadow_debug_mode + 1) % 2;
        LOG_F(INFO, "Shadow debug {}", state.shadow_debug_mode == 0 ? "off" : "cascades");
        break;
    case SDLK_J:
        state.static_pbr_ibl_strength = std::max(0.0f, state.static_pbr_ibl_strength - 0.1f * scale);
        LOG_F(INFO, "Static PBR IBL strength {:.2f}", state.static_pbr_ibl_strength);
        break;
    case SDLK_K:
        state.static_pbr_ibl_strength = std::min(4.0f, state.static_pbr_ibl_strength + 0.1f * scale);
        LOG_F(INFO, "Static PBR IBL strength {:.2f}", state.static_pbr_ibl_strength);
        break;
    case SDLK_N:
        state.static_pbr_exposure = std::max(0.1f, state.static_pbr_exposure - 0.1f * scale);
        LOG_F(INFO, "Static PBR exposure {:.2f}", state.static_pbr_exposure);
        break;
    case SDLK_M:
        state.static_pbr_exposure = std::min(10.0f, state.static_pbr_exposure + 0.1f * scale);
        LOG_F(INFO, "Static PBR exposure {:.2f}", state.static_pbr_exposure);
        break;
    case SDLK_F:
        if (state.current_scene) {
            if (state.area_navigation) {
                fit_area_navigation_camera(*state.camera, state.current_scene->current_bounds());
            } else {
                state.camera->fit_to_bounds(state.current_scene->current_bounds());
            }
        }
        break;
    case SDLK_A:
        if (state.area_navigation) {
            state.camera->move_right(-move_speed);
        } else {
            state.camera->orbit(-8.0f * scale, 0.0f);
        }
        break;
    case SDLK_D:
        if (state.area_navigation) {
            state.camera->move_right(move_speed);
        } else {
            state.camera->orbit(8.0f * scale, 0.0f);
        }
        break;
    case SDLK_W:
        if (state.area_navigation) {
            state.camera->move_forward(move_speed, true);
        } else {
            state.camera->orbit(0.0f, 6.0f * scale);
        }
        break;
    case SDLK_S:
        if (state.area_navigation) {
            state.camera->move_forward(-move_speed, true);
        } else {
            state.camera->orbit(0.0f, -6.0f * scale);
        }
        break;
    case SDLK_LEFT:
        if (state.area_navigation) {
            state.camera->yaw(-rotate_speed);
        } else {
            state.camera->orbit(-8.0f * scale, 0.0f);
        }
        break;
    case SDLK_RIGHT:
        if (state.area_navigation) {
            state.camera->yaw(rotate_speed);
        } else {
            state.camera->orbit(8.0f * scale, 0.0f);
        }
        break;
    case SDLK_UP:
        if (state.area_navigation) {
            if (key.mod & SDL_KMOD_CTRL) {
                state.camera->pitch(rotate_speed);
            } else {
                state.camera->move_up(move_speed);
            }
        } else {
            state.camera->orbit(0.0f, 6.0f * scale);
        }
        break;
    case SDLK_DOWN:
        if (state.area_navigation) {
            if (key.mod & SDL_KMOD_CTRL) {
                state.camera->pitch(-rotate_speed);
            } else {
                state.camera->move_up(-move_speed);
            }
        } else {
            state.camera->orbit(0.0f, -6.0f * scale);
        }
        break;
    case SDLK_Q:
        state.camera->zoom(1.0f + 0.12f * scale);
        break;
    case SDLK_E:
        state.camera->zoom(1.0f / (1.0f + 0.12f * scale));
        break;
    default:
        break;
    }
}

void render_frame(AppState& state)
{
    if (state.renderer_reload_requested) {
        state.renderer_reload_requested = false;
        reload_renderer_runtime(state);
    }

    auto* cmd = nw::gfx::begin_frame(state.gfx_context);
    if (!cmd) return;

    imgui_prepare_frame(state, cmd);

    const auto begin_window_render = [&]() {
        nw::gfx::cmd_begin_render(cmd, {});
        nw::gfx::cmd_set_viewport(cmd,
            0.0f, 0.0f,
            static_cast<float>(state.window_width),
            static_cast<float>(state.window_height),
            0.0f, 1.0f);
        nw::gfx::cmd_set_scissor(cmd,
            0, 0,
            static_cast<uint32_t>(state.window_width),
            static_cast<uint32_t>(state.window_height));
    };

    if (state.current_scene && state.preview_resources && state.camera) {
        const auto bounds = state.current_scene->current_bounds();
        const auto clip_planes = camera_clip_planes(*state.camera, bounds);
        state.camera->set_near_far(clip_planes.first, clip_planes.second);

        nw::render::viewer::RenderContext ctx{};
        ctx.view = state.camera->get_view_matrix();
        ctx.projection = state.camera->get_projection_matrix();
        ctx.camera_position = state.camera->get_position();
        ctx.camera_target = state.camera->get_target();
        ctx.camera_near_plane = state.camera->near_plane();
        ctx.camera_far_plane = state.camera->far_plane();
        ctx.orthographic_camera = state.camera->is_orthographic();
        ctx.time_seconds = state.scene_time_seconds;
        ctx.static_pbr_ibl_strength = state.static_pbr_ibl_strength;
        ctx.static_pbr_exposure = state.static_pbr_exposure;
        ctx.static_pbr_ibl_diffuse_texture_index = state.static_pbr_ibl_diffuse_texture_index;
        ctx.static_pbr_ibl_specular_texture_index = state.static_pbr_ibl_specular_texture_index;
        ctx.static_pbr_brdf_lut_texture_index = state.static_pbr_brdf_lut_texture_index;
        ctx.static_pbr_ibl_enabled = static_pbr_ibl_active(state);
        ctx.lighting = resolve_scene_lighting(state, *state.current_scene);
        ctx.lighting_space = resolve_scene_lighting_space(*state.current_scene);
        ctx.environment = resolve_scene_environment(state, *state.current_scene);
        ctx.local_lights = state.current_scene->render_local_lights;

        const auto fog = resolve_scene_fog(state, *state.current_scene);
        ctx.fog = fog;
        const uint32_t shadow_resolution = nw::render::viewer::viewer_shadow_map_resolution();
        const uint32_t shadow_cascade_count = nw::render::viewer::viewer_shadow_cascade_count();
        ctx.shadow = nw::render::viewer::resolve_scene_shadow(ctx, bounds, shadow_resolution, shadow_cascade_count);
        ctx.shadow.debug_mode = state.shadow_debug_mode;
        auto& render_service = nw::render::render_service();
        auto& forward_plus_renderer = render_service.forward_plus_renderer();
        auto render_model_ctx = render_service.model_render_context();
        forward_plus_renderer.prepare_render_context(
            state.forward_plus_frame,
            cmd,
            ctx,
            0,
            0,
            static_cast<uint32_t>(state.window_width),
            static_cast<uint32_t>(state.window_height),
            state.forward_plus_policy);

        const auto render_model_animation_sample_stats = nw::render::viewer::sample_render_model_animations(
            state.render_model_animation_samples,
            *state.current_scene);

        nw::render::viewer::collect_prepared_model_surface_draws(
            state.prepared_model_draws,
            state.prepared_model_surfaces,
            *state.current_scene);
        log_render_model_animation_runtime_state(state, render_model_animation_sample_stats);
        state.prepared_draw_scratch.begin_frame();

        const auto render_scene_pass = [&](const nw::render::viewer::RenderContext& pass_ctx,
                                           nw::render::viewer::RenderPassSelection pass) {
            const std::span<const nw::render::PreparedModelSurfaceDraw> surfaces{
                state.prepared_model_surfaces.draws.data(),
                state.prepared_model_surfaces.draws.size()};
            const auto* render_model_skin_table = &state.prepared_model_surfaces.render_model_skins;
            nw::render::viewer::render_prepared_nwn_legacy_surface_draws(
                render_service,
                cmd,
                state.prepared_model_draws,
                surfaces,
                pass_ctx,
                pass,
                state.prepared_draw_scratch,
                state.nwn_prepared_draw_items);
            nw::render::viewer::render_prepared_render_model_surface_draws(
                render_model_ctx,
                cmd,
                *state.current_scene,
                surfaces,
                pass_ctx,
                pass,
                render_model_skin_table);
        };

        if (ctx.shadow.enabled) {
            nw::render::viewer::render_scene_shadow_maps(
                render_service,
                render_model_ctx,
                cmd,
                *state.current_scene,
                ctx.shadow,
                shadow_resolution);
        }

        const bool scene_has_water = state.current_scene->contains_water();
        begin_window_render();
        render_scene_pass(ctx, nw::render::viewer::RenderPassSelection::opaque_cutout);
        if (scene_has_water) {
            render_scene_pass(ctx, nw::render::viewer::RenderPassSelection::water);
        }
        render_scene_pass(ctx, nw::render::viewer::RenderPassSelection::transparent);
        nw::render::viewer::render_scene_particles(
            render_service,
            cmd,
            *state.current_scene,
            ctx);
        if (state.show_debug_overlay && state.debug_renderer) {
            state.debug_renderer->render_debug_shapes(cmd, *state.current_scene, ctx);
            state.debug_renderer->render_debug_grid(cmd, *state.current_scene, ctx,
                state.debug_grid_spacing,
                state.debug_grid_major_interval,
                state.debug_grid_minor_width,
                state.debug_grid_major_width,
                state.debug_grid_opacity,
                state.debug_grid_z_offset);
        }
    } else {
        begin_window_render();
    }

    imgui_render(state, cmd);
    nw::gfx::cmd_end_render(cmd);

    if (state.screenshot_requested && state.screenshot_delay_frames > 0) {
        --state.screenshot_delay_frames;
    } else if (state.screenshot_requested || state.turntable_capture) {
        auto path = current_capture_path(state);
        bool captured = nw::gfx::capture_screenshot(state.gfx_context, cmd, path.c_str());
        if (captured) {
            LOG_F(INFO, "Saved screenshot: {}", path);
        } else {
            LOG_F(ERROR, "Failed to save screenshot: {}", path);
        }

        if (state.turntable_capture) {
            ++state.capture_index;
            if (state.capture_index >= state.turntable_frames) {
                state.running = false;
            }
        } else {
            state.screenshot_captured = captured;
            state.screenshot_requested = false;
            state.running = false;
        }
        return;
    }

    nw::gfx::end_frame(state.gfx_context);
}

int run_area_benchmark_command(AppState& state, std::string_view area_resref,
    std::string_view module_path, std::string_view user_path,
    int frames, int warmup_frames, bool lights_enabled, bool shadows_enabled,
    bool local_shadows_enabled, bool debug_enabled,
    const nw::render::viewer::ForwardPlusRenderPolicy& forward_plus_policy,
    const AreaBenchmarkCameraOptions& camera_options,
    std::optional<float> area_time_seconds,
    int visible_tile_radius, AreaBenchmarkVisibilityMode visibility_mode, float visibility_cone_half_angle_degrees,
    const std::filesystem::path& output_path,
    const std::filesystem::path& screenshot_path)
{
    if (!state.preview_resources || !state.gfx_context) {
        LOG_F(ERROR, "Area benchmark requires initialized preview render resources and graphics context");
        return 1;
    }

    frames = std::max(frames, 1);
    warmup_frames = std::max(warmup_frames, 0);
    const nw::render::viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = static_cast<uint32_t>(std::max(state.window_width, 1)),
        .height = static_cast<uint32_t>(std::max(state.window_height, 1)),
    };

    nw::render::viewer::ViewerSession session{*state.preview_resources, state.debug_renderer.get()};
    configure_preview_session(session, PreviewSessionSetup{
                                           .lights_enabled = lights_enabled,
                                           .shadows_enabled = shadows_enabled,
                                           .local_shadows_enabled = local_shadows_enabled,
                                           .debug_enabled = debug_enabled,
                                           .forward_plus_policy = &forward_plus_policy,
                                       });

    LOG_F(INFO, "Area benchmark loading '{}'", area_resref);
    if (!prepare_area_session_for_benchmark(
            session, area_resref, viewport,
            camera_options,
            visible_tile_radius, visibility_mode,
            visibility_cone_half_angle_degrees)) {
        return 1;
    }
    if (area_time_seconds) {
        session.set_area_day_night_elapsed_seconds(*area_time_seconds, false);
    }

    std::vector<nw::render::viewer::ViewerFrameStats> samples;
    std::string benchmark_failure;
    const bool ok = run_viewer_session_frames(
        state, session, viewport,
        warmup_frames, frames, &samples,
        &benchmark_failure);
    if (!ok) {
        LOG_F(ERROR, "Area benchmark failed during frame run: {}", benchmark_failure);
        return 1;
    }

    bool screenshot_captured = false;
    std::string screenshot_output;
    if (!screenshot_path.empty()) {
        screenshot_output = screenshot_path.string();
        std::string capture_failure;
        screenshot_captured = capture_viewer_session_frame(
            state, session, viewport,
            screenshot_output,
            &capture_failure);
        if (!screenshot_captured) {
            LOG_F(ERROR, "Area benchmark failed to capture screenshot: {}", capture_failure);
            return 1;
        }
        if (screenshot_captured) {
            LOG_F(INFO, "Saved area benchmark screenshot: {}", screenshot_output);
        } else {
            LOG_F(ERROR, "Failed to save area benchmark screenshot: {}", screenshot_output);
        }
    }

    const auto timing_summary = [&](auto getter) {
        double min_ms = std::numeric_limits<double>::max();
        double max_ms = 0.0;
        double sum_ms = 0.0;
        for (const auto& sample : samples) {
            const double value = seconds_to_ms(getter(sample));
            min_ms = std::min(min_ms, value);
            max_ms = std::max(max_ms, value);
            sum_ms += value;
        }
        return json{
            {"avg", samples.empty() ? 0.0 : sum_ms / static_cast<double>(samples.size())},
            {"min", samples.empty() ? 0.0 : min_ms},
            {"max", samples.empty() ? 0.0 : max_ms},
        };
    };

    const auto counter_summary = [&](auto getter) {
        uint64_t min_value = std::numeric_limits<uint64_t>::max();
        uint64_t max_value = 0;
        long double sum = 0.0;
        for (const auto& sample : samples) {
            const uint64_t value = getter(sample);
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
            sum += static_cast<long double>(value);
        }
        return json{
            {"avg", samples.empty() ? 0.0 : static_cast<double>(sum / static_cast<long double>(samples.size()))},
            {"min", samples.empty() ? 0u : min_value},
            {"max", samples.empty() ? 0u : max_value},
        };
    };

    json frame_samples = json::array();
    for (const auto& sample : samples) {
        frame_samples.push_back(frame_stats_json(sample));
    }

    const auto* scene = session.scene();
    json area_render_cache = json::object();
    if (scene && scene->area_render_scene) {
        area_render_cache = area_render_cache_stats_json(scene->area_render_scene->stats());
    }
    const json render_service_stats = current_render_service_stats_json();
    json asset_cache = render_asset_cache_stats_json(render_service_stats);
    json model_loader_resource_cache = model_loader_resource_cache_stats_json(render_service_stats);
    const size_t visibility_mask_visible_chunks = visible_tile_radius >= 0
        ? session.area_visibility_mask_visible_chunk_count()
        : 0u;
    const SceneLocalLightSourceCounts scene_light_counts = scene
        ? count_scene_local_light_sources(*scene)
        : SceneLocalLightSourceCounts{};
    // clang-format off
    json report{
        {"area", std::string(area_resref)},
        {"module", benchmark_path_metadata_json(module_path)},
        {"user_path", benchmark_path_metadata_json(user_path)},
        {"frames", frames},
        {"warmup_frames", warmup_frames},
        {"viewport", {
            {"width", viewport.width},
            {"height", viewport.height},
        }},
        {"camera", {
            {"mode", area_benchmark_camera_mode_label(camera_options.mode)},
            {"position", vec3_json(session.camera().get_position())},
            {"target", vec3_json(session.camera().get_target())},
            {"fov_degrees", session.camera().fov_degrees()},
            {"near_plane", session.camera().near_plane()},
            {"far_plane", session.camera().far_plane()},
        }},
        {"options", {
            {"lights", lights_enabled},
            {"shadows", shadows_enabled},
            {"local_shadows", local_shadows_enabled},
            {"debug", debug_enabled},
            {"forward_plus", forward_plus_policy_json(forward_plus_policy)},
            {"area_time_seconds", area_time_seconds ? json(*area_time_seconds) : json(nullptr)},
            {"visible_tile_radius", visible_tile_radius},
            {"visible_tile_mode", area_benchmark_visibility_mode_label(visibility_mode)},
            {"visible_tile_cone_half_angle_degrees", visibility_cone_half_angle_degrees},
            {"visibility_mask_visible_chunks", visibility_mask_visible_chunks},
            {"screenshot", screenshot_path.empty() ? json(nullptr) : json{
                {"path", screenshot_output},
                {"captured", screenshot_captured},
            }},
        }},
        {"scene", scene ? json{
            {"models", scene->models.size()},
            {"static_models", scene->static_models.size()},
            {"particle_systems", scene->particles.size()},
            {"local_lights", scene->local_lights.size()},
            {"local_lights_authored_model", scene_light_counts.authored_model},
            {"local_lights_tile_model", scene_light_counts.tile_model},
            {"local_lights_placeable_table", scene_light_counts.placeable_table},
            {"area_width", scene->area_width},
            {"area_height", scene->area_height},
            {"has_water", scene->contains_water()},
            {"area_render_cache", std::move(area_render_cache)},
        } : json::object()},
        {"asset_cache", std::move(asset_cache)},
        {"nwn_model_loader_resource_cache", std::move(model_loader_resource_cache)},
        {"summary", {
            {"timings_ms", {
                {"tick", timing_summary([](const auto& stats) { return stats.tick_seconds; })},
                {"setup", timing_summary([](const auto& stats) { return stats.setup_seconds; })},
                {"shadow", timing_summary([](const auto& stats) { return stats.shadow_seconds; })},
                {"opaque", timing_summary([](const auto& stats) { return stats.opaque_seconds; })},
                {"water", timing_summary([](const auto& stats) { return stats.water_seconds; })},
                {"transparent", timing_summary([](const auto& stats) { return stats.transparent_seconds; })},
                {"particles", timing_summary([](const auto& stats) { return stats.particles_seconds; })},
                {"debug", timing_summary([](const auto& stats) { return stats.debug_seconds; })},
                {"area_prepare", timing_summary([](const auto& stats) { return stats.area_prepare_seconds; })},
                {"local_shadow", timing_summary([](const auto& stats) { return stats.local_shadow_seconds; })},
                {"forward_plus_prepare",
                    timing_summary([](const auto& stats) { return stats.forward_plus_prepare_seconds; })},
                {"forward_plus_light_cull",
                    timing_summary([](const auto& stats) { return stats.forward_plus_light_cull_seconds; })},
                {"forward_plus_cluster_header",
                    timing_summary([](const auto& stats) { return stats.forward_plus_cluster_header_seconds; })},
                {"forward_plus_cluster_index",
                    timing_summary([](const auto& stats) { return stats.forward_plus_cluster_index_seconds; })},
                {"forward_plus_upload",
                    timing_summary([](const auto& stats) { return stats.forward_plus_upload_seconds; })},
                {"total_render", timing_summary([](const auto& stats) { return stats.total_render_seconds; })},
            }},
            {"gpu_timings_ms", {
                {"shadow", timing_summary([](const auto& stats) { return stats.gpu_shadow_seconds; })},
                {"opaque", timing_summary([](const auto& stats) { return stats.gpu_opaque_seconds; })},
                {"water", timing_summary([](const auto& stats) { return stats.gpu_water_seconds; })},
                {"transparent", timing_summary([](const auto& stats) { return stats.gpu_transparent_seconds; })},
                {"particles", timing_summary([](const auto& stats) { return stats.gpu_particles_seconds; })},
                {"debug", timing_summary([](const auto& stats) { return stats.gpu_debug_seconds; })},
                {"forward_plus_cull",
                    timing_summary([](const auto& stats) { return stats.gpu_forward_plus_cull_seconds; })},
            }},
            {"commands", {
                {"gpu_timers", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.gpu_timer_count); })},
                {"draws", counter_summary([](const auto& stats) { return stats.total_command_stats.draw_count; })},
                {"shadow_draws", counter_summary([](const auto& stats) { return stats.shadow_command_stats.draw_count; })},
                {"local_shadow_draws", counter_summary([](const auto& stats) { return stats.local_shadow_command_stats.draw_count; })},
                {"opaque_draws", counter_summary([](const auto& stats) { return stats.opaque_command_stats.draw_count; })},
                {"water_draws", counter_summary([](const auto& stats) { return stats.water_command_stats.draw_count; })},
                {"transparent_draws", counter_summary([](const auto& stats) { return stats.transparent_command_stats.draw_count; })},
                {"particle_draws", counter_summary([](const auto& stats) { return stats.particle_command_stats.draw_count; })},
                {"debug_draws", counter_summary([](const auto& stats) { return stats.debug_command_stats.draw_count; })},
                {"indirect_draw_calls", counter_summary([](const auto& stats) { return stats.total_command_stats.indirect_draw_call_count; })},
                {"pipeline_binds", counter_summary([](const auto& stats) { return stats.total_command_stats.pipeline_bind_count; })},
                {"pipeline_binds_skipped", counter_summary([](const auto& stats) { return stats.total_command_stats.pipeline_bind_skipped_count; })},
                {"resource_binds", counter_summary([](const auto& stats) { return stats.total_command_stats.resource_bind_count; })},
                {"resource_binds_skipped", counter_summary([](const auto& stats) { return stats.total_command_stats.resource_bind_skipped_count; })},
                {"uniform_allocations", counter_summary([](const auto& stats) { return stats.total_command_stats.uniform_allocation_count; })},
                {"uniform_allocation_bytes", counter_summary([](const auto& stats) { return stats.total_command_stats.uniform_allocation_bytes; })},
            }},
            {"particles", {
                {"systems", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_system_count); })},
                {"submitted_systems", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_submitted_system_count); })},
                {"culled_systems", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_culled_system_count); })},
                {"invalid_material_packets", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_invalid_material_packet_count); })},
                {"mesh_packets", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_mesh_packet_count); })},
                {"mesh_particles", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_mesh_particle_count); })},
                {"mesh_submitted_particles", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_mesh_submitted_particle_count); })},
                {"mesh_dropped_particles", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_mesh_dropped_particle_count); })},
                {"mesh_missing_resref_packets", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_mesh_missing_resref_packet_count); })},
                {"mesh_missing_model_packets", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_mesh_missing_model_packet_count); })},
                {"mesh_invalid_particle_indices", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.particle_mesh_invalid_particle_index_count); })},
            }},
            {"lights", {
                {"local_lights", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.local_light_count); })},
                {"authored_model", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.local_light_authored_model_count); })},
                {"tile_model", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.local_light_tile_model_count); })},
                {"placeable_table", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.local_light_placeable_table_count); })},
            }},
            {"forward_plus", {
                {"prepared_frames", counter_summary([](const auto& stats) { return stats.forward_plus_prepared ? 1u : 0u; })},
                {"active_frames", counter_summary([](const auto& stats) { return stats.forward_plus_active ? 1u : 0u; })},
                {"empty_frames", counter_summary([](const auto& stats) { return stats.forward_plus_empty ? 1u : 0u; })},
                {"gpu_culling_frames", counter_summary([](const auto& stats) { return stats.forward_plus_gpu_culling ? 1u : 0u; })},
                {"lights", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_light_count); })},
                {"clusters", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_cluster_count); })},
                {"active_clusters", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_active_cluster_count); })},
                {"cluster_light_indices", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_cluster_light_index_count); })},
                {"cluster_light_index_capacity", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_cluster_light_index_capacity); })},
                {"max_lights_per_cluster", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_max_lights_per_cluster); })},
                {"overflow_clusters", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_overflow_cluster_count); })},
                {"overflow_lights", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_overflow_light_count); })},
                {"upload_bytes", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_upload_bytes); })},
                {"tile_size", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_tile_size); })},
                {"depth_slices", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.forward_plus_depth_slices); })},
            }},
            {"shadows", {
                {"cascades", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.shadow_cascade_count); })},
                {"caster_models", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.shadow_caster_model_count); })},
                {"submitted_models", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.shadow_submitted_model_count); })},
                {"culled_models", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.shadow_culled_model_count); })},
                {"local_rendered_frames", counter_summary([](const auto& stats) { return stats.local_shadows_rendered ? 1u : 0u; })},
                {"local_caster_lights", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.local_shadow_caster_light_count); })},
                {"local_submitted_models", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.local_shadow_submitted_model_count); })},
                {"local_culled_models", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.local_shadow_culled_model_count); })},
                {"shadow_indirect_sidecar_input_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.shadow_area_indirect_sidecar_bridge.input_surface_count); })},
                {"shadow_indirect_sidecar_selected_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.shadow_area_indirect_sidecar_bridge.selected_draw_count); })},
                {"shadow_indirect_sidecar_dropped_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.shadow_area_indirect_sidecar_bridge.dropped_surface_count()); })},
                {"shadow_sidecar_input_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.shadow_area_sidecar_bridge.input_surface_count); })},
                {"shadow_sidecar_selected_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.shadow_area_sidecar_bridge.selected_draw_count); })},
                {"shadow_sidecar_dropped_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.shadow_area_sidecar_bridge.dropped_surface_count()); })},
                {"local_shadow_sidecar_input_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.local_shadow_area_sidecar_bridge.input_surface_count); })},
                {"local_shadow_sidecar_selected_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.local_shadow_area_sidecar_bridge.selected_draw_count); })},
                {"local_shadow_sidecar_dropped_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.local_shadow_area_sidecar_bridge.dropped_surface_count()); })},
            }},
            {"static_batches", {
                {"material_batches", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_material_batch_count); })},
                {"material_input_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_material_input_draw_count); })},
                {"material_instances", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_material_instance_count); })},
                {"material_draw_calls", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_material_draw_call_count); })},
                {"material_fallback_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_material_fallback_draw_count); })},
                {"material_failed_batch_attempt_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_material_failed_batch_attempt_draw_count); })},
                {"material_indirect_calls", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_material_indirect_call_count); })},
                {"material_indirect_commands", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_material_indirect_command_count); })},
                {"material_max_instances_per_draw", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_material_max_instances_per_draw); })},
                {"material_indirect_command_upload_bytes", counter_summary([](const auto& stats) { return stats.static_batch_material_indirect_command_upload_bytes; })},
                {"material_cached_indirect_command_bytes", counter_summary([](const auto& stats) { return stats.static_batch_material_cached_indirect_command_bytes; })},
                {"material_draw_data_bytes", counter_summary([](const auto& stats) { return stats.static_batch_material_draw_data_bytes; })},
                {"material_draw_data_cache_hits", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_material_draw_data_cache_hit_count); })},
                {"material_cached_draw_data_bytes", counter_summary([](const auto& stats) { return stats.static_batch_material_cached_draw_data_bytes; })},
                {"shadow_batches", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_shadow_batch_count); })},
                {"shadow_input_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_shadow_input_draw_count); })},
                {"shadow_instances", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_shadow_instance_count); })},
                {"shadow_draw_calls", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_shadow_draw_call_count); })},
                {"shadow_failed_batch_attempt_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_shadow_failed_batch_attempt_draw_count); })},
                {"shadow_indirect_calls", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_shadow_indirect_call_count); })},
                {"shadow_indirect_commands", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_shadow_indirect_command_count); })},
                {"shadow_max_instances_per_draw", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_shadow_max_instances_per_draw); })},
                {"shadow_indirect_command_upload_bytes", counter_summary([](const auto& stats) { return stats.static_batch_shadow_indirect_command_upload_bytes; })},
                {"shadow_cached_indirect_command_bytes", counter_summary([](const auto& stats) { return stats.static_batch_shadow_cached_indirect_command_bytes; })},
                {"shadow_draw_data_bytes", counter_summary([](const auto& stats) { return stats.static_batch_shadow_draw_data_bytes; })},
                {"shadow_draw_data_cache_hits", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.static_batch_shadow_draw_data_cache_hit_count); })},
                {"shadow_cached_draw_data_bytes", counter_summary([](const auto& stats) { return stats.static_batch_shadow_cached_draw_data_bytes; })},
            }},
            {"area_frame", {
                {"visible_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_visible_record_count); })},
                {"visible_chunks", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_visible_chunk_count); })},
                {"culled_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_culled_record_count); })},
                {"culled_chunks", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_culled_chunk_count); })},
                {"visibility_culled_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_visibility_culled_record_count); })},
                {"visibility_culled_chunks", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_visibility_culled_chunk_count); })},
                {"opaque_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_opaque_record_count); })},
                {"water_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_water_record_count); })},
                {"transparent_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_transparent_record_count); })},
                {"shadow_caster_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_shadow_caster_record_count); })},
                {"visible_prepared_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_visible_prepared_surface_count); })},
                {"visible_lights", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_visible_light_count); })},
                {"uses_cached_draw_lists", counter_summary([](const auto& stats) { return stats.area_frame_uses_cached_draw_lists ? 1ull : 0ull; })},
                {"uses_sorted_static_draw_lists", counter_summary([](const auto& stats) { return stats.area_frame_uses_sorted_static_draw_lists ? 1ull : 0ull; })},
                {"material_indirect_sidecar_input_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_material_indirect_sidecar_bridge.input_surface_count); })},
                {"material_indirect_sidecar_selected_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_material_indirect_sidecar_bridge.selected_draw_count); })},
                {"material_indirect_sidecar_dropped_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_material_indirect_sidecar_bridge.dropped_surface_count()); })},
                {"static_material_indirect_sidecar_input_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_static_material_indirect_sidecar_bridge.input_surface_count); })},
                {"static_material_indirect_sidecar_selected_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_static_material_indirect_sidecar_bridge.selected_draw_count); })},
                {"static_material_indirect_sidecar_dropped_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_frame_static_material_indirect_sidecar_bridge.dropped_surface_count()); })},
                {"static_material_draw_data_sidecar_input_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_static_material_draw_data_sidecar_bridge.input_surface_count); })},
                {"static_material_draw_data_sidecar_selected_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_static_material_draw_data_sidecar_bridge.selected_draw_count); })},
                {"static_material_draw_data_sidecar_dropped_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_static_material_draw_data_sidecar_bridge.dropped_surface_count()); })},
                {"static_material_sidecar_input_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_static_material_sidecar_submission.input_surface_count); })},
                {"static_material_sidecar_selected_draws", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_static_material_sidecar_submission.selected_draw_count); })},
                {"static_material_sidecar_dropped_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_static_material_sidecar_submission.dropped_surface_count()); })},
                {"static_material_sidecar_dropped_non_nwn_surfaces", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_static_material_sidecar_submission.dropped_non_nwn_surface_count); })},
                {"static_material_sidecar_dropped_invalid_surface_indices", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_static_material_sidecar_submission.dropped_invalid_surface_index_count); })},
                {"static_material_sidecar_dropped_missing_sidecars", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_static_material_sidecar_submission.dropped_missing_sidecar_draw_count); })},
                {"static_material_sidecar_dropped_invalid_sidecars", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_static_material_sidecar_submission.dropped_invalid_sidecar_draw_count); })},
                {"direct_model_input_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_direct_model_submission.input_record_count); })},
                {"direct_model_selected_legacy_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_direct_model_submission.selected_legacy_record_count); })},
                {"direct_model_selected_render_model_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_direct_model_submission.selected_render_model_record_count); })},
                {"direct_model_skipped_render_model_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_direct_model_submission.skipped_render_model_record_count); })},
                {"direct_model_dropped_invalid_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_direct_model_submission.dropped_invalid_record_count); })},
                {"direct_model_dropped_invalid_sources", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_direct_model_submission.dropped_invalid_source_count); })},
                {"direct_model_dropped_unsupported_kinds", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_direct_model_submission.dropped_unsupported_kind_count); })},
                {"direct_model_dropped_records", counter_summary([](const auto& stats) { return static_cast<uint64_t>(stats.area_direct_model_submission.dropped_record_count()); })},
            }},
        }},
        {"samples", std::move(frame_samples)},
    };
    // clang-format on

    if (!output_path.empty()) {
        std::ofstream out{output_path};
        if (!out) {
            LOG_F(ERROR, "Failed to open benchmark output path '{}'", output_path.string());
            return 1;
        }
        out << report.dump(2) << '\n';
    }

    std::cout << report.dump(2) << '\n'
              << std::flush;
    return !screenshot_path.empty() && !screenshot_captured ? 1 : 0;
}

int run_area_sweep_command(AppState& state, std::string_view source,
    std::string_view module_path, std::string_view user_path,
    int frames, int warmup_frames, size_t limit,
    std::string_view variant_set, bool local_shadows_enabled, bool debug_enabled,
    const nw::render::viewer::ForwardPlusRenderPolicy& forward_plus_policy,
    const std::filesystem::path& output_path)
{
    if (!state.preview_resources || !state.gfx_context || !state.gfx_core) {
        LOG_F(ERROR, "Area sweep requires initialized preview render resources and graphics context");
        return 1;
    }

    frames = std::max(frames, 1);
    warmup_frames = std::max(warmup_frames, 0);

    const auto areas = collect_area_sweep_areas(source, limit);
    if (areas.empty()) {
        LOG_F(ERROR, "Area sweep needs at least one area resref");
        return 1;
    }

    const auto variants = make_area_sweep_variants(variant_set);
    if (variants.empty()) {
        LOG_F(ERROR, "Invalid area sweep variants '{}'. Use minimal, default, or all.", std::string{variant_set});
        return 1;
    }

    const nw::render::viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = static_cast<uint32_t>(std::max(state.window_width, 1)),
        .height = static_cast<uint32_t>(std::max(state.window_height, 1)),
    };

    json area_names = json::array();
    for (const auto& area : areas) {
        area_names.push_back(area);
    }

    json variant_names = json::array();
    for (const auto& variant : variants) {
        variant_names.push_back(variant.name);
    }

    json cases = json::array();
    uint64_t validation_warnings = 0;
    uint64_t validation_errors = 0;
    int passed_cases = 0;
    int failed_cases = 0;
    const size_t total_cases = areas.size() * variants.size();
    size_t case_index = 0;

    for (const auto& area : areas) {
        for (const auto& variant : variants) {
            ++case_index;
            LOG_F(INFO, "Area sweep case {}/{}: {} [{}]", case_index, total_cases, area, variant.name);

            nw::gfx::wait_idle(state.gfx_context);
            nw::gfx::reset_validation_report(state.gfx_core);

            std::vector<nw::render::viewer::ViewerFrameStats> samples;
            samples.reserve(static_cast<size_t>(frames));
            bool loaded = false;
            bool rendered = false;
            std::string failure;
            json scene_json = json::object();
            json asset_cache_json = json::object();
            json model_loader_resource_cache_json = json::object();
            size_t visibility_mask_visible_chunks = 0;
            auto case_forward_plus_policy = forward_plus_policy;
            case_forward_plus_policy.debug_mode = variant.forward_plus_debug_mode;
            const bool case_local_shadows_enabled = local_shadows_enabled && variant.local_shadows_enabled;

            {
                nw::render::viewer::ViewerSession session{*state.preview_resources, state.debug_renderer.get()};
                configure_preview_session(session, PreviewSessionSetup{
                                                       .lights_enabled = variant.lights_enabled,
                                                       .shadows_enabled = variant.shadows_enabled,
                                                       .local_shadows_enabled = case_local_shadows_enabled,
                                                       .debug_enabled = debug_enabled,
                                                       .forward_plus_policy = &case_forward_plus_policy,
                                                   });

                const AreaBenchmarkCameraOptions benchmark_camera_options{};
                loaded = prepare_area_session_for_benchmark(
                    session, area, viewport,
                    benchmark_camera_options,
                    variant.visible_tile_radius,
                    variant.visibility_mode,
                    variant.visibility_cone_half_angle_degrees);
                if (!loaded) {
                    failure = "load_area failed";
                } else {
                    rendered = run_viewer_session_frames(
                        state, session, viewport, warmup_frames, frames, &samples, &failure, false);
                    if (!rendered) {
                        failure = "begin_frame failed";
                    }

                    nw::gfx::wait_idle(state.gfx_context);

                    const auto* scene = session.scene();
                    json area_render_cache = json::object();
                    if (scene && scene->area_render_scene) {
                        area_render_cache = area_render_cache_stats_json(scene->area_render_scene->stats());
                    }
                    visibility_mask_visible_chunks = variant.visible_tile_radius >= 0
                        ? session.area_visibility_mask_visible_chunk_count()
                        : 0u;
                    const json render_service_stats = current_render_service_stats_json();
                    asset_cache_json = render_asset_cache_stats_json(render_service_stats);
                    model_loader_resource_cache_json = model_loader_resource_cache_stats_json(render_service_stats);
                    const SceneLocalLightSourceCounts scene_light_counts = scene
                        ? count_scene_local_light_sources(*scene)
                        : SceneLocalLightSourceCounts{};
                    scene_json = scene ? json{
                                             {"models", scene->models.size()},
                                             {"static_models", scene->static_models.size()},
                                             {"particle_systems", scene->particles.size()},
                                             {"local_lights", scene->local_lights.size()},
                                             {"local_lights_authored_model", scene_light_counts.authored_model},
                                             {"local_lights_tile_model", scene_light_counts.tile_model},
                                             {"local_lights_placeable_table", scene_light_counts.placeable_table},
                                             {"area_width", scene->area_width},
                                             {"area_height", scene->area_height},
                                             {"has_water", scene->contains_water()},
                                             {"area_render_cache", std::move(area_render_cache)},
                                         }
                                       : json::object();
                }
            }

            nw::gfx::wait_idle(state.gfx_context);
            const auto validation = nw::gfx::validation_report(state.gfx_core);
            validation_warnings += validation.warning_count;
            validation_errors += validation.error_count;

            const bool ok = loaded && rendered && static_cast<int>(samples.size()) == frames
                && validation.error_count == 0;
            if (ok) {
                ++passed_cases;
            } else {
                ++failed_cases;
                if (failure.empty() && validation.error_count > 0) {
                    failure = "validation errors";
                } else if (failure.empty()) {
                    failure = "render incomplete";
                }
            }

            cases.push_back({
                {"area", area},
                {"variant", variant.name},
                {"ok", ok},
                {"failure", failure.empty() ? json(nullptr) : json(failure)},
                {"frames_rendered", samples.size()},
                {"options", {
                                {"lights", variant.lights_enabled},
                                {"shadows", variant.shadows_enabled},
                                {"local_shadows", case_local_shadows_enabled},
                                {"debug", debug_enabled},
                                {"forward_plus", forward_plus_policy_json(case_forward_plus_policy)},
                                {"visible_tile_radius", variant.visible_tile_radius},
                                {"visible_tile_mode", area_benchmark_visibility_mode_label(variant.visibility_mode)},
                                {"visible_tile_cone_half_angle_degrees", variant.visibility_cone_half_angle_degrees},
                                {"visibility_mask_visible_chunks", visibility_mask_visible_chunks},
                            }},
                {"validation", {
                                   {"warnings", validation.warning_count},
                                   {"errors", validation.error_count},
                                   {"first_warning", optional_string_json(validation.first_warning)},
                                   {"first_error", optional_string_json(validation.first_error)},
                               }},
                {"scene", std::move(scene_json)},
                {"asset_cache", std::move(asset_cache_json)},
                {"nwn_model_loader_resource_cache", std::move(model_loader_resource_cache_json)},
                {"last_frame", samples.empty() ? json(nullptr) : area_sweep_frame_stats_json(samples.back())},
            });
        }
    }

    const json render_service_stats = current_render_service_stats_json();
    json report{
        {"command", "area-sweep"},
        {"source", std::string{source}},
        {"module", benchmark_path_metadata_json(module_path)},
        {"user_path", benchmark_path_metadata_json(user_path)},
        {"frames", frames},
        {"warmup_frames", warmup_frames},
        {"viewport", {
                         {"width", viewport.width},
                         {"height", viewport.height},
                     }},
        {"validation_enabled", state.enable_validation},
        {"variant_set", std::string{variant_set}},
        {"areas", std::move(area_names)},
        {"variants", std::move(variant_names)},
        {"summary", {
                        {"case_count", total_cases},
                        {"passed", passed_cases},
                        {"failed", failed_cases},
                        {"validation_warnings", validation_warnings},
                        {"validation_errors", validation_errors},
                    }},
        {"asset_cache", render_asset_cache_stats_json(render_service_stats)},
        {"nwn_model_loader_resource_cache", model_loader_resource_cache_stats_json(render_service_stats)},
        {"cases", std::move(cases)},
    };

    if (!output_path.empty()) {
        std::ofstream out{output_path};
        if (!out) {
            LOG_F(ERROR, "Failed to open area sweep output path '{}'", output_path.string());
            return 1;
        }
        out << report.dump(2) << '\n';
    }

    std::cout << report.dump(2) << '\n'
              << std::flush;
    return failed_cases == 0 ? 0 : 1;
}

int run_viewer_screenshot_command(AppState& state, std::string_view source,
    const std::filesystem::path& screenshot_path, bool area_scene)
{
    if (!state.preview_resources || !state.gfx_context) {
        LOG_F(ERROR, "Viewer screenshot requires initialized preview render resources and graphics context");
        return 1;
    }
    if (screenshot_path.empty()) {
        LOG_F(ERROR, "Viewer screenshot requires an output path");
        return 1;
    }

    const nw::render::viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = static_cast<uint32_t>(std::max(state.window_width, 1)),
        .height = static_cast<uint32_t>(std::max(state.window_height, 1)),
    };

    nw::render::viewer::ViewerSession session{*state.preview_resources, state.debug_renderer.get()};
    configure_preview_session(session, PreviewSessionSetup{
                                           .debug_enabled = state.show_debug_overlay,
                                       });
    session.set_preview_scene_load_options(state.preview_scene_load_options);

    const bool loaded = area_scene ? session.load_area(source) : session.load_model(source);
    if (!loaded) {
        return 1;
    }

    auto* scene = session.scene();
    if (!scene) {
        LOG_F(ERROR, "Viewer screenshot failed to load scene '{}'", source);
        return 1;
    }
    if (!area_scene && !state.animation_override.empty()) {
        const bool animation_loaded = session.select_animation(state.animation_override);
        if (!animation_loaded) {
            LOG_F(WARNING, "Animation '{}' was not found for '{}'", state.animation_override, source);
        }
        if (animation_loaded && !scene->particles.empty()) {
            scene->update(0);
            const int32_t particle_prime_ms = nw::render::viewer::compute_particle_prime_ms(*scene, true);
            scene->update(particle_prime_ms);
        }
    }
    apply_static_pbr_environment_policy(state, *scene);
    if (state.static_pbr_environment_policy == StaticPbrEnvironmentPolicy::reference_ibl) {
        ensure_static_pbr_ibl_textures(state);
        session.set_static_pbr_environment(
            state.static_pbr_ibl_strength,
            state.static_pbr_exposure,
            state.static_pbr_ibl_diffuse_texture_index,
            state.static_pbr_ibl_specular_texture_index,
            state.static_pbr_brdf_lut_texture_index,
            static_pbr_ibl_active(state));
    }

    session.fit_to_scene(viewport);
    if (area_scene) {
        fit_area_navigation_camera(session.camera(), scene->current_bounds());
    } else {
        fit_model_preview_camera(session.camera(), *scene);
    }

    {
        std::string screenshot_failure;
        const bool ok = run_viewer_session_frames(
            state, session, viewport, 2, 0, nullptr, &screenshot_failure, false);
        if (!ok) {
            LOG_F(ERROR, "Viewer screenshot failed during warmup frame run: {}", screenshot_failure);
            return 1;
        }
    }

    const std::string screenshot_output = screenshot_path.string();
    std::string screenshot_failure;
    const bool captured = capture_viewer_session_frame(
        state, session, viewport,
        screenshot_output,
        &screenshot_failure);
    if (!captured) {
        LOG_F(ERROR, "Viewer screenshot failed to capture: {}", screenshot_failure);
        return 1;
    }
    if (captured) {
        LOG_F(INFO, "Saved screenshot: {}", screenshot_output);
        log_prepared_model_surface_submission_stats(session.last_frame_stats());
        log_preview_scene_runtime_sync_stats(session.last_frame_stats());
    } else {
        LOG_F(ERROR, "Failed to save screenshot: {}", screenshot_output);
    }
    return captured ? 0 : 1;
}

void set_vfx_sequence_time(AppState& state, int time_ms, bool stop_after_seek)
{
    if (!state.current_scene || state.vfx_sequence_steps.empty() || state.vfx_sequence_loop_ms <= 0) {
        return;
    }

    const int clamped = std::clamp(time_ms, 0, state.vfx_sequence_loop_ms);
    state.vfx_sequence_loop_seed = 0x12345678u;
    reset_vfx_sequence(state);
    update_vfx_sequence(state, clamped);
    state.current_scene->update(clamped);
    if (stop_after_seek) {
        stop_scene_playback(state, false);
    }
}

int run_area_dump_command(const std::filesystem::path& module_path, const std::filesystem::path& output_dir,
    std::string_view user_path, bool skip_existing, size_t limit, bool show_debug_overlay)
{
    AppState state{};
    state.show_debug_overlay = show_debug_overlay;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_F(ERROR, "SDL_Init failed: {}", SDL_GetError());
        nw::kernel::services().shutdown();
        return 1;
    }

    if (!init_graphics(state, nullptr)) {
        LOG_F(ERROR, "Failed to initialize graphics for area dump");
        SDL_Quit();
        nw::kernel::services().shutdown();
        return 1;
    }

    state.camera = std::make_unique<nw::render::viewer::Camera>();
    state.camera->set_aspect_ratio(1280.0f / 720.0f);

    nw::Module* module = nullptr;
    if (!init_kernel_services(module_path.string(), user_path, &module)) {
        shutdown_graphics(state);
        SDL_Quit();
        return 1;
    }

    if (!init_render_runtime(state)) {
        nw::kernel::services().shutdown();
        shutdown_graphics(state);
        SDL_Quit();
        return 1;
    }

    if (!init_viewer_renderers(state)) {
        state.shader_provider.reset();
        shutdown_graphics(state);
        SDL_Quit();
        nw::kernel::services().shutdown();
        return 1;
    }

    std::vector<nw::Resref> areas;
    if (module->areas.is<nw::Vector<nw::Resref>>()) {
        const auto& module_areas = module->areas.as<nw::Vector<nw::Resref>>();
        areas.assign(module_areas.begin(), module_areas.end());
    }
    if (areas.empty()) {
        LOG_F(ERROR, "Module has no areas: {}", module_path.string());
        reset_viewer_renderers(state);
        state.shader_provider.reset();
        shutdown_graphics(state);
        SDL_Quit();
        nw::kernel::services().shutdown();
        return 1;
    }

    const auto out_dir = output_dir.empty() ? std::filesystem::path{"."} : output_dir;
    std::filesystem::create_directories(out_dir);

    size_t processed = 0;
    for (const auto& area : areas) {
        if (limit > 0 && processed >= limit) {
            break;
        }

        const auto screenshot_path = (out_dir / (std::string(area.view()) + ".png"));
        if (skip_existing && std::filesystem::exists(screenshot_path)) {
            LOG_F(INFO, "Skipping existing area screenshot: {}", screenshot_path.string());
            continue;
        }

        load_area(state, area.view());
        if (!state.current_scene) {
            LOG_F(ERROR, "Failed to load area for dump: {}", area.view());
            continue;
        }

        const bool captured = run_screenshot_capture(
            state, screenshot_path,
            kAreaDumpScreenshotWarmupFrames,
            kAreaDumpScreenshotMaxFrames);
        if (!captured) {
            LOG_F(ERROR, "Failed to capture area screenshot: {}", area.view());
        } else {
            LOG_F(INFO, "Captured area screenshot: {}", state.screenshot_path);
            ++processed;
        }
    }

    replace_current_scene_after_gpu_idle(state, {});
    reset_viewer_renderers(state);
    state.shader_provider.reset();
    state.camera.reset();
    shutdown_graphics(state);
    SDL_Quit();
    nw::kernel::services().shutdown();
    return 0;
}

} // namespace mudl
