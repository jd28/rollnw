#include "session.hpp"

#include "area_lighting.hpp"
#include "preview_model_animation.hpp"
#include "scene_debug.hpp"
#include "scene_particles.hpp"
#include "scene_shadow.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/log.hpp>
#include <nw/render/model_renderer.hpp>
#include <nw/render/render_service.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace nw::render::viewer {

namespace {

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

bool load_default_scene_animation(PreviewScene& scene)
{
    return scene.load_default_animations(PreferredModelAnimationContext::hold);
}

bool environment_flag_disabled(const char* name)
{
    const char* value = std::getenv(name);
    return value
        && (std::strcmp(value, "0") == 0
            || std::strcmp(value, "false") == 0
            || std::strcmp(value, "FALSE") == 0
            || std::strcmp(value, "no") == 0
            || std::strcmp(value, "NO") == 0
            || std::strcmp(value, "off") == 0
            || std::strcmp(value, "OFF") == 0);
}

bool environment_flag_enabled(const char* name)
{
    const char* value = std::getenv(name);
    return value
        && (std::strcmp(value, "1") == 0
            || std::strcmp(value, "true") == 0
            || std::strcmp(value, "TRUE") == 0
            || std::strcmp(value, "yes") == 0
            || std::strcmp(value, "YES") == 0
            || std::strcmp(value, "on") == 0
            || std::strcmp(value, "ON") == 0);
}

bool viewer_water_enabled()
{
    static const bool enabled = !environment_flag_disabled("ROLLNW_VIEWER_WATER");
    return enabled;
}

// Forward+ GPU gather is the default; set ROLLNW_VIEWER_FORWARD_PLUS_GPU_CULL=0 to force CPU gather.
bool viewer_forward_plus_gpu_cull_enabled()
{
    static const bool enabled = !environment_flag_disabled("ROLLNW_VIEWER_FORWARD_PLUS_GPU_CULL");
    return enabled;
}

bool viewer_shadows_enabled()
{
    static const bool enabled = !environment_flag_disabled("ROLLNW_VIEWER_SHADOWS");
    return enabled;
}

bool viewer_local_shadows_enabled()
{
    static const bool enabled = !environment_flag_disabled("ROLLNW_VIEWER_LOCAL_SHADOWS");
    return enabled;
}

bool viewer_depth_prepass_enabled()
{
    static const bool enabled = !environment_flag_disabled("ROLLNW_VIEWER_DEPTH_PREPASS");
    return enabled;
}

// Non-rendering bridge check: collects the common prepared model draw protocol
// and validates counts/indices without submitting through it.
bool viewer_prepared_model_draw_validation_enabled()
{
    static const bool enabled = environment_flag_enabled("ROLLNW_VIEWER_PREPARED_MODEL_DRAW_VALIDATION");
    return enabled;
}

void wait_for_preview_resources_idle(PreviewRenderResources* preview_resources)
{
    if (preview_resources && preview_resources->gfx_context()) {
        nw::gfx::wait_idle(preview_resources->gfx_context());
    }
}

using Clock = std::chrono::steady_clock;

constexpr const char* kGpuTimerShadow = "shadow";
constexpr const char* kGpuTimerDepthPrepass = "depth_prepass";
constexpr const char* kGpuTimerOpaque = "opaque";
constexpr const char* kGpuTimerWater = "water";
constexpr const char* kGpuTimerTransparent = "transparent";
constexpr const char* kGpuTimerParticles = "particles";
constexpr const char* kGpuTimerDebug = "debug";

float elapsed_seconds(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<float>(end - start).count();
}

class ScopedGpuTimer {
public:
    ScopedGpuTimer(nw::gfx::CommandList* command_list, const char* label)
        : command_list_{command_list}
        , scope_{nw::gfx::cmd_begin_gpu_timer(command_list, label)}
    {
    }

    ~ScopedGpuTimer()
    {
        if (scope_.valid()) {
            nw::gfx::cmd_end_gpu_timer(command_list_, scope_);
        }
    }

