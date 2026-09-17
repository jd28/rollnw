#pragma once

#include "client_input_routes.hpp"
#include "object_workbench.hpp"
#include "smalls_creature_inventory.hpp"

#include <optional>
#include <string>

namespace Rml {
class ElementDocument;
class Element;
}

namespace nw::toolset {
class WorkspaceState;
class ToolsetBackend;
class ShellController;
struct CommandContext;

enum class InventoryWorkbenchClickKind : uint8_t {
    none,
    equipment,
    item,
    page,
};

// Schema 1: a selected UI edge owns its fixed semantic header and tab bytes.
// Dense source indices must still name these live item identities after release.
struct InventoryWorkbenchClick {
    ObjectHandle object{};
    ObjectHandle inventory_item{};
    ObjectHandle equipped_item{};
    uint64_t module_generation = 0;
    uint64_t mutation_epoch = 0;
    int32_t index = -1;
    int32_t selection = -1;
    int32_t page = 0;
    InventoryWorkbenchClickKind kind = InventoryWorkbenchClickKind::none;
    ClientRmlForwardPhase release_phase = ClientRmlForwardPhase::none;
    std::string tab_id;
};

// One displayed inventory owns the validated SmallS row/text batches, selected
// source index/page and generated item textures. Clearing rows retains the icon
// cache. Texture binding borrows this collection until unbound before destruction.
// DOM and target facts are borrowed for one synchronous call. Invalid provider
// data renders its diagnostic; stale tabs render waiting. Rebuild clamps invalid
// pages to 0 and drops out-of-range selection to -1. Providers retain grid rules.
struct InventoryWorkbenchViewState {
    nw::toolset::ItemIconTextureCache item_icon_cache;
    nw::toolset::InventoryViewSnapshot creature_inventory;
    bool creature_inventory_rendered = false;
    int32_t creature_inventory_page = 0;
    int32_t creature_inventory_selection = -1;
};

std::optional<InventoryWorkbenchClick> capture_inventory_workbench_click(
    Rml::Element* hit, const InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target,
    const WorkspaceState& workspace, uint64_t module_generation);
// True requests the existing window sync, including a rejected backend command
// whose result has been displayed. Stale/invalid protocol requests return false.
bool apply_inventory_workbench_click(InventoryWorkbenchClick& click,
    InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target,
    const WorkspaceState& workspace, ToolsetBackend& backend,
    ShellController& shell, const CommandContext& context);

bool active_creature_inventory_matches_tab(const InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target);
void rebuild_active_creature_inventory(InventoryWorkbenchViewState& state, nw::ObjectHandle object);
void clear_active_creature_inventory(InventoryWorkbenchViewState& state);
bool sync_creature_inventory_window(Rml::ElementDocument* doc, InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target, bool force);
void append_creature_inventory_markup(std::string& content_markup, const InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target);

} // namespace nw::toolset
