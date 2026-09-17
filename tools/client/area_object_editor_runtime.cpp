#include "area_object_editor.hpp"
#include "area_regions.hpp"
#include "command_view.hpp"
#include "object_edits.hpp"
#include "preview_session.hpp"
#include "renderer.hpp"
#include "shell_controller.hpp"
#include "toolset_backend.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace nw::toolset {
namespace {

constexpr float kWorkspaceTabDragThresholdPx = 5.0f;
constexpr float kAreaObjectPlacementOpacity = 0.45f;

bool validate_placement_preview(ClientRenderer& renderer,
    std::unique_ptr<nw::toolset::AreaPlacementNavigation>& navigation,
    nw::ObjectHandle area, const nw::ObjectSpatialState& spatial,
    std::string& diagnostic)
{
    const std::array rows{spatial};
    nw::toolset::AreaPlacementNavigation uncached;
    if (spatial.owner.type == nw::ObjectType::creature && !navigation) {
        try {
            navigation = std::make_unique<nw::toolset::AreaPlacementNavigation>();
        } catch (const std::bad_alloc&) {
            diagnostic = "Creature placement navigation allocation failed";
            return false;
        }
    }
    auto& snapshot = navigation ? *navigation : uncached;
    const auto result = nw::toolset::validate_area_placements(snapshot, area, rows);
    diagnostic = result.diagnostic;
    if (navigation) {
        const bool enabled = result.status != nw::nav::NavStatus::rejected
            && nw::toolset::collect_placement_navigation_debug(snapshot);
        if (!renderer.update_toolset_preview_navigation_debug({
                .triangles = snapshot.debug_triangles,
                .revision = snapshot.revision,
                .enabled = enabled,
            })) {
            LOG_F(WARNING, "Creature placement navigation overlay update failed");
        }
    }
    return result.ok();
}

bool project_area_navigation_point(
    ClientRenderer& renderer,
    std::unique_ptr<nw::toolset::AreaPlacementNavigation>& navigation,
    nw::ObjectHandle area,
    Rml::Vector2f point,
    ClientViewportRect viewport,
    glm::vec3& output,
    std::string& diagnostic)
{
    const auto ray = renderer.viewer_viewport_ray(
        point.x, point.y, viewport);
    if (!ray) {
        diagnostic = "Navigation ray could not be constructed";
        return false;
    }
    if (!navigation) {
        try {
            navigation
                = std::make_unique<nw::toolset::AreaPlacementNavigation>();
        } catch (const std::bad_alloc&) {
            diagnostic = "Area navigation allocation failed";
            return false;
        }
    }
    const std::array inputs{nw::nav::NavRayProjectionInput{
        .origin = ray->origin,
        .displacement = ray->displacement,
    }};
    std::array<nw::nav::NavRayProjectionResult, 1> projected{};
    const auto stats = nw::toolset::project_area_navigation_rays(
        *navigation, area, inputs, projected, diagnostic);
    if (stats.output_count != 1
        || projected[0].status != nw::nav::NavStatus::ok) {
        return false;
    }
    output = projected[0].position;
    return true;
}

bool update_area_region_placement(
    ClientRenderer& renderer,
    AreaObjectPlacementState& placement,
    nw::ObjectHandle area,
    Rml::Vector2f point,
    ClientViewportRect viewport)
{
    glm::vec3 projected{0.0f};
    if (!project_area_navigation_point(renderer,
            placement.navigation, area, point, viewport,
            projected, placement.diagnostic)) {
        placement.region_hover.reset();
        placement.region_closing_valid = false;
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        renderer.update_viewer_area_region_preview(
            placement.region_points, std::nullopt, false);
        return true;
    }

    const auto* live_area = nw::kernel::objects().get<nw::Area>(area);
    std::vector<glm::vec3> candidate = placement.region_points;
    candidate.push_back(projected);
    std::string path_diagnostic;
    const bool open_valid = live_area
        && nw::toolset::validate_area_region_path(
            live_area->width, live_area->height,
            candidate, false, path_diagnostic);
    std::string close_diagnostic;
    placement.region_closing_valid = open_valid && candidate.size() >= 3
        && nw::toolset::validate_area_region_path(
            live_area->width, live_area->height,
            candidate, true, close_diagnostic);
    placement.region_hover = projected;
    placement.area = area;
    placement.phase = open_valid
        ? AreaObjectPlacementPhase::ghost_valid
        : AreaObjectPlacementPhase::ghost_invalid;
    placement.diagnostic = open_valid
        ? close_diagnostic
        : path_diagnostic;
    if (!renderer.update_viewer_area_region_preview(
            placement.region_points,
            placement.region_hover,
            placement.region_closing_valid)) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        placement.diagnostic = "Region preview construction failed";
    }
    return true;
}