    ScopedGpuTimer(const ScopedGpuTimer&) = delete;
    ScopedGpuTimer& operator=(const ScopedGpuTimer&) = delete;

private:
    nw::gfx::CommandList* command_list_ = nullptr;
    nw::gfx::GpuTimerScope scope_{};
};

uint32_t saturating_count(size_t value)
{
    return static_cast<uint32_t>(std::min<size_t>(value, std::numeric_limits<uint32_t>::max()));
}

void add_saturating(uint32_t& target, uint32_t value) noexcept
{
    const uint64_t next = static_cast<uint64_t>(target) + static_cast<uint64_t>(value);
    target = static_cast<uint32_t>(std::min<uint64_t>(next, std::numeric_limits<uint32_t>::max()));
}

void add_prepared_surface_submission_stats(
    ViewerFrameStats& target,
    const PreviewPreparedModelSurfaceSubmissionStats& source) noexcept
{
    add_saturating(
        target.prepared_nwn_legacy_selected_draw_count,
        source.nwn_legacy.selected_draw_count);
    add_saturating(
        target.prepared_nwn_legacy_missing_sidecar_draw_count,
        source.nwn_legacy.missing_sidecar_draw_count);
    add_saturating(
        target.prepared_nwn_legacy_invalid_sidecar_draw_count,
        source.nwn_legacy.invalid_sidecar_draw_count);

    auto& render_model = target.prepared_render_model_surface_submission;
    add_saturating(render_model.submitted_surface_count, source.render_model.submitted_surface_count);
    add_saturating(render_model.dropped_invalid_surface_count, source.render_model.dropped_invalid_surface_count);
    add_saturating(render_model.submitted_run_count, source.render_model.submitted_run_count);
    add_saturating(
        render_model.opaque_cutout_submitted_run_count,
        source.render_model.opaque_cutout_submitted_run_count);
    add_saturating(render_model.water_submitted_run_count, source.render_model.water_submitted_run_count);
    add_saturating(
        render_model.transparent_submitted_run_count,
        source.render_model.transparent_submitted_run_count);
    add_saturating(render_model.all_submitted_run_count, source.render_model.all_submitted_run_count);
    add_saturating(
        render_model.skin_bindings.skinned_surface_count,
        source.render_model.skin_bindings.skinned_surface_count);
    add_saturating(
        render_model.skin_bindings.table_bound_surface_count,
        source.render_model.skin_bindings.table_bound_surface_count);
    add_saturating(
        render_model.skin_bindings.bind_pose_fallback_surface_count,
        source.render_model.skin_bindings.bind_pose_fallback_surface_count);
    add_saturating(
        render_model.skin_bindings.missing_table_surface_count,
        source.render_model.skin_bindings.missing_table_surface_count);
    add_saturating(
        render_model.skin_bindings.invalid_table_index_surface_count,
        source.render_model.skin_bindings.invalid_table_index_surface_count);
    add_saturating(
        render_model.skin_bindings.invalid_table_range_surface_count,
        source.render_model.skin_bindings.invalid_table_range_surface_count);
}

void apply_gpu_timer_result(ViewerFrameStats& stats, const nw::gfx::GpuTimerResult& result)
{
    if (!result.available || !result.label) {
        return;
    }

    if (std::strcmp(result.label, kGpuTimerShadow) == 0) {
        stats.gpu_shadow_seconds += result.seconds;
    } else if (std::strcmp(result.label, kGpuTimerDepthPrepass) == 0) {
        stats.gpu_depth_prepass_seconds += result.seconds;
    } else if (std::strcmp(result.label, kGpuTimerOpaque) == 0) {
        stats.gpu_opaque_seconds += result.seconds;
    } else if (std::strcmp(result.label, kGpuTimerWater) == 0) {
        stats.gpu_water_seconds += result.seconds;
    } else if (std::strcmp(result.label, kGpuTimerTransparent) == 0) {
        stats.gpu_transparent_seconds += result.seconds;
    } else if (std::strcmp(result.label, kGpuTimerParticles) == 0) {
        stats.gpu_particles_seconds += result.seconds;
    } else if (std::strcmp(result.label, kGpuTimerDebug) == 0) {
        stats.gpu_debug_seconds += result.seconds;
    } else if (std::strcmp(result.label, nw::render::kForwardPlusGpuTimerCull) == 0) {
        stats.gpu_forward_plus_cull_seconds += result.seconds;
    } else {
        return;
    }
    ++stats.gpu_timer_count;
}

Bounds render_bounds_for_scene(const PreviewScene& scene, ViewerSceneKind kind)
{
    if (kind == ViewerSceneKind::area
        && scene.area_render_scene
        && scene.area_render_scene->has_scene_bounds()
        && scene.area_render_scene->stats().dynamic_record_count == 0) {
        return scene.area_render_scene->scene_bounds();
    }
    return scene.current_bounds();
}

void accumulate_selected_light(ViewerFrameStats& stats, const nw::render::LocalLight& light) noexcept
{
    const float color_max = std::max(light.color.x, std::max(light.color.y, light.color.z));
    stats.local_light_selected_color_max = std::max(stats.local_light_selected_color_max, color_max);
    stats.local_light_selected_intensity_max = std::max(stats.local_light_selected_intensity_max, light.intensity);
    if (color_max > 1.0e-4f) {
        add_saturating(stats.local_light_selected_colored_total, 1u);
    }
}

void accumulate_selected_surface_lights(
    ViewerFrameStats& stats,
    const AreaRenderScene& area_scene,
    const nw::render::PreparedModelSurfaceDraw& surface,
    std::span<const nw::render::LocalLight> lights) noexcept
{
    const auto light_indices = area_scene.light_indices_for_record(surface.handle_index);
    if (light_indices.empty()) {
        return;
    }

    uint32_t selected_count = 0;
    for (const uint32_t light_index : light_indices) {
        if (light_index >= lights.size()) {
            continue;
        }
        ++selected_count;
        accumulate_selected_light(stats, lights[light_index]);
    }

    if (selected_count == 0) {
        return;
    }

    add_saturating(stats.local_light_selected_draw_count, 1u);
    add_saturating(stats.local_light_selected_total, selected_count);
    stats.local_light_selected_max = std::max(stats.local_light_selected_max, selected_count);
}

void accumulate_selected_surface_lights(
    ViewerFrameStats& stats,
    const AreaRenderScene& area_scene,
    std::span<const uint32_t> surface_indices,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    std::span<const nw::render::LocalLight> lights) noexcept
{
    for (const uint32_t surface_index : surface_indices) {
        if (surface_index < surfaces.size()) {
            accumulate_selected_surface_lights(stats, area_scene, surfaces[surface_index], lights);
        }
    }
}

Bounds shadow_bounds_for_scene(
    const PreviewScene& scene,
    ViewerSceneKind kind,
    const AreaRenderFrame* area_frame)
{
    if (kind == ViewerSceneKind::area
        && area_frame
        && area_frame->has_shadow_caster_bounds()) {
        return area_frame->shadow_caster_bounds();
    }
    return render_bounds_for_scene(scene, kind);
}

uint64_t counter_delta(uint64_t after, uint64_t before)
{
    return after >= before ? after - before : 0;
}

nw::gfx::CommandStats command_stats_delta(
    const nw::gfx::CommandStats& after,
    const nw::gfx::CommandStats& before)
{
    nw::gfx::CommandStats result{};
    result.render_pass_count = counter_delta(after.render_pass_count, before.render_pass_count);
    result.pipeline_bind_count = counter_delta(after.pipeline_bind_count, before.pipeline_bind_count);
    result.pipeline_bind_skipped_count = counter_delta(after.pipeline_bind_skipped_count, before.pipeline_bind_skipped_count);
    result.vertex_buffer_bind_count = counter_delta(after.vertex_buffer_bind_count, before.vertex_buffer_bind_count);
    result.vertex_buffer_bind_skipped_count = counter_delta(after.vertex_buffer_bind_skipped_count, before.vertex_buffer_bind_skipped_count);
    result.index_buffer_bind_count = counter_delta(after.index_buffer_bind_count, before.index_buffer_bind_count);
    result.index_buffer_bind_skipped_count = counter_delta(after.index_buffer_bind_skipped_count, before.index_buffer_bind_skipped_count);
    result.resource_bind_count = counter_delta(after.resource_bind_count, before.resource_bind_count);
    result.resource_bind_skipped_count = counter_delta(after.resource_bind_skipped_count, before.resource_bind_skipped_count);
    result.descriptor_buffer_bind_count = counter_delta(after.descriptor_buffer_bind_count, before.descriptor_buffer_bind_count);
    result.descriptor_buffer_bind_skipped_count = counter_delta(after.descriptor_buffer_bind_skipped_count, before.descriptor_buffer_bind_skipped_count);
    result.draw_count = counter_delta(after.draw_count, before.draw_count);
    result.indexed_draw_count = counter_delta(after.indexed_draw_count, before.indexed_draw_count);
    result.nonindexed_draw_count = counter_delta(after.nonindexed_draw_count, before.nonindexed_draw_count);
    result.indirect_draw_call_count = counter_delta(after.indirect_draw_call_count, before.indirect_draw_call_count);
    result.draw_instance_count = counter_delta(after.draw_instance_count, before.draw_instance_count);
    result.draw_index_count = counter_delta(after.draw_index_count, before.draw_index_count);
    result.draw_vertex_count = counter_delta(after.draw_vertex_count, before.draw_vertex_count);
    result.dispatch_count = counter_delta(after.dispatch_count, before.dispatch_count);
    result.uniform_allocation_count = counter_delta(after.uniform_allocation_count, before.uniform_allocation_count);
    result.uniform_allocation_bytes = counter_delta(after.uniform_allocation_bytes, before.uniform_allocation_bytes);
    result.vertex_allocation_count = counter_delta(after.vertex_allocation_count, before.vertex_allocation_count);
    result.vertex_allocation_bytes = counter_delta(after.vertex_allocation_bytes, before.vertex_allocation_bytes);
    return result;
}

size_t live_particle_count(const PreviewScene& scene)
{
    size_t result = 0;
    for (const auto& scene_particles : scene.particles) {
        result += scene_particles.system.particles.core.position.size();
    }
    return result;
}

bool scene_forward_plus_lights_enabled(
    ViewerSceneKind kind,
    bool area_lights_enabled,
    const ForwardPlusRenderPolicy& policy) noexcept
{
    if (!policy.enabled) {
        return false;
    }
    return kind == ViewerSceneKind::area ? area_lights_enabled : kind != ViewerSceneKind::none;
}

ForwardPlusRenderPolicy resolve_scene_forward_plus_policy(
    const ForwardPlusRenderPolicy& base_policy,
    ViewerSceneKind kind,
    const AreaRenderFrame* area_frame)
{
    ForwardPlusRenderPolicy policy = base_policy;
    if (kind == ViewerSceneKind::area && policy.auto_configure_area) {
        if (area_frame) {
            const auto& stats = area_frame->stats();
            policy.config = area_forward_plus_config(stats.visible_record_count, stats.visible_light_count);
        } else {
            policy.config = {};
        }
    }
    return policy;
}

void bootstrap_scene_playback(PreviewScene& scene)
{
    const bool loaded_legacy_animation = load_default_scene_animation(scene);
    rebuild_render_model_animation_instances(scene, 0, 0.0f);
    const bool loaded_render_model_animation = set_default_render_model_animation_clip(
        scene,
        kPreferredRenderModelHoldAnimations,
        0.0f);
    const bool loaded_animation = loaded_legacy_animation || loaded_render_model_animation;
    if (loaded_animation) {
        advance_render_model_animation_times(scene, 0.033f);
        scene.update(33);
    } else {
        scene.rebuild_particles();
    }

    if (!scene.particles.empty()) {
        const int32_t particle_prime_ms = compute_particle_prime_ms(scene, loaded_animation);
        scene.update(particle_prime_ms);
        LOG_F(INFO, "Viewer session: primed {} particle systems ({} live particles, {} ms)",
            scene.particles.size(),
            live_particle_count(scene),
            particle_prime_ms);
    }
}

} // namespace

