#pragma once

#include "area_door_hooks.hpp"
#include "area_navigation.hpp"
#include "viewport_pointer_drag.hpp"
#include "viewport_rect.hpp"

#include <RmlUi/Core/Types.h>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/resources/assets.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ClientRenderer;

namespace nw::toolset {

class ShellController;
class ToolsetBackend;
struct CommandContext;
struct CommandResult;

// One displayed drag/placement owns its saved spatial rows and cached door/nav
// snapshots. A placement owns its temporary ObjectManager root until commit
// transfers it to the area or cancel destroys it after removing previews.
// Renderer/kernel borrows are synchronous and never stored as raw pointers.
struct AreaObjectDragState {
    nw::ObjectHandle area{};
    nw::ObjectSpatialState before;
    nw::ObjectSpatialState preview;
    glm::vec3 grab_offset{0.0f};
    ClientViewportPointerDrag pointer;
    bool active = false;
    bool moved = false;
    bool valid = false;
    bool encounter_spawn = false;
    uint32_t encounter_spawn_index = UINT32_MAX;
    nw::ObjectSpawnPoint spawn_before;
    nw::ObjectSpawnPoint spawn_preview;
    std::unique_ptr<AreaDoorHookSnapshot> door_hooks;
    int32_t door_hook_type = -2;
    std::unique_ptr<AreaPlacementNavigation> navigation;
    std::string diagnostic;
};

enum class AreaObjectPlacementPhase : uint8_t {
    idle,
    armed,
    ghost_valid,
    ghost_invalid,
};

struct AreaObjectPlacementState {
    nw::Resource resource;
    nw::ObjectHandle area{};
    nw::ObjectHandle object{};
    nw::ObjectHandle previous_selection{};
    nw::ObjectSpatialState preview;
    Rml::Vector2f drag_start;
    std::string tab_id;
    AreaObjectPlacementPhase phase = AreaObjectPlacementPhase::idle;
    bool threshold_crossed = false;
    bool materialization_failed = false;
    bool region_drawing = false;
    bool region_closing_valid = false;
    std::vector<glm::vec3> region_points;
    std::optional<glm::vec3> region_hover;
    std::unique_ptr<AreaDoorHookSnapshot> door_hooks;
    int32_t door_hook_type = -2;
    std::unique_ptr<AreaPlacementNavigation> navigation;
    std::string diagnostic;

    [[nodiscard]] bool active() const noexcept
    {
        return phase != AreaObjectPlacementPhase::idle;
    }
};

[[nodiscard]] bool editable_area_object(ObjectHandle object) noexcept;
[[nodiscard]] bool placement_blueprint_resource(const Resource& resource) noexcept;
[[nodiscard]] bool region_blueprint_resource(const Resource& resource) noexcept;
[[nodiscard]] bool area_object_placement_position_valid(ObjectHandle area, glm::vec3 position);
[[nodiscard]] bool snap_area_door_preview(ObjectHandle area, ObjectHandle door,
    glm::vec3 requested_position, std::unique_ptr<AreaDoorHookSnapshot>& hooks,
    int32_t& hook_type, ObjectSpatialState& preview, std::string& diagnostic);

enum class AreaObjectEditKey : uint8_t {
    remove,
    randomize_orientation,
};

// Unknown keys produce no action. Eligibility/focus/repeat gates belong to the
// input adapter; these two native commands do not change workbench identity.
bool handle_area_object_edit_key(ClientRenderer& renderer, AreaObjectEditKey key,
    ToolsetBackend& backend, const CommandContext& context,
    ShellController& shell, ObjectHandle active_object);

// The caller supplies current tab/selection facts and a current area viewport.
// Invalid/stale hits cancel or reject; dragging pins the press viewport. Source
// row press state is cleared only after the placement crosses its 5 px threshold.
// Region/spawn vector allocation errors propagate before command commit, as in
// the existing main-thread gesture path; they do not fall back to another edit.
void sync_area_object_after_command(ClientRenderer& renderer, ShellController& shell, const CommandResult& result, ObjectHandle active_object);
bool begin_area_object_drag(ClientRenderer& renderer, AreaObjectDragState& drag, Rml::Vector2f point, ClientViewportRect viewport);
bool update_area_object_drag(ClientRenderer& renderer, AreaObjectDragState& drag, Rml::Vector2f point, ClientViewportRect viewport);
void cancel_area_object_drag(ClientRenderer& renderer, AreaObjectDragState& drag);
void commit_area_object_drag(ClientRenderer& renderer, AreaObjectDragState& drag_state, ObjectHandle active_object, ToolsetBackend& backend, const CommandContext& context, ShellController& shell);
bool add_encounter_spawn_point(ClientRenderer& renderer, Rml::Vector2f point, ClientViewportRect viewport, ToolsetBackend& backend, const CommandContext& context, ShellController& shell);
void arm_area_object_placement(ClientRenderer& renderer, AreaObjectPlacementState& placement, Resource resource, Rml::Vector2f point, std::string_view active_tab_id);
void cancel_area_object_placement(ClientRenderer& renderer, AreaObjectPlacementState& placement_state, ShellController& shell);
bool update_area_object_placement(ClientRenderer& renderer, AreaObjectPlacementState& placement, Rml::Vector2f point, const std::optional<ClientViewportRect>& viewport, std::string_view active_tab_id, int& pressed_recent_index, ShellController& shell);
bool accept_area_region_point(ClientRenderer& renderer, AreaObjectPlacementState& placement, Rml::Vector2f point, ClientViewportRect viewport, ShellController& shell);
bool complete_area_region_placement(ClientRenderer& renderer, AreaObjectPlacementState& placement, Rml::Vector2f point, ClientViewportRect viewport, ToolsetBackend& backend, const CommandContext& context, ShellController& shell);
void commit_area_object_placement(ClientRenderer& renderer, AreaObjectPlacementState& placement_state, ToolsetBackend& backend, const CommandContext& context, ShellController& shell);
} // namespace nw::toolset