bool delete_selected_encounter_spawn_point(ClientRenderer& renderer, ToolsetBackend& backend, const CommandContext& context, ShellController& shell)
{
    const auto encounter = renderer.active_viewer_object();
    const auto area = renderer.area_viewer_object();
    const uint32_t spawn_index
        = renderer.active_viewer_area_debug_subindex(encounter);
    const auto* geometry
        = nw::kernel::objects().components().find_geometry(encounter);
    if (encounter.type != nw::ObjectType::encounter
        || area.type != nw::ObjectType::area
        || !geometry || spawn_index >= geometry->spawn_points.size()) {
        return false;
    }
    std::vector<nw::ObjectSpawnPoint> before{
        geometry->spawn_points.begin(),
        geometry->spawn_points.end(),
    };
    auto after = before;
    after.erase(after.begin() + static_cast<ptrdiff_t>(spawn_index));
    const auto result = backend.replace_encounter_spawn_points(
        {
            .area = area,
            .encounter = encounter,
            .before = std::move(before),
            .after = std::move(after),
        },
        context);
    append_command_results(shell, {&result, 1});
    return true;
}

} // namespace

void sync_area_object_after_command(ClientRenderer& renderer, ShellController& shell, const CommandResult& result, ObjectHandle active_object)
{
    append_command_results(shell, {&result, 1});
    const auto object = active_object;
    if (editable_area_object(object)) {
        renderer.sync_viewer_area_object_spatial(object);
    }
}

bool begin_area_object_drag(ClientRenderer& renderer, AreaObjectDragState& drag, Rml::Vector2f point, ClientViewportRect viewport)
{
    const nw::ObjectHandle object = renderer.active_viewer_object();
    if (!editable_area_object(object)) {
        return false;
    }

    const auto* spatial = nw::kernel::objects().components().find_spatial(object);
    if (!spatial) {
        return false;
    }
    const auto surface_point = renderer.viewer_area_surface_point(
        point.x, point.y, viewport);
    if (!surface_point) {
        return false;
    }

    const uint32_t spawn_index
        = renderer.active_viewer_area_debug_subindex(object);
    const auto* geometry = object.type == nw::ObjectType::encounter
        ? nw::kernel::objects().components().find_geometry(object)
        : nullptr;
    const bool encounter_spawn = geometry
        && spawn_index < geometry->spawn_points.size();
    const nw::ObjectSpawnPoint spawn = encounter_spawn
        ? geometry->spawn_points[spawn_index]
        : nw::ObjectSpawnPoint{};
    drag = {
        .area = renderer.area_viewer_object(),
        .before = *spatial,
        .preview = *spatial,
        .grab_offset = spatial->position - *surface_point,
        .pointer = {viewport, {point.x, point.y}},
        .active = true,
        .valid = true,
        .encounter_spawn = encounter_spawn,
        .encounter_spawn_index = spawn_index,
        .spawn_before = spawn,
        .spawn_preview = spawn,
    };
    return true;
}

bool update_area_object_drag(ClientRenderer& renderer, AreaObjectDragState& drag, Rml::Vector2f point, ClientViewportRect viewport)
{
    if (!drag.active) {
        return false;
    }
    if (renderer.area_viewer_object() != drag.area) {
        cancel_area_object_drag(renderer, drag);
        return false;
    }
    const auto pointer_status = update_viewport_pointer_drag(
        drag.pointer, {point.x, point.y}, viewport);
    if (pointer_status == ClientViewportPointerDragStatus::cancelled) {
        cancel_area_object_drag(renderer, drag);
        return false;
    }
    if (pointer_status == ClientViewportPointerDragStatus::pending) return false;
    if (drag.encounter_spawn) {
        glm::vec3 position{0.0f};
        drag.valid = project_area_navigation_point(renderer,
            drag.navigation, drag.area, point, viewport,
            position, drag.diagnostic);
        if (!drag.valid) return false;
        drag.spawn_preview.position = position;
        drag.moved = drag.spawn_preview != drag.spawn_before;
        renderer.update_viewer_area_region_preview(
            {}, drag.spawn_preview.position, true);
        return true;
    }

    const auto surface_point = renderer.viewer_area_surface_point(
        point.x, point.y, viewport);
    if (!surface_point) {
        drag.valid = false;
        drag.diagnostic = "Placement ray did not hit an area surface";
        return false;
    }

    const glm::vec3 position = *surface_point + drag.grab_offset;
    drag.preview.position = position;
    const bool snapped = snap_area_door_preview(
        drag.area,
        drag.before.owner,
        position,
        drag.door_hooks,
        drag.door_hook_type,
        drag.preview,
        drag.diagnostic);
    drag.moved = drag.preview.position != drag.before.position
        || drag.preview.orientation != drag.before.orientation;
    drag.valid = snapped && validate_placement_preview(renderer, drag.navigation, drag.area, drag.preview, drag.diagnostic);
    if (snapped) {
        renderer.preview_viewer_area_object_spatial(drag.preview);
    }
    return true;
}