ViewerSession::ViewerSession(PreviewRenderResources& preview_resources, SceneDebugRenderer* debug_renderer)
    : preview_resources_(&preview_resources)
    , debug_renderer_(debug_renderer)
{
    preview_scene_load_options_ = default_preview_scene_load_options();
    forward_plus_policy_.gpu_culling = viewer_forward_plus_gpu_cull_enabled();
    completed_gpu_timer_results_.reserve(8);
}

ViewerSession::~ViewerSession()
{
    clear();
}

bool ViewerSession::load_model(std::string_view resref)
{
    if (!preview_resources_) {
        return false;
    }

    auto scene = load_preview_scene(*preview_resources_, resref, preview_scene_load_options_);
    if (!scene) {
        LOG_F(ERROR, "Viewer session: failed to load model '{}'", resref);
        return false;
    }

    if (!set_scene(std::move(scene), ViewerSceneKind::model, std::string{resref})) {
        return false;
    }

    if (scene_) {
        bootstrap_scene_playback(*scene_);
    }

    return true;
}

bool ViewerSession::load_area(std::string_view resref)
{
    if (!preview_resources_) {
        return false;
    }

    auto scene = load_area_scene(*preview_resources_, resref, preview_scene_load_options_);
    if (!scene) {
        LOG_F(ERROR, "Viewer session: failed to load area '{}'", resref);
        return false;
    }
    if (!set_scene(std::move(scene), ViewerSceneKind::area, std::string{resref})) {
        return false;
    }

    if (scene_) {
        bootstrap_scene_playback(*scene_);
    }

    return true;
}

bool ViewerSession::load_object_file(const std::filesystem::path& path)
{
    if (!preview_resources_) {
        return false;
    }

    auto scene = load_preview_scene(*preview_resources_, path.string(), preview_scene_load_options_);
    if (!scene) {
        LOG_F(ERROR, "Viewer session: failed to load object file '{}'", path.string());
        return false;
    }
    if (!set_scene(std::move(scene), ViewerSceneKind::object_file, path.string())) {
        return false;
    }
    if (scene_) {
        bootstrap_scene_playback(*scene_);
    }
    return true;
}

void ViewerSession::clear()
{
    if (scene_) {
        wait_for_preview_resources_idle(preview_resources_);
    }
    scene_.reset();
    forward_plus_frame_.clear();
    area_frame_.clear();
    prepared_model_draws_.clear();
    prepared_model_surfaces_.clear();
    area_visible_model_instance_handles_.clear();
    prepared_draw_scratch_.clear();
    clear_area_visibility_mask();
    scene_kind_ = ViewerSceneKind::none;
    loaded_source_.clear();
    scene_time_seconds_ = 0.0f;
    area_day_night_elapsed_seconds_ = 0.0f;
}

void ViewerSession::tick(int32_t dt_ms)
{
    const auto tick_start = Clock::now();
    if (dt_ms < 0) {
        dt_ms = 0;
    }

    camera_.update(static_cast<float>(dt_ms) * 0.001f);
    if (playing_ && scene_) {
        const float dt = static_cast<float>(dt_ms) * 0.001f;
        scene_time_seconds_ += dt;
        if (area_day_night_autoplay_ && supports_area_day_night_cycle(*scene_)) {
            set_area_day_night_elapsed_seconds(area_day_night_elapsed_seconds_ + dt, true);
        }
        advance_render_model_animation_times(*scene_, dt);
        scene_->update(dt_ms);
    }
    last_tick_seconds_ = elapsed_seconds(tick_start, Clock::now());
}

