#include "play_preview_view.hpp"
#include "renderer.hpp"
#include "runtime_input.hpp"
#include "shell_controller.hpp"

#include <fmt/format.h>

namespace nw::toolset {

void reset_play_preview_input(PlayPreviewState& preview, RuntimeInputState& input) noexcept
{
    preview.fixed_step = {};
    reset_runtime_pending_input(input);
}

void stop_play_preview(ClientRenderer& renderer, PlayPreviewState& preview,
    RuntimeInputState& input, ShellController& shell)
{
    if (preview.visuals_attached
        || preview.session.active()) {
        if (!renderer.end_toolset_preview_visuals()) {
            shell.append_output("error", "Failed to remove play-preview visuals");
        }
    }
    preview.visuals_attached = false;
    nw::toolset::stop_toolset_preview(preview.session);
    preview.pending_actor = {};
    preview.area = nw::ObjectHandle{};
    preview.module_generation = 0;
    preview.tab_id.clear();
    preview.placement_diagnostic.clear();
    reset_play_preview_input(preview, input);
}

PlayPreviewStartResult start_play_preview_from_ray(ClientRenderer& renderer,
    PlayPreviewState& preview, RuntimeInputState& input, ShellController& shell,
    const ClientViewportRay& ray)
{
    if (!preview.placement_pending()) return {};

    const nw::toolset::PreviewSessionStartInput start_input{
        .area = preview.area,
        .actor = preview.pending_actor,
        .spawn_ray = {
            .origin = ray.origin,
            .displacement = ray.displacement,
        },
        .camera = {.yaw = play_preview_yaw(ray)},
        .spawn_source = nw::toolset::PreviewSessionStartInput::SpawnSource::navigation_ray,
    };
    const auto started = nw::toolset::start_toolset_preview(
        preview.session, start_input);
    if (!started.ok()) {
        const std::string diagnostic = started.diagnostic.empty()
            ? "Play-preview startup failed"
            : started.diagnostic;
        preview.placement_diagnostic = diagnostic;
        return {.status = PlayPreviewStartStatus::spawn_failed, .message = diagnostic};
    }

    const std::array actors{started.actor};
    if (!renderer.begin_toolset_preview_visuals(
            actors,
            nw::toolset::toolset_preview_door_visual_states(
                preview.session),
            preview.session.camera())) {
        stop_play_preview(renderer, preview, input, shell);
        return {.status = PlayPreviewStartStatus::visuals_failed, .message = "Failed to attach play-preview actor visuals"};
    }

    preview.pending_actor = {};
    preview.placement_diagnostic.clear();
    preview.visuals_attached = true;
    reset_play_preview_input(preview, input);
    return {.status = PlayPreviewStartStatus::started, .message = started.stats.movement_disabled ? fmt::format("Play preview started with {} navigation polygons; this creature's authored movement rate is NOMOVE", started.stats.navigation_polygon_count) : fmt::format("Play preview started with {} navigation polygons", started.stats.navigation_polygon_count)};
}

void arm_play_preview(PlayPreviewState& preview, RuntimeInputState& input, Resource actor,
    ObjectHandle area, uint64_t module_generation, std::string_view tab_id)
{
    preview.pending_actor = actor;
    preview.area = area;
    preview.module_generation = module_generation;
    preview.tab_id = tab_id;
    preview.placement_diagnostic.clear();
    reset_play_preview_input(preview, input);
}

PreviewStatus update_play_preview_frame(ClientRenderer& renderer, PlayPreviewState& preview,
    RuntimeInputState& input, ShellController& shell, double frame_seconds, PcSourceEligibility eligibility)
{
    if (!preview.session.active()) { return PreviewStatus::idle; }
    PcDeviceSample physical;
    auto input_status = acquire_pc_device_sample(input,
        frame_seconds, eligibility, physical);
    PreviewInputSample frame_sample;
    if (input_status == PreviewStatus::ok) {
        input_status = translate_pc_input_samples({&physical, 1}, {&frame_sample, 1});
    }
    const auto fixed_stats = input_status == PreviewStatus::ok
        ? build_preview_tick_samples(preview.fixed_step,
              frame_seconds, frame_sample, preview.tick_inputs)
        : PreviewFixedStepStats{.status = input_status};
    if (fixed_stats.status != PreviewStatus::ok) {
        shell.append_output("error",
            "Play-preview input sampling failed");
        stop_play_preview(renderer, preview, input, shell);
        return PreviewStatus::invalid_input;
    } else if (fixed_stats.tick_count > 0) {
        const auto tick_stats = tick_toolset_preview(
            preview.session,
            std::span{preview.tick_inputs}.first(
                fixed_stats.tick_count),
            preview.spatial_rows,
            preview.locomotion_rows);
        const bool tick_ok
            = tick_stats.status == PreviewStatus::ok;
        const bool visual_ok = tick_ok
            && renderer.update_toolset_preview_visuals(
                std::span{preview.spatial_rows}.first(
                    tick_stats.output_count),
                std::span{preview.locomotion_rows}.first(
                    tick_stats.output_count),
                toolset_preview_door_visual_states(
                    preview.session),
                preview.session.camera());
        const auto navigation_debug
            = toolset_preview_navigation_debug(
                preview.session);
        const bool navigation_debug_ok = !navigation_debug.enabled
            || renderer.update_toolset_preview_navigation_debug(
                navigation_debug);
        consume_runtime_input_edges(input);
        if (!tick_ok || !visual_ok || !navigation_debug_ok) {
            shell.append_output("error",
                !tick_ok         ? "Play-preview simulation failed"
                    : !visual_ok ? "Failed to update play-preview visuals"
                                 : "Failed to update navigation debug geometry");
            stop_play_preview(renderer, preview, input, shell);
            return PreviewStatus::invalid_input;
        }
    }
    return PreviewStatus::ok;
}

void apply_play_preview_pointer_action(ClientRenderer& renderer, PlayPreviewState& preview,
    RuntimeInputState& input, glm::vec2 point, ClientViewportRect viewport)
{
    clear_preview_pointer_action(
        input.pending);
    const auto door_hit
        = acquire_runtime_door_hit(renderer,
            point,
            viewport,
            toolset_preview_door_handles(
                preview.session));
    const auto door_states
        = toolset_preview_door_visual_states(
            preview.session);
    const bool door_requires_interaction = door_hit
        && preview_door_requires_interaction(
            door_states, door_hit->door_index);
    PcPointerAction action;
    if (door_requires_interaction) {
        action.door_interactable = true;
        action.door_index = door_hit->door_index;
        action.door_bounds_min = door_hit->bounds_min;
        action.door_bounds_max = door_hit->bounds_max;
    } else if (const auto ray
        = acquire_runtime_viewport_ray(renderer,
            point,
            viewport)) {
        const std::array projection_inputs{
            nw::nav::NavRayProjectionInput{
                .origin = ray->origin,
                .displacement = ray->displacement,
            },
        };
        std::array<nw::nav::NavRayProjectionResult, 1> projected{};
        project_toolset_preview_rays(
            preview.session,
            projection_inputs,
            projected);
        if (projected[0].status == nw::nav::NavStatus::ok) {
            action.navigation_projected = true;
            action.navigation_position = projected[0].position;
        }
    }
    (void)apply_pc_pointer_actions({&action, 1}, {&input.pending, 1});
}

void toggle_play_preview_navigation_debug(ClientRenderer& renderer, PlayPreviewState& preview, ShellController& shell)
{
    const bool enabled
        = !toolset_preview_navigation_debug(
            preview.session)
               .enabled;
    const auto status
        = set_toolset_preview_navigation_debug(
            preview.session, enabled);
    const bool render_ok = status == PreviewStatus::ok
        && renderer.update_toolset_preview_navigation_debug(
            toolset_preview_navigation_debug(
                preview.session));
    shell.append_output(
        render_ok ? "info" : "error",
        render_ok
            ? (enabled
                      ? "Navigation debug enabled"
                      : "Navigation debug disabled")
            : "Failed to update navigation debug geometry");
}

const char* play_preview_pointer_cursor(ClientRenderer& renderer, const PlayPreviewState& preview,
    glm::vec2 point, ClientViewportRect viewport)
{
    const auto door_hit = preview.session.active()
        ? acquire_runtime_door_hit(renderer, point, viewport, toolset_preview_door_handles(preview.session))
        : std::nullopt;
    const bool interactable = door_hit && preview_door_requires_interaction(toolset_preview_door_visual_states(preview.session), door_hit->door_index);
    return preview.placement_pending() ? "cross" : interactable ? "pointer"
                                                                : "arrow";
}

} // namespace nw::toolset