void cancel_area_object_drag(ClientRenderer& renderer, AreaObjectDragState& drag)
{
    if (!drag.active) {
        return;
    }
    if (drag.navigation) {
        renderer.update_toolset_preview_navigation_debug({});
    }
    if (drag.encounter_spawn) {
        renderer.update_viewer_area_region_preview(
            {}, std::nullopt, false);
    }
    if (drag.pointer.dragging) {
        renderer.sync_viewer_area_object_spatial(drag.before.owner);
    }
    drag = {};
}

void commit_area_object_drag(ClientRenderer& renderer, AreaObjectDragState& drag_state, ObjectHandle active_object, ToolsetBackend& backend, const CommandContext& context, ShellController& shell)
{
    if (!drag_state.active) {
        return;
    }

    const auto drag = std::move(drag_state);
    drag_state = {};
    if (drag.navigation) renderer.update_toolset_preview_navigation_debug({});
    if (drag.encounter_spawn) {
        renderer.update_viewer_area_region_preview(
            {}, std::nullopt, false);
    }
    if (!drag.pointer.dragging) return;
    if (!drag.moved || active_object != drag.before.owner) {
        renderer.sync_viewer_area_object_spatial(drag.before.owner);
        return;
    }
    if (!drag.valid) {
        if (!drag.encounter_spawn) {
            renderer.sync_viewer_area_object_spatial(drag.before.owner);
        }
        shell.append_output("warn", drag.diagnostic);
        return;
    }

    if (drag.encounter_spawn) {
        const auto* geometry
            = nw::kernel::objects().components().find_geometry(
                drag.before.owner);
        if (!geometry
            || drag.encounter_spawn_index
                >= geometry->spawn_points.size()) {
            shell.append_output("warn",
                "Encounter spawn point changed during the drag");
            return;
        }
        std::vector<nw::ObjectSpawnPoint> before{
            geometry->spawn_points.begin(),
            geometry->spawn_points.end(),
        };
        auto after = before;
        after[drag.encounter_spawn_index] = drag.spawn_preview;
        const auto result = backend.replace_encounter_spawn_points(
            {
                .area = drag.area,
                .encounter = drag.before.owner,
                .before = std::move(before),
                .after = std::move(after),
            },
            context);
        append_command_results(shell, {&result, 1});
        return;
    }

    const auto result = backend.transform_area_object(
        {
            .object = drag.before.owner,
            .before = {
                .position = drag.before.position,
                .orientation = drag.before.orientation,
                .scale = drag.before.scale,
            },
            .after = {
                .position = drag.preview.position,
                .orientation = drag.preview.orientation,
                .scale = drag.preview.scale,
            },
            .area = drag.area,
        },
        context);
    sync_area_object_after_command(renderer, shell, result, active_object);
}

bool add_encounter_spawn_point(ClientRenderer& renderer, Rml::Vector2f point, ClientViewportRect viewport, ToolsetBackend& backend, const CommandContext& context, ShellController& shell)
{
    const auto encounter = renderer.active_viewer_object();
    const auto area = renderer.area_viewer_object();
    if (encounter.type != nw::ObjectType::encounter
        || area.type != nw::ObjectType::area) {
        return false;
    }
    std::unique_ptr<nw::toolset::AreaPlacementNavigation> navigation;
    glm::vec3 position{0.0f};
    std::string diagnostic;
    if (!project_area_navigation_point(renderer, navigation, area,
            point, viewport, position, diagnostic)) {
        shell.append_output("warn", diagnostic);
        return true;
    }
    const auto* geometry
        = nw::kernel::objects().components().find_geometry(encounter);
    std::vector<nw::ObjectSpawnPoint> before;
    if (geometry) {
        before.assign(
            geometry->spawn_points.begin(),
            geometry->spawn_points.end());
    }
    auto after = before;
    after.push_back({
        .position = position,
        .orientation = 0.0f,
    });
    const auto result = backend.replace_encounter_spawn_points(
        {
            .area = area,
            .encounter = encounter,
            .before = std::move(before),
            .after = std::move(after),
        },
        context);
    append_command_results(shell, {&result, 1});
    return true;
}