void ViewerSession::render(nw::gfx::CommandList* command_list, ViewerViewport viewport)
{
    if (!preview_resources_ || !scene_ || !command_list || !viewport.valid()) {
        last_frame_stats_ = {};
        return;
    }

    ViewerFrameStats frame_stats{};
    frame_stats.tick_seconds = last_tick_seconds_;
    if (nw::gfx::get_completed_gpu_timer_results(preview_resources_->context(), completed_gpu_timer_results_)) {
        for (const auto& result : completed_gpu_timer_results_) {
            apply_gpu_timer_result(frame_stats, result);
        }
    }
    frame_stats.model_count = saturating_count(scene_->models.size());
    frame_stats.static_model_count = saturating_count(scene_->static_models.size());
    frame_stats.particle_system_count = saturating_count(scene_->particles.size());
    frame_stats.prepared_render_model_draws_enabled = true;
    frame_stats.prepared_nwn_legacy_draws_enabled = scene_kind_ != ViewerSceneKind::area;
    const bool shadows_enabled = viewer_shadows_enabled() && (area_shadows_enabled_ || scene_kind_ != ViewerSceneKind::area);
    const bool validate_prepared_model_draws_this_frame = viewer_prepared_model_draw_validation_enabled();
    const bool collect_prepared_model_surfaces_for_shadows = shadows_enabled
        && scene_kind_ != ViewerSceneKind::area;
    const bool collect_all_prepared_model_draws_this_frame = frame_stats.prepared_nwn_legacy_draws_enabled
        || validate_prepared_model_draws_this_frame
        || collect_prepared_model_surfaces_for_shadows;
    const bool collect_area_visible_prepared_model_draws = scene_kind_ == ViewerSceneKind::area
        && scene_->area_render_scene;

    if (!scene_->model_attachments.empty()) {
        frame_stats.runtime_sync_stats = sync_model_instance_runtime_state(*scene_);
    }
    frame_stats.render_model_animation_sample_stats = sample_render_model_animations(
        render_model_animation_samples_, *scene_);

    const auto apply_prepared_model_draw_stats = [&]() {
        if (frame_stats.prepared_render_model_draws_enabled) {
            frame_stats.prepared_render_model_draw_count = prepared_model_draws_.common.stats.render_model_draw_count;
        }
        if (frame_stats.prepared_nwn_legacy_draws_enabled) {
            frame_stats.prepared_nwn_legacy_draw_count = prepared_model_draws_.common.stats.nwn_legacy_draw_count;
        }
        frame_stats.prepared_model_draw_material_fallback_count = prepared_model_draws_.common.stats.material_fallback_draw_count;
        frame_stats.prepared_model_draw_render_model_material_fallback_count = prepared_model_draws_.common.stats.render_model_material_fallback_draw_count;
        frame_stats.prepared_model_draw_nwn_legacy_material_fallback_count = prepared_model_draws_.common.stats.nwn_legacy_material_fallback_draw_count;
        frame_stats.prepared_model_surface_stats_enabled = true;
        frame_stats.prepared_model_surface_stats = prepared_model_surfaces_.stats;
        frame_stats.prepared_render_model_skin_table_stats = prepared_model_surfaces_.render_model_skins.stats;
        frame_stats.prepared_model_surface_materials = nw::render::prepared_model_surface_material_stats(
            std::span<const nw::render::PreparedModelSurfaceDraw>{
                prepared_model_surfaces_.draws.data(),
                prepared_model_surfaces_.draws.size()});
        frame_stats.prepared_model_surface_material_bindings = nw::render::validate_prepared_model_surface_material_bindings(
            std::span<const nw::render::PreparedModelSurfaceDraw>{
                prepared_model_surfaces_.draws.data(),
                prepared_model_surfaces_.draws.size()},
            prepared_model_draws_.common,
            prepared_model_draws_.ranges);
    };

    if (!collect_area_visible_prepared_model_draws) {
        if (collect_all_prepared_model_draws_this_frame) {
            collect_prepared_model_surface_draws(prepared_model_draws_, prepared_model_surfaces_, *scene_);
        } else {
            collect_prepared_model_surface_draws(
                prepared_model_draws_,
                prepared_model_surfaces_,
                *scene_,
                std::span<const nw::render::ModelInstanceHandle>{scene_->static_model_instance_handles});
        }
        apply_prepared_model_draw_stats();
    } else {
        prepared_model_draws_.clear();
        prepared_model_surfaces_.clear();
    }
    if (validate_prepared_model_draws_this_frame && !collect_area_visible_prepared_model_draws) {
        frame_stats.prepared_model_draw_validation_enabled = true;
        frame_stats.prepared_model_draw_validation = validate_prepared_model_draws(*scene_, prepared_model_draws_);
    }
    if (scene_->area_render_scene) {
        const auto& cache_stats = scene_->area_render_scene->stats();
        frame_stats.area_cache_record_count = cache_stats.record_count;
        frame_stats.area_cache_static_record_count = cache_stats.static_record_count;
        frame_stats.area_cache_dynamic_record_count = cache_stats.dynamic_record_count;
        frame_stats.area_cache_opaque_record_count = cache_stats.opaque_cutout_record_count;
        frame_stats.area_cache_water_record_count = cache_stats.water_record_count;
        frame_stats.area_cache_transparent_record_count = cache_stats.transparent_record_count;
        frame_stats.area_cache_shadow_caster_record_count = cache_stats.shadow_caster_record_count;
        frame_stats.area_cache_prepared_draw_count = cache_stats.prepared_draw_count;
        frame_stats.area_cache_shadow_prepared_surface_count = cache_stats.shadow_prepared_surface_count;
        frame_stats.area_cache_static_geometry_mesh_count = cache_stats.static_geometry_mesh_count;
        frame_stats.area_cache_static_geometry_vertex_count = cache_stats.static_geometry_vertex_count;
        frame_stats.area_cache_static_geometry_index_count = cache_stats.static_geometry_index_count;
        frame_stats.area_cache_static_geometry_bytes = cache_stats.static_geometry_bytes;
        frame_stats.area_cache_light_index_count = cache_stats.light_index_count;
        frame_stats.area_cache_local_light_count = cache_stats.local_light_count;
        frame_stats.area_cache_max_light_indices_per_record = cache_stats.max_light_indices_per_record;
        frame_stats.area_cache_max_shadow_prepared_surfaces_per_record = cache_stats.max_shadow_prepared_surfaces_per_record;
        frame_stats.area_cache_chunk_light_index_count = cache_stats.chunk_light_index_count;
        frame_stats.area_cache_max_light_indices_per_chunk = cache_stats.max_light_indices_per_chunk;
        frame_stats.area_cache_chunk_count = cache_stats.chunk_count;
        frame_stats.area_cache_nonempty_chunk_count = cache_stats.nonempty_chunk_count;
        frame_stats.area_cache_max_records_per_chunk = cache_stats.max_records_per_chunk;
    }
    const bool local_lights_enabled = scene_forward_plus_lights_enabled(scene_kind_, area_lights_enabled_, forward_plus_policy_);
    if (local_lights_enabled) {
        frame_stats.local_light_count = saturating_count(scene_->local_lights.size());
        for (const auto& light : scene_->local_lights) {
            if (light.source == SceneLocalLightSource::tile_model) {
                ++frame_stats.local_light_tile_model_count;
            } else if (light.source == SceneLocalLightSource::placeable_table) {
                ++frame_stats.local_light_placeable_table_count;
            } else {
                ++frame_stats.local_light_authored_model_count;
            }
            const float color_max = std::max(light.color.x, std::max(light.color.y, light.color.z));
            frame_stats.local_light_color_max = std::max(frame_stats.local_light_color_max, color_max);
            frame_stats.local_light_intensity_max = std::max(frame_stats.local_light_intensity_max, light.intensity);
            if (color_max > 1.0e-4f) {
                ++frame_stats.local_light_colored_count;
            }
        }
    }
    const auto render_start = Clock::now();
    const auto command_stats_sample = [&]() {
        nw::gfx::CommandStats stats{};
        nw::gfx::get_command_stats(command_list, stats);
        return stats;
    };
    const auto render_command_stats_start = command_stats_sample();

    update_viewport(viewport);
    auto render_context = make_render_context();
    const auto area_prepare_start = Clock::now();
    AreaRenderScene* area_render_scene = nullptr;
    AreaRenderFrame* area_render_frame = nullptr;
    if (scene_kind_ == ViewerSceneKind::area && scene_->area_render_scene) {
        area_render_scene = scene_->area_render_scene.get();
        if (area_visibility_radius_tiles_ >= 0) {
            area_visibility_mask_visible_chunk_count_ = rebuild_area_visibility_mask(
                *area_render_scene,
                AreaVisibilityMaskOptions{
                    .camera_position = camera_.get_position(),
                    .camera_target = camera_.get_target(),
                    .radius_tiles = area_visibility_radius_tiles_,
                    .half_angle_degrees = area_visibility_half_angle_degrees_,
                    .mode = area_visibility_mask_mode_,
                },
                area_visibility_mask_);
            area_visibility_mask_enabled_ = area_visibility_mask_visible_chunk_count_ > 0;
        }
        area_render_scene->refresh_runtime_records(*scene_);
        AreaRenderCullContext area_cull{};
        area_cull.enabled = true;
        area_cull.view_projection = render_context.projection * render_context.view;
        area_cull.collect_direct_render_model_records = false;
        if (area_visibility_mask_enabled_
            && area_visibility_mask_.size() >= scene_->area_render_scene->stats().chunk_count) {
            area_cull.chunk_visibility_enabled = true;
            area_cull.visible_chunk_mask = area_visibility_mask_;
        }
        prepare_area_frame(*area_render_scene, area_frame_, area_cull);
        area_render_frame = &area_frame_;
        const auto& area_frame_stats = area_frame_.stats();
        frame_stats.area_frame_visible_record_count = area_frame_stats.visible_record_count;
        frame_stats.area_frame_visible_static_record_count = area_frame_stats.visible_static_record_count;
        frame_stats.area_frame_visible_dynamic_record_count = area_frame_stats.visible_dynamic_record_count;
        frame_stats.area_frame_visible_chunk_count = area_frame_stats.visible_chunk_count;
        frame_stats.area_frame_culled_record_count = area_frame_stats.culled_record_count;
        frame_stats.area_frame_culled_chunk_count = area_frame_stats.culled_chunk_count;
        frame_stats.area_frame_visibility_culled_record_count = area_frame_stats.visibility_culled_record_count;
        frame_stats.area_frame_visibility_culled_chunk_count = area_frame_stats.visibility_culled_chunk_count;
        frame_stats.area_frame_opaque_record_count = area_frame_stats.opaque_cutout_record_count;
        frame_stats.area_frame_water_record_count = area_frame_stats.water_record_count;
        frame_stats.area_frame_transparent_record_count = area_frame_stats.transparent_record_count;
        frame_stats.area_frame_shadow_caster_record_count = area_frame_stats.shadow_caster_record_count;
        frame_stats.area_frame_visible_prepared_surface_count = area_frame_stats.visible_prepared_surface_count;
        frame_stats.area_frame_visible_light_count = area_frame_stats.visible_light_count;
        frame_stats.area_frame_uses_cached_draw_lists = area_frame_stats.uses_cached_draw_lists;
        frame_stats.area_frame_uses_sorted_static_draw_lists = area_frame_stats.uses_sorted_static_draw_lists;
        frame_stats.area_frame_material_indirect_sidecar_bridge =
            area_frame_stats.material_indirect_sidecar_bridge;
        frame_stats.area_frame_static_material_indirect_sidecar_bridge =
            area_frame_stats.static_material_indirect_sidecar_bridge;
        frame_stats.area_prepare_seconds = elapsed_seconds(area_prepare_start, Clock::now());
        if (collect_area_visible_prepared_model_draws) {
            const auto visible_nwn_legacy_handles = area_render_frame->visible_nwn_legacy_instance_handles();
            const auto visible_render_model_handles = area_render_frame->visible_render_model_instance_handles();
            area_visible_model_instance_handles_.clear();
            area_visible_model_instance_handles_.reserve(
                visible_nwn_legacy_handles.size() + visible_render_model_handles.size());
            area_visible_model_instance_handles_.insert(
                area_visible_model_instance_handles_.end(),
                visible_nwn_legacy_handles.begin(),
                visible_nwn_legacy_handles.end());
            area_visible_model_instance_handles_.insert(
                area_visible_model_instance_handles_.end(),
                visible_render_model_handles.begin(),
                visible_render_model_handles.end());
            frame_stats.prepared_nwn_legacy_draws_enabled = !visible_nwn_legacy_handles.empty();
            collect_prepared_model_surface_draws(
                prepared_model_draws_,
                prepared_model_surfaces_,
                *scene_,
                std::span<const nw::render::ModelInstanceHandle>{
                    area_visible_model_instance_handles_.data(),
                    area_visible_model_instance_handles_.size()});
            apply_prepared_model_draw_stats();
            if (validate_prepared_model_draws_this_frame) {
                frame_stats.prepared_model_draw_validation_enabled = true;
                frame_stats.prepared_model_draw_validation = validate_prepared_model_draws(
                    *scene_,
                    prepared_model_draws_,
                    std::span<const nw::render::ModelInstanceHandle>{
                        area_visible_model_instance_handles_.data(),
                        area_visible_model_instance_handles_.size()});
            }
        }
        const auto lights = std::span<const nw::render::LocalLight>{render_context.local_lights};
        const auto& surfaces = area_render_scene->prepared_model_surface_draws().draws;
        accumulate_selected_surface_lights(
            frame_stats,
            *area_render_scene,
            area_render_frame->visible_opaque_prepared_surface_indices(),
            surfaces,
            lights);
        accumulate_selected_surface_lights(
            frame_stats,
            *area_render_scene,
            area_render_frame->visible_cutout_prepared_surface_indices(),
            surfaces,
            lights);
        accumulate_selected_surface_lights(
            frame_stats,
            *area_render_scene,
            area_render_frame->visible_water_prepared_surface_indices(),
            surfaces,
            lights);
        accumulate_selected_surface_lights(
            frame_stats,
            *area_render_scene,
            area_render_frame->visible_transparent_prepared_surface_indices(),
            surfaces,
            lights);
    }
    auto& render_service = nw::render::render_service();
    // Select shadow-casting local lights and assign atlas slots before Forward+
    // packing so each ForwardPlusLightGpu carries its shadow slot in params.z.
    const bool local_shadows_enabled = local_lights_enabled
        && local_shadows_enabled_
        && viewer_shadows_enabled()
        && viewer_local_shadows_enabled()
        && (area_shadows_enabled_ || scene_kind_ != ViewerSceneKind::area);
    if (local_shadows_enabled && !scene_->render_local_lights.empty()) {
        const bool use_area_light_candidates = scene_kind_ == ViewerSceneKind::area
            && area_render_frame
            && area_render_frame->uses_filtered_light_indices();
        const std::optional<std::span<const uint32_t>> local_shadow_light_indices = use_area_light_candidates
            ? std::optional<std::span<const uint32_t>>{area_render_frame->visible_light_indices()}
            : std::nullopt;
        render_context.local_shadows = resolve_local_shadows(
            render_context, scene_->render_local_lights, local_shadow_light_indices);
    }
    if (local_lights_enabled && !render_context.local_lights.empty()) {
        ForwardPlusRenderPolicy forward_plus_policy = resolve_scene_forward_plus_policy(forward_plus_policy_, scene_kind_, area_render_frame);
        ForwardPlusRenderResult forward_plus_result{};
        auto& forward_plus_renderer = render_service.forward_plus_renderer();
        const auto apply_forward_plus_stats = [&](const auto& result) {
            const auto& forward_stats = render_context.forward_plus.stats;
            frame_stats.forward_plus_prepared = result.prepared;
            frame_stats.forward_plus_active = render_context.forward_plus.enabled;
            frame_stats.forward_plus_empty = forward_stats.light_count == 0;
            frame_stats.forward_plus_gpu_culling = result.gpu_culling;
            frame_stats.forward_plus_prepare_seconds = result.prepare_seconds;
            frame_stats.forward_plus_light_cull_seconds = forward_stats.light_cull_seconds;
            frame_stats.forward_plus_cluster_header_seconds = forward_stats.cluster_header_seconds;
            frame_stats.forward_plus_cluster_index_seconds = forward_stats.cluster_index_seconds;
            frame_stats.forward_plus_upload_seconds = result.upload_seconds;
            frame_stats.forward_plus_light_count = forward_stats.light_count;
            frame_stats.forward_plus_cluster_count = forward_stats.cluster_count;
            frame_stats.forward_plus_active_cluster_count = forward_stats.active_cluster_count;
            frame_stats.forward_plus_cluster_light_index_count = forward_stats.cluster_light_index_count;
            frame_stats.forward_plus_cluster_light_index_capacity = forward_stats.cluster_light_index_capacity;
            frame_stats.forward_plus_max_lights_per_cluster = forward_stats.max_lights_per_cluster;
            frame_stats.forward_plus_overflow_cluster_count = forward_stats.overflow_cluster_count;
            frame_stats.forward_plus_overflow_light_count = forward_stats.overflow_light_count;
            frame_stats.forward_plus_upload_bytes = forward_stats.upload_bytes;
            frame_stats.forward_plus_tile_size = render_context.forward_plus.tile_size;
            frame_stats.forward_plus_depth_slices = render_context.forward_plus.cluster_dimensions.z;
        };
        const bool use_filtered_lights = scene_kind_ == ViewerSceneKind::area
            && area_render_frame
            && area_render_frame->uses_filtered_light_indices();
        const std::optional<std::span<const uint32_t>> light_indices = use_filtered_lights
            ? std::optional<std::span<const uint32_t>>{area_render_frame->visible_light_indices()}
            : std::nullopt;
        forward_plus_result = forward_plus_renderer.prepare_render_context(
            forward_plus_frame_,
            command_list,
            render_context,
            viewport.x,
            viewport.y,
            viewport.width,
            viewport.height,
            forward_plus_policy,
            light_indices);
        apply_forward_plus_stats(forward_plus_result);
    } else {
        forward_plus_frame_.clear();
        render_context.forward_plus = {};
    }
    nw::gfx::StorageSpan static_material_draw_data{};
    auto render_model_ctx = render_service.model_render_context();
    if (area_render_scene && area_render_frame && area_render_frame->uses_sorted_static_draw_lists()) {
        static_material_draw_data = refresh_area_static_material_draw_data(
            render_service,
            *area_render_scene,
            *area_render_frame,
            prepared_draw_scratch_,
            &frame_stats.area_static_material_draw_data_sidecar_bridge);
    }
    const auto setup_end = Clock::now();
    frame_stats.setup_seconds = elapsed_seconds(render_start, setup_end);

    prepared_draw_scratch_.begin_frame();

    if (shadows_enabled) {
        const auto shadow_command_stats_start = command_stats_sample();
        const auto shadow_start = Clock::now();
        const uint32_t shadow_resolution = viewer_shadow_map_resolution();
        const uint32_t shadow_cascade_count = viewer_shadow_cascade_count();
        const Bounds shadow_bounds = shadow_bounds_for_scene(*scene_, scene_kind_, area_render_frame);
        render_context.shadow = resolve_scene_shadow(
            render_context, shadow_bounds, shadow_resolution, shadow_cascade_count);
        render_context.shadow.debug_mode = shadow_debug_mode_;
        ShadowRenderStats shadow_stats{};
        bool shadow_rendered = false;
        {
            const ScopedGpuTimer gpu_timer{command_list, kGpuTimerShadow};
            shadow_rendered = render_scene_shadow_maps(
                render_service, render_model_ctx,
                command_list, *scene_, render_context.shadow, shadow_resolution, &shadow_stats,
                area_render_scene ? &area_frame_ : nullptr, &prepared_draw_scratch_,
                &prepared_model_draws_,
                &prepared_model_surfaces_);
        }
        const auto shadow_end = Clock::now();
        frame_stats.shadow_command_stats = command_stats_delta(command_stats_sample(), shadow_command_stats_start);
        frame_stats.shadow_seconds = elapsed_seconds(shadow_start, shadow_end);
        frame_stats.shadows_rendered = shadow_rendered && render_context.shadow.enabled;
        frame_stats.shadow_cascade_count = shadow_stats.cascade_count;
        frame_stats.shadow_resolution = shadow_resolution;
        frame_stats.shadow_caster_model_count = shadow_stats.caster_model_count;
        frame_stats.shadow_no_caster_model_count = shadow_stats.no_caster_model_count;
        frame_stats.shadow_submitted_model_count = shadow_stats.submitted_model_count;
        frame_stats.shadow_culled_model_count = shadow_stats.culled_model_count;
        frame_stats.shadow_prepared_surface_shadow_range_count = shadow_stats.prepared_surface_shadow_range_count;
        frame_stats.shadow_prepared_surface_invalid_range_count = shadow_stats.prepared_surface_invalid_range_count;
        frame_stats.shadow_area_indirect_sidecar_bridge = shadow_stats.area_indirect_sidecar_bridge;
        frame_stats.shadow_area_sidecar_bridge = shadow_stats.area_sidecar_bridge;
    }

    if (local_shadows_enabled && render_context.local_shadows.count > 0) {
        const auto local_shadow_command_stats_start = command_stats_sample();
        const auto local_shadow_start = Clock::now();
        LocalShadowRenderStats local_shadow_stats{};
        bool local_shadow_rendered = false;
        {
            const ScopedGpuTimer gpu_timer{command_list, kGpuTimerShadow};
            local_shadow_rendered = render_local_shadow_maps(
                render_service, render_model_ctx, command_list,
                *scene_, render_context.local_shadows, viewer_shadow_map_resolution(), &local_shadow_stats,
                &prepared_draw_scratch_, area_render_scene ? &area_frame_ : nullptr,
                &prepared_model_draws_,
                &prepared_model_surfaces_);
        }
        const auto local_shadow_end = Clock::now();
        frame_stats.local_shadow_command_stats = command_stats_delta(command_stats_sample(), local_shadow_command_stats_start);
        frame_stats.local_shadow_seconds = elapsed_seconds(local_shadow_start, local_shadow_end);
        frame_stats.local_shadows_rendered = local_shadow_rendered && render_context.local_shadows.count > 0;
        frame_stats.local_shadow_caster_light_count = local_shadow_stats.caster_light_count;
        frame_stats.local_shadow_submitted_model_count = local_shadow_stats.submitted_model_count;
        frame_stats.local_shadow_culled_model_count = local_shadow_stats.culled_model_count;
        frame_stats.local_shadow_area_sidecar_bridge = local_shadow_stats.area_sidecar_bridge;
    }

    const auto render_scene_pass = [&](const nw::render::RenderContext& pass_context,
                                       nw::render::RenderPassSelection pass) {
        if (area_render_scene && area_render_frame) {
            const auto static_material_batches = area_static_material_draw_batches_for_pass(
                *area_render_frame, pass, static_material_draw_data);
            frame_stats.area_static_material_sidecar_submission.add(
                render_area_static_material_draw_batches(
                    render_service,
                    command_list,
                    *area_render_scene,
                    static_material_batches.span(),
                    pass_context,
                    prepared_draw_scratch_));
        }
        const std::span<const nw::render::PreparedModelSurfaceDraw> surfaces{
            prepared_model_surfaces_.draws.data(),
            prepared_model_surfaces_.draws.size()};
        const auto* render_model_skin_table = &prepared_model_surfaces_.render_model_skins;
        PreviewPreparedModelSurfaceSubmissionStats submission_stats{};
        if (frame_stats.prepared_nwn_legacy_draws_enabled) {
            submission_stats.nwn_legacy = render_prepared_nwn_legacy_surface_draws(
                render_service,
                command_list,
                prepared_model_draws_,
                surfaces,
                pass_context,
                pass,
                prepared_draw_scratch_,
                nwn_prepared_draw_items_);
        }
        submission_stats.render_model = render_prepared_render_model_surface_draws(
            render_model_ctx,
            command_list,
            *scene_,
            surfaces,
            pass_context,
            pass,
            render_model_skin_table,
            &prepared_model_surfaces_.render_model_surface_packets);
        add_prepared_surface_submission_stats(frame_stats, submission_stats);
    };

    const bool scene_has_water = viewer_water_enabled() && scene_->contains_water();
    nw::gfx::cmd_begin_render(command_list, {}, nw::gfx::RenderLoadOp::load);
    nw::gfx::cmd_set_viewport(command_list,
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height),
        0.0f,
        1.0f);
    nw::gfx::cmd_set_scissor(command_list, viewport.x, viewport.y, viewport.width, viewport.height);

    // Depth pre-pass: lay down opaque static depth so the opaque color pass (LESS_EQUAL)
    // gets early-Z rejection of occluded fragments before the PBR + clustered-light shader.
    if (viewer_depth_prepass_enabled() && area_render_scene && area_render_frame) {
        const ScopedGpuTimer gpu_timer{command_list, kGpuTimerDepthPrepass};
        const AreaStaticMaterialDrawBatch depth_batch = area_static_material_depth_prepass_batch(
            *area_render_frame,
            static_material_draw_data);
        AreaPreparedSurfaceSidecarStats depth_sidecar_stats{};
        render_area_static_material_depth_prepass(
            render_service,
            command_list,
            *area_render_scene,
            depth_batch,
            render_context,
            prepared_draw_scratch_,
            &depth_sidecar_stats);
        frame_stats.area_static_material_sidecar_submission.add(depth_sidecar_stats);
    }

    const auto opaque_command_stats_start = command_stats_sample();
    const auto opaque_start = Clock::now();
    {
        const ScopedGpuTimer gpu_timer{command_list, kGpuTimerOpaque};
        render_scene_pass(render_context, nw::render::RenderPassSelection::opaque_cutout);
    }
    const auto opaque_end = Clock::now();
    frame_stats.opaque_command_stats = command_stats_delta(command_stats_sample(), opaque_command_stats_start);
    frame_stats.opaque_seconds = elapsed_seconds(opaque_start, opaque_end);
    frame_stats.main_pass_count += 1;
    if (scene_has_water) {
        const auto water_command_stats_start = command_stats_sample();
        const auto water_start = Clock::now();
        {
            const ScopedGpuTimer gpu_timer{command_list, kGpuTimerWater};
            render_scene_pass(render_context, nw::render::RenderPassSelection::water);
        }
        const auto water_end = Clock::now();
        frame_stats.water_command_stats = command_stats_delta(command_stats_sample(), water_command_stats_start);
        frame_stats.water_seconds = elapsed_seconds(water_start, water_end);
        frame_stats.water_rendered = true;
        frame_stats.main_pass_count += 1;
    }
    const auto transparent_command_stats_start = command_stats_sample();
    const auto transparent_start = Clock::now();
    {
        const ScopedGpuTimer gpu_timer{command_list, kGpuTimerTransparent};
        render_scene_pass(render_context, nw::render::RenderPassSelection::transparent);
    }
    const auto transparent_end = Clock::now();
    frame_stats.transparent_command_stats = command_stats_delta(command_stats_sample(), transparent_command_stats_start);
    frame_stats.transparent_seconds = elapsed_seconds(transparent_start, transparent_end);
    frame_stats.main_pass_count += 1;
    const auto particle_command_stats_start = command_stats_sample();
    const auto particles_start = Clock::now();
    ParticleRenderStats particle_render_stats{};
    {
        const ScopedGpuTimer gpu_timer{command_list, kGpuTimerParticles};
        particle_render_stats = render_scene_particles(
            render_service,
            command_list,
            *scene_,
            render_context,
            ParticleRenderFilter{
                .area_scene = area_render_scene,
                .area_frame = area_render_frame,
            });
    }
    const auto particles_end = Clock::now();
    frame_stats.particle_command_stats = command_stats_delta(command_stats_sample(), particle_command_stats_start);
    frame_stats.particles_seconds = elapsed_seconds(particles_start, particles_end);
    frame_stats.particle_submitted_system_count = particle_render_stats.submitted_system_count;
    frame_stats.particle_culled_system_count = particle_render_stats.culled_system_count;
    frame_stats.particle_invalid_material_packet_count = particle_render_stats.invalid_material_packet_count;
    frame_stats.particle_mesh_packet_count = particle_render_stats.mesh_packet_count;
    frame_stats.particle_mesh_particle_count = particle_render_stats.mesh_particle_count;
    frame_stats.particle_mesh_submitted_particle_count = particle_render_stats.mesh_submitted_particle_count;
    frame_stats.particle_mesh_dropped_particle_count = particle_render_stats.mesh_dropped_particle_count;
    frame_stats.particle_mesh_missing_resref_packet_count = particle_render_stats.mesh_missing_resref_packet_count;
    frame_stats.particle_mesh_missing_model_packet_count = particle_render_stats.mesh_missing_model_packet_count;
    frame_stats.particle_mesh_invalid_particle_index_count = particle_render_stats.mesh_invalid_particle_index_count;
    const auto debug_command_stats_start = command_stats_sample();
    const auto debug_start = Clock::now();
    const DebugShapeOptions debug_options{
        .enabled = area_debug_enabled_ || scene_kind_ != ViewerSceneKind::area,
        .triggers = area_triggers_enabled_ || scene_kind_ != ViewerSceneKind::area,
        .encounters = area_encounters_enabled_ || scene_kind_ != ViewerSceneKind::area,
    };
    {
        const ScopedGpuTimer gpu_timer{command_list, kGpuTimerDebug};
        if (debug_renderer_) {
            debug_renderer_->render_debug_shapes(command_list, *scene_, render_context, debug_options);
        }
    }
    const auto debug_end = Clock::now();
    frame_stats.debug_command_stats = command_stats_delta(command_stats_sample(), debug_command_stats_start);
    frame_stats.debug_seconds = elapsed_seconds(debug_start, debug_end);

    nw::gfx::cmd_end_render(command_list);
    const auto& batch_stats = prepared_draw_scratch_.batch_stats;
    frame_stats.static_batch_material_batch_count = batch_stats.material_batch_count;
    frame_stats.static_batch_material_input_draw_count = batch_stats.material_input_draw_count;
    frame_stats.static_batch_material_instance_count = batch_stats.material_instance_count;
    frame_stats.static_batch_material_draw_call_count = batch_stats.material_draw_call_count;
    frame_stats.static_batch_material_fallback_draw_count = batch_stats.material_fallback_draw_count;
    frame_stats.static_batch_material_failed_batch_attempt_draw_count = batch_stats.material_failed_batch_attempt_draw_count;
    frame_stats.static_batch_material_indirect_call_count = batch_stats.material_indirect_call_count;
    frame_stats.static_batch_material_indirect_command_count = batch_stats.material_indirect_command_count;
    frame_stats.static_batch_material_max_instances_per_draw = batch_stats.material_max_instances_per_draw;
    frame_stats.static_batch_material_indirect_command_upload_bytes = batch_stats.material_indirect_command_upload_bytes;
    frame_stats.static_batch_material_cached_indirect_command_bytes = batch_stats.material_cached_indirect_command_bytes;
    frame_stats.static_batch_material_draw_data_bytes = batch_stats.material_draw_data_bytes;
    frame_stats.static_batch_material_draw_data_cache_hit_count = batch_stats.material_draw_data_cache_hit_count;
    frame_stats.static_batch_material_cached_draw_data_bytes = batch_stats.material_cached_draw_data_bytes;
    frame_stats.static_batch_shadow_batch_count = batch_stats.shadow_batch_count;
    frame_stats.static_batch_shadow_input_draw_count = batch_stats.shadow_input_draw_count;
    frame_stats.static_batch_shadow_instance_count = batch_stats.shadow_instance_count;
    frame_stats.static_batch_shadow_draw_call_count = batch_stats.shadow_draw_call_count;
    frame_stats.static_batch_shadow_failed_batch_attempt_draw_count = batch_stats.shadow_failed_batch_attempt_draw_count;
    frame_stats.static_batch_shadow_indirect_call_count = batch_stats.shadow_indirect_call_count;
    frame_stats.static_batch_shadow_indirect_command_count = batch_stats.shadow_indirect_command_count;
    frame_stats.static_batch_shadow_max_instances_per_draw = batch_stats.shadow_max_instances_per_draw;
    frame_stats.static_batch_shadow_indirect_command_upload_bytes = batch_stats.shadow_indirect_command_upload_bytes;
    frame_stats.static_batch_shadow_cached_indirect_command_bytes = batch_stats.shadow_cached_indirect_command_bytes;
    frame_stats.static_batch_shadow_draw_data_bytes = batch_stats.shadow_draw_data_bytes;
    frame_stats.static_batch_shadow_draw_data_cache_hit_count = batch_stats.shadow_draw_data_cache_hit_count;
    frame_stats.static_batch_shadow_cached_draw_data_bytes = batch_stats.shadow_cached_draw_data_bytes;
    frame_stats.total_command_stats = command_stats_delta(command_stats_sample(), render_command_stats_start);
    frame_stats.total_render_seconds = elapsed_seconds(render_start, Clock::now());
    last_frame_stats_ = frame_stats;
}

