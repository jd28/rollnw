#pragma once

#include "object_edits.hpp"
#include "object_workbench.hpp"

#include <RmlUi/Core/Types.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Rml {
class Context;
class ElementDocument;
}

namespace nw::toolset {

class ShellController;
class ToolsetBackend;

enum class ProjectBlueprintDragPhase : uint8_t {
    idle,
    armed,
    target_valid,
    target_invalid,
};

enum class ProjectBlueprintDragKind : uint8_t {
    none,
    item,
    encounter_spawn,
    sound_resource,
};

enum class ProjectBlueprintDropTargetKind : uint8_t {
    none,
    inventory,
    equipment,
    encounter_spawns,
    sound_resources,
    store_inventory,
};

struct ProjectBlueprintDropTarget {
    ProjectBlueprintDropTargetKind kind = ProjectBlueprintDropTargetKind::none;
    int32_t page = -1;
    int32_t row = -1;
    int32_t column = -1;
    int32_t category = -1;
    nw::EquipIndex slot = nw::EquipIndex::invalid;

    bool operator==(const ProjectBlueprintDropTarget&) const = default;
};

struct ProjectBlueprintDragState {
    std::filesystem::path source_path;
    nw::Resource resource;
    nw::ObjectHandle owner{};
    nw::ObjectHandle item{};
    std::optional<EncounterSpawnEdit> encounter_spawn_edit;
    std::optional<SoundResourceEdit> sound_resource_edit;
    Rml::Vector2f drag_start;
    std::string tab_id;
    ProjectBlueprintDropTarget target;
    ProjectBlueprintDragKind kind = ProjectBlueprintDragKind::none;
    ProjectBlueprintDragPhase phase = ProjectBlueprintDragPhase::idle;
    int32_t width = 0;
    int32_t height = 0;
    bool threshold_crossed = false;
    bool materialization_failed = false;

    [[nodiscard]] bool active() const noexcept
    {
        return phase != ProjectBlueprintDragPhase::idle;
    }
};

// WorkspaceState::active_tab_id returns a string by value, so this snapshot owns
// its ID. Recapture the current facts after each ordered UI/native event.
struct ProjectResourceDragContext {
    std::string active_tab_id;
    ObjectHandle details_object{};
    ObjectHandle inventory_object{};
    ObjectWorkbenchSurface surface = ObjectWorkbenchSurface::details;
    bool object_matches_tab = false;
    bool inventory_matches_tab = false;
};

// Cancel clears DOM target markers before destroying the temporary item. Commit
// transfers a successfully placed item to its owner; failed edits destroy it.
// Invalid source/capacity/targets reject, and failed materialization is not
// retried until another arm. One displayed drag is an ordered UI singleton.
bool arm_project_blueprint_drag(ProjectBlueprintDragState& drag, const ProjectResourceDragContext& view, const Resource& resource, const std::filesystem::path& source_path, Rml::Vector2f point);
bool project_blueprint_drag_context_matches(const ProjectBlueprintDragState& drag, const ProjectResourceDragContext& view);
void cancel_project_blueprint_drag(Rml::ElementDocument* doc, ProjectBlueprintDragState& drag);
bool update_project_blueprint_drag(Rml::Context* context, Rml::ElementDocument* doc, ProjectBlueprintDragState& drag, const ProjectResourceDragContext& view, Rml::Vector2f point, int32_t inventory_page, int& pressed_recent_index, ShellController& shell);
void commit_project_blueprint_drag(Rml::ElementDocument* doc, ProjectBlueprintDragState& drag_state, ToolsetBackend& backend, const CommandContext& context, ShellController& shell);
} // namespace nw::toolset