void arm_area_object_placement(ClientRenderer& renderer, AreaObjectPlacementState& placement, Resource resource, Rml::Vector2f point, std::string_view active_tab_id)
{
    if (!placement_blueprint_resource(resource)) {
        return;
    }

    const auto previous_selection = renderer.active_viewer_object();
    placement = {
        .resource = std::move(resource),
        .previous_selection = previous_selection,
        .drag_start = point,
        .tab_id = std::string{active_tab_id},
        .phase = AreaObjectPlacementPhase::armed,
    };
}

bool handle_area_object_edit_key(ClientRenderer& renderer, AreaObjectEditKey key,
    ToolsetBackend& backend, const CommandContext& context,
    ShellController& shell, ObjectHandle active_object)
{
    if (!editable_area_object(renderer.active_viewer_object())) { return false; }
    switch (key) {
    case AreaObjectEditKey::remove: {
        if (delete_selected_encounter_spawn_point(renderer, backend, context, shell)) { return true; }
        const auto result = backend.execute_command("area.object.delete", {}, context);
        append_command_results(shell, {&result, 1});
        return true;
    }
    case AreaObjectEditKey::randomize_orientation: {
        const auto result = backend.execute_command("object.transform.randomize_orientation", {}, context);
        sync_area_object_after_command(renderer, shell, result, active_object);
        return true;
    }
    default:
        return false;
    }
}

void cancel_area_object_placement(ClientRenderer& renderer, AreaObjectPlacementState& placement_state, ShellController& shell)
{
    if (!placement_state.active()) {
        return;
    }

    const auto placement = std::move(placement_state);
    placement_state = {};
    if (region_blueprint_resource(placement.resource)) {
        renderer.update_viewer_area_region_preview(
            {}, std::nullopt, false);
    }
    if (placement.navigation) renderer.update_toolset_preview_navigation_debug({});
    if (nw::kernel::objects().valid(placement.object)) {
        nw::kernel::objects().destroy(placement.object);
    }
    if (placement.object.type != nw::ObjectType::invalid
        && renderer.area_viewer_object() == placement.area) {
        const auto selected = nw::kernel::objects().valid(placement.previous_selection)
            ? placement.previous_selection
            : nw::ObjectHandle{};
        if (!renderer.rebuild_live_viewer_area(placement.area, selected)) {
            shell.append_output("error", "Area object placement cancellation rebuild failed");
        }
    }
}