bool ViewerSession::select_animation(std::string_view animation)
{
    return scene_
        && (scene_->load_animation(animation)
            || set_render_model_animation_clip_by_name(*scene_, animation, 0.0f));
}

void ViewerSession::set_area_day_night_elapsed_seconds(float elapsed_seconds, bool log_transition)
{
    if (!scene_ || !supports_area_day_night_cycle(*scene_)) {
        area_day_night_elapsed_seconds_ = 0.0f;
        return;
    }

    area_day_night_elapsed_seconds_ = normalize_area_day_night_elapsed_seconds(elapsed_seconds);
    const bool day_night_changed = sync_area_day_night_state(*scene_, area_day_night_elapsed_seconds_, log_transition);
    if (day_night_changed && preview_resources_ && scene_->area_render_scene) {
        scene_->area_render_scene->refresh_light_indices(*scene_);
        area_frame_.clear();
        area_frame_.reserve_for_scene(*scene_->area_render_scene);
    }
}

void ViewerSession::set_static_pbr_environment(
    float ibl_strength,
    float exposure,
    uint32_t ibl_diffuse_texture_index,
    uint32_t ibl_specular_texture_index,
    uint32_t brdf_lut_texture_index,
    bool enabled) noexcept
{
    static_pbr_ibl_strength_ = ibl_strength;
    static_pbr_exposure_ = exposure;
    static_pbr_ibl_diffuse_texture_index_ = ibl_diffuse_texture_index;
    static_pbr_ibl_specular_texture_index_ = ibl_specular_texture_index;
    static_pbr_brdf_lut_texture_index_ = brdf_lut_texture_index;
    static_pbr_ibl_enabled_ = enabled;
}