bool update_area_object_placement(ClientRenderer& renderer, AreaObjectPlacementState& placement, Rml::Vector2f point, const std::optional<ClientViewportRect>& viewport, std::string_view active_tab_id, int& pressed_recent_index, ShellController& shell)
{
    if (!placement.active()) {
        return false;
    }

    const float dx = point.x - placement.drag_start.x;
    const float dy = point.y - placement.drag_start.y;
    if (!placement.threshold_crossed
        && std::abs(dx) < kWorkspaceTabDragThresholdPx
        && std::abs(dy) < kWorkspaceTabDragThresholdPx) {
        return false;
    }
    placement.threshold_crossed = true;
    pressed_recent_index = -1;

    if (!viewport || !viewport->contains_point(point.x, point.y)) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        if (region_blueprint_resource(placement.resource)) {
            placement.region_hover.reset();
            placement.region_closing_valid = false;
            renderer.update_viewer_area_region_preview(
                placement.region_points, std::nullopt, false);
        }
        return true;
    }

    const auto area = renderer.area_viewer_object();
    if (area.type != nw::ObjectType::area || !nw::kernel::objects().valid(area)
        || active_tab_id != placement.tab_id) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        placement.region_hover.reset();
        placement.region_closing_valid = false;
        return true;
    }
    if (placement.area.type != nw::ObjectType::invalid && placement.area != area) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        placement.region_hover.reset();
        placement.region_closing_valid = false;
        return true;
    }

    if (region_blueprint_resource(placement.resource)
        && placement.object.type == nw::ObjectType::invalid) {
        return update_area_region_placement(
            renderer, placement, area, point, *viewport);
    }

    const auto surface_point = renderer.viewer_area_surface_point(
        point.x, point.y, *viewport);
    if (!surface_point) {
        placement.phase = AreaObjectPlacementPhase::ghost_invalid;
        placement.diagnostic = "Placement ray did not hit an area surface";
        return true;
    }
    const bool valid_position = area_object_placement_position_valid(area, *surface_point);
    if (placement.object.type == nw::ObjectType::invalid) {
        if (!valid_position || placement.materialization_failed) {
            placement.phase = AreaObjectPlacementPhase::ghost_invalid;
            return true;
        }

        const std::array placement_rows{nw::toolset::AreaObjectBlueprintPlacement{
            .resource = placement.resource,
            .transform = {
                .position = *surface_point,
                .orientation = {1.0f, 0.0f, 0.0f},
                .scale = {1.0f, 1.0f, 1.0f},
            },
        }};
        auto loaded = nw::toolset::load_area_object_blueprints(area, placement_rows);
        if (!loaded.ok() || loaded.objects.size() != 1) {
            placement.materialization_failed = true;
            placement.phase = AreaObjectPlacementPhase::ghost_invalid;
            shell.append_output(
                loaded.status == nw::toolset::AreaObjectBlueprintLoadStatus::failed ? "error" : "warn",
                loaded.diagnostic.empty() ? "Area object placement load failed" : loaded.diagnostic);
            return true;
        }

        placement.area = area;
        placement.object = loaded.objects.front();
        const std::array objects{placement.object};
        if (!renderer.append_viewer_area_object_previews(objects, kAreaObjectPlacementOpacity)) {
            nw::kernel::objects().destroy(placement.object);
            placement.object = nw::ObjectHandle{};
            placement.materialization_failed = true;
            placement.phase = AreaObjectPlacementPhase::ghost_invalid;
            shell.append_output("error", "Area object placement preview construction failed");
            return true;
        }
        const auto* spatial = nw::kernel::objects().components().find_spatial(placement.object);
        if (!spatial) {
            cancel_area_object_placement(renderer, placement, shell);
            return true;
        }
        placement.preview = *spatial;
    }

    placement.preview.position = *surface_point;
    const bool snapped = snap_area_door_preview(
        area,
        placement.object,
        *surface_point,
        placement.door_hooks,
        placement.door_hook_type,
        placement.preview,
        placement.diagnostic);
    if (snapped && !renderer.preview_viewer_area_object_spatial(placement.preview)) {
        shell.append_output("error", "Area object placement preview update failed");
        cancel_area_object_placement(renderer, placement, shell);
        return true;
    }
    const bool admitted = snapped && validate_placement_preview(renderer, placement.navigation, area, placement.preview, placement.diagnostic);
    placement.phase = valid_position && snapped && admitted
        ? AreaObjectPlacementPhase::ghost_valid
        : AreaObjectPlacementPhase::ghost_invalid;
    return true;
}

bool accept_area_region_point(ClientRenderer& renderer, AreaObjectPlacementState& placement, Rml::Vector2f point, ClientViewportRect viewport, ShellController& shell)
{
    if (!placement.region_drawing
        || !update_area_region_placement(
            renderer, placement, placement.area, point, viewport)
        || placement.phase != AreaObjectPlacementPhase::ghost_valid
        || !placement.region_hover) {
        if (!placement.diagnostic.empty()) {
            shell.append_output("warn", placement.diagnostic);
        }
        return false;
    }
    placement.region_points.push_back(*placement.region_hover);
    placement.region_hover.reset();
    placement.region_closing_valid = false;
    placement.diagnostic.clear();
    renderer.update_viewer_area_region_preview(
        placement.region_points, std::nullopt, false);
    return true;
}