void ViewerSession::reset_area_day_night_cycle()
{
    if (!scene_ || !supports_area_day_night_cycle(*scene_)) {
        return;
    }
    set_area_day_night_elapsed_seconds(initial_area_day_night_elapsed_seconds(*scene_), false);
}

bool ViewerSession::fit_to_scene(ViewerViewport viewport)
{
    if (!scene_ || !viewport.valid()) {
        return false;
    }
    viewport_ = viewport;
    fit_loaded_scene();
    return true;
}

bool ViewerSession::set_area_gameplay_view(ViewerViewport viewport, float fov_degrees)
{
    if (!scene_ || scene_kind_ != ViewerSceneKind::area || !viewport.valid()) {
        return false;
    }

    viewport_ = viewport;
    update_viewport(viewport_);
    camera_.set_area_gameplay_view(render_bounds_for_scene(*scene_, scene_kind_), fov_degrees);
    return true;
}

void ViewerSession::set_area_visibility_mask(std::span<const uint8_t> visible_chunk_mask)
{
    area_visibility_mask_.assign(visible_chunk_mask.begin(), visible_chunk_mask.end());
    area_visibility_mask_enabled_ = !area_visibility_mask_.empty();
    area_visibility_mask_visible_chunk_count_ = static_cast<size_t>(
        std::count(area_visibility_mask_.begin(), area_visibility_mask_.end(), uint8_t{1u}));
    area_visibility_radius_tiles_ = -1;
    area_visibility_mask_mode_ = AreaVisibilityMaskMode::radius;
}

void ViewerSession::set_area_visibility_radius_tiles(int32_t radius_tiles)
{
    area_visibility_radius_tiles_ = std::max(radius_tiles, int32_t{0});
    area_visibility_mask_mode_ = AreaVisibilityMaskMode::radius;
    area_visibility_mask_enabled_ = false;
    area_visibility_mask_visible_chunk_count_ = 0;
}

void ViewerSession::set_area_visibility_view_cone_tiles(int32_t radius_tiles, float half_angle_degrees)
{
    area_visibility_radius_tiles_ = std::max(radius_tiles, int32_t{0});
    area_visibility_half_angle_degrees_ = std::clamp(half_angle_degrees, 1.0f, 179.0f);
    area_visibility_mask_mode_ = AreaVisibilityMaskMode::view_cone;
    area_visibility_mask_enabled_ = false;
    area_visibility_mask_visible_chunk_count_ = 0;
}

void ViewerSession::clear_area_visibility_mask()
{
    area_visibility_mask_.clear();
    area_visibility_mask_enabled_ = false;
    area_visibility_mask_visible_chunk_count_ = 0;
    area_visibility_radius_tiles_ = -1;
    area_visibility_half_angle_degrees_ = 75.0f;
    area_visibility_mask_mode_ = AreaVisibilityMaskMode::radius;
}