bool complete_area_region_placement(ClientRenderer& renderer, AreaObjectPlacementState& placement, Rml::Vector2f point, ClientViewportRect viewport, ToolsetBackend& backend, const CommandContext& context, ShellController& shell)
{
    if (!placement.region_drawing
        || !update_area_region_placement(
            renderer, placement, placement.area, point, viewport)
        || !placement.region_hover) {
        return false;
    }

    std::vector<glm::vec3> world_points = placement.region_points;
    const glm::vec3 final_delta = world_points.empty()
        ? glm::vec3{1.0f}
        : world_points.back() - *placement.region_hover;
    if (world_points.empty()
        || glm::dot(final_delta, final_delta) > 1.0e-8f) {
        world_points.push_back(*placement.region_hover);
    }
    const auto* area = nw::kernel::objects().get<nw::Area>(placement.area);
    nw::toolset::AreaRegionGeometry geometry;
    if (!area || !nw::toolset::build_area_region_geometry(area->width, area->height, world_points, geometry, placement.diagnostic)) {
        shell.append_output("warn", placement.diagnostic);
        return false;
    }

    const std::array placement_rows{
        nw::toolset::AreaObjectBlueprintPlacement{
            .resource = placement.resource,
            .transform = {
                .position = geometry.root_position,
                .orientation = {1.0f, 0.0f, 0.0f},
                .scale = {1.0f, 1.0f, 1.0f},
            },
            .geometry_points = geometry.local_points,
        },
    };
    auto loaded = nw::toolset::load_area_object_blueprints(
        placement.area, placement_rows);
    if (!loaded.ok() || loaded.objects.size() != 1) {
        placement.diagnostic = loaded.diagnostic.empty()
            ? "Area region placement load failed"
            : std::move(loaded.diagnostic);
        shell.append_output(
            loaded.status
                    == nw::toolset::AreaObjectBlueprintLoadStatus::failed
                ? "error"
                : "warn",
            placement.diagnostic);
        return false;
    }
    placement.object = loaded.objects.front();
    const auto* spatial = nw::kernel::objects().components().find_spatial(
        placement.object);
    if (!spatial) {
        nw::kernel::objects().destroy(placement.object);
        placement.object = nw::ObjectHandle{};
        shell.append_output("error",
            "Area region placement has no spatial state");
        return false;
    }
    placement.preview = *spatial;
    placement.phase = AreaObjectPlacementPhase::ghost_valid;
    renderer.update_viewer_area_region_preview({}, std::nullopt, false);
    const std::array objects{placement.object};
    if (!renderer.append_viewer_area_object_previews(
            objects, kAreaObjectPlacementOpacity)) {
        nw::kernel::objects().destroy(placement.object);
        placement.object = nw::ObjectHandle{};
        shell.append_output("error",
            "Area region preview construction failed");
        return false;
    }
    commit_area_object_placement(renderer, placement, backend, context, shell);
    return true;
}

void commit_area_object_placement(ClientRenderer& renderer, AreaObjectPlacementState& placement_state, ToolsetBackend& backend, const CommandContext& context, ShellController& shell)
{
    if (!placement_state.active()) {
        return;
    }
    if (placement_state.phase != AreaObjectPlacementPhase::ghost_valid
        || !nw::kernel::objects().valid(placement_state.object)) {
        if (!placement_state.diagnostic.empty()) {
            shell.append_output("warn", placement_state.diagnostic);
        }
        cancel_area_object_placement(renderer, placement_state, shell);
        return;
    }

    const auto placement = std::move(placement_state);
    placement_state = {};
    if (placement.navigation) renderer.update_toolset_preview_navigation_debug({});
    auto& components = nw::kernel::objects().components();
    const auto* current_spatial = components.find_spatial(placement.object);
    const nw::ObjectSpatialState before = current_spatial
        ? *current_spatial
        : nw::ObjectSpatialState{};
    if (!current_spatial
        || !components.set_position(placement.object, placement.preview.position)
        || !components.set_orientation(
            placement.object, placement.preview.orientation)) {
        if (current_spatial) {
            components.set_position(placement.object, before.position);
            components.set_orientation(placement.object, before.orientation);
        }
        if (nw::kernel::objects().valid(placement.object)) {
            nw::kernel::objects().destroy(placement.object);
        }
        const auto selected = nw::kernel::objects().valid(placement.previous_selection)
            ? placement.previous_selection
            : nw::ObjectHandle{};
        if (!renderer.rebuild_live_viewer_area(placement.area, selected)) {
            shell.append_output("error", "Area object placement rollback rebuild failed");
        }
        shell.append_output("error", "Area object placement spatial commit failed");
        return;
    }

    const std::array objects{placement.object};
    auto result = backend.place_area_objects(
        placement.area,
        objects,
        context);
    append_command_results(shell, {&result, 1});
    if (!result.ok()) {
        if (nw::kernel::objects().valid(placement.object)) {
            nw::kernel::objects().destroy(placement.object);
        }
        const auto selected = nw::kernel::objects().valid(placement.previous_selection)
            ? placement.previous_selection
            : nw::ObjectHandle{};
        if (!renderer.rebuild_live_viewer_area(placement.area, selected)) {
            shell.append_output("error", "Area object placement rollback rebuild failed");
        }
    }
}

} // namespace nw::toolset