void ViewerSession::update_viewport(ViewerViewport viewport)
{
    if (!scene_ || !viewport.valid()) {
        return;
    }

    viewport_ = viewport;
    const float aspect = static_cast<float>(std::max(1u, viewport.width))
        / static_cast<float>(std::max(1u, viewport.height));
    const auto bounds = render_bounds_for_scene(*scene_, scene_kind_);
    const float radius = std::max(bounds.radius(), 1.0f);
    const float camera_distance = glm::length(camera_.get_position() - bounds.center());
    const float far_plane = std::max({1000.0f, radius * 8.0f, camera_distance + radius * 2.0f + 64.0f});
    camera_.set_aspect_ratio(aspect);
    camera_.set_near_far(0.1f, far_plane);
}

bool ViewerSession::set_scene(std::unique_ptr<PreviewScene> scene, ViewerSceneKind kind, std::string source)
{
    if (!scene) {
        return false;
    }

    if (scene_) {
        wait_for_preview_resources_idle(preview_resources_);
    }
    scene_ = std::move(scene);
    forward_plus_frame_.clear();
    area_frame_.clear();
    prepared_model_draws_.clear();
    prepared_model_surfaces_.clear();
    prepared_draw_scratch_.clear();
    clear_area_visibility_mask();
    scene_kind_ = kind;
    loaded_source_ = std::move(source);
    scene_time_seconds_ = 0.0f;
    area_day_night_elapsed_seconds_ = 0.0f;
    if (scene_ && supports_area_day_night_cycle(*scene_)) {
        set_area_day_night_elapsed_seconds(initial_area_day_night_elapsed_seconds(*scene_), false);
    }
    if (scene_ && scene_->area_render_scene) {
        area_frame_.reserve_for_scene(*scene_->area_render_scene);
        const auto& area_cache_stats = scene_->area_render_scene->stats();
        prepared_draw_scratch_.reserve(area_cache_stats.max_prepared_draws_per_record);
    }
    fit_loaded_scene();
    return true;
}

void ViewerSession::fit_loaded_scene()
{
    if (!scene_) {
        return;
    }

    if (viewport_.valid()) {
        update_viewport(viewport_);
    }
    const auto bounds = render_bounds_for_scene(*scene_, scene_kind_);
    camera_.fit_to_bounds(bounds);
    if (scene_kind_ == ViewerSceneKind::area) {
        camera_.set_free_view(camera_.get_position(), camera_.get_target(), Camera::ProjectionMode::perspective);
    }
}

nw::render::RenderContext ViewerSession::make_render_context() const
{
    nw::render::RenderContext result{};
    result.view = camera_.get_view_matrix();
    result.projection = camera_.get_projection_matrix();
    result.camera_position = camera_.get_position();
    result.camera_target = camera_.get_target();
    result.camera_near_plane = camera_.near_plane();
    result.camera_far_plane = camera_.far_plane();
    result.orthographic_camera = camera_.is_orthographic();
    result.time_seconds = scene_time_seconds_;
    result.static_pbr_ibl_strength = static_pbr_ibl_strength_;
    result.static_pbr_exposure = static_pbr_exposure_;
    result.static_pbr_ibl_diffuse_texture_index = static_pbr_ibl_diffuse_texture_index_;
    result.static_pbr_ibl_specular_texture_index = static_pbr_ibl_specular_texture_index_;
    result.static_pbr_brdf_lut_texture_index = static_pbr_brdf_lut_texture_index_;
    result.static_pbr_ibl_enabled = static_pbr_ibl_enabled_;

    if (scene_) {
        const bool area_inspection_daylight = !area_lights_enabled_ && scene_kind_ == ViewerSceneKind::area;
        const AreaLightingMode lighting_mode = area_inspection_daylight
            ? AreaLightingMode::inspection_daylight
            : AreaLightingMode::authored;
        result.lighting = resolve_preview_scene_lighting(*scene_, area_day_night_elapsed_seconds_, lighting_mode);
        result.lighting_space = resolve_preview_scene_lighting_space(*scene_, lighting_mode);
        result.fog = resolve_preview_scene_fog(*scene_, area_day_night_elapsed_seconds_, authored_area_fog_enabled_);
        result.environment = resolve_preview_scene_environment(*scene_, area_day_night_elapsed_seconds_);
        result.forward_plus_debug_mode = forward_plus_policy_.debug_mode;
        if (scene_forward_plus_lights_enabled(scene_kind_, area_lights_enabled_, forward_plus_policy_)) {
            result.local_lights = scene_->render_local_lights;
        }
    } else {
        result.lighting = studio_preview_lighting();
        result.lighting_space = studio_preview_lighting_space();
    }
    return result;
}

} // namespace nw::render::viewer
