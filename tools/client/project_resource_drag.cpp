#include "project_resource_drag.hpp"
#include "command_view.hpp"
#include "shell_controller.hpp"
#include "toolset_backend.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Store.hpp>

#include <RmlUi/Core.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <utility>

namespace nw::toolset {
namespace {

constexpr int kCreatureInventoryCellPx = 32;
constexpr float kWorkspaceTabDragThresholdPx = 5.0f;

Rml::Element* find_el(Rml::ElementDocument* document, const char* id)
{
    return document ? document->GetElementById(id) : nullptr;
}

Rml::Element* find_ancestor_with_id(Rml::Element* element, std::string_view id)
{
    for (auto* cursor = element; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->GetId() == id) { return cursor; }
    }
    return nullptr;
}

Rml::Element* find_ancestor_with_class(Rml::Element* element, std::string_view class_name)
{
    for (auto* cursor = element; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->IsClassSet(class_name.data())) { return cursor; }
    }
    return nullptr;
}

std::optional<int32_t> parse_decimal_int32(std::string_view value)
{
    if (value.empty()) { return std::nullopt; }
    int32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) { return std::nullopt; }
    return result;
}

bool project_blueprint_drag_resource(const nw::Resource& resource,
    const std::filesystem::path& source_path,
    nw::ResourceType::type type) noexcept
{
    return resource.valid()
        && resource.type == type
        && source_path.extension() == ".json";
}

void clear_project_blueprint_drop_visuals(Rml::ElementDocument* doc,
    const ProjectBlueprintDragState& drag)
{
    if (auto* target = find_el(doc, "creature_inventory_drop_target")) {
        target->SetProperty("display", "none");
        target->SetClass("valid", false);
        target->SetClass("invalid", false);
    }
    if (drag.target.kind == ProjectBlueprintDropTargetKind::equipment
        && static_cast<uint32_t>(drag.target.slot) < 18) {
        const std::string id = "creature_equipment_slot_"
            + std::to_string(static_cast<uint32_t>(drag.target.slot));
        if (auto* slot = find_el(doc, id.c_str())) {
            slot->SetClass("drop_valid", false);
            slot->SetClass("drop_invalid", false);
        }
    }
    if (auto* spawns = find_el(doc, "encounter_spawn_collection")) {
        spawns->SetClass("drop_valid", false);
        spawns->SetClass("drop_invalid", false);
    }
    if (auto* sounds = find_el(doc, "sound_resource_collection")) {
        sounds->SetClass("drop_valid", false);
        sounds->SetClass("drop_invalid", false);
    }
    for (int32_t category = 0; category < 5; ++category) {
        const std::string id = "store_inventory_drop_" + std::to_string(category);
        if (auto* target = find_el(doc, id.c_str())) {
            target->SetClass("drop_valid", false);
            target->SetClass("drop_invalid", false);
        }
    }
}

void set_project_blueprint_drop_target(Rml::ElementDocument* doc,
    ProjectBlueprintDragState& drag,
    ProjectBlueprintDropTarget target,
    bool valid)
{
    clear_project_blueprint_drop_visuals(doc, drag);
    drag.target = target;
    drag.phase = valid
        ? ProjectBlueprintDragPhase::target_valid
        : ProjectBlueprintDragPhase::target_invalid;

    if (target.kind == ProjectBlueprintDropTargetKind::inventory) {
        if (auto* overlay = find_el(doc, "creature_inventory_drop_target")) {
            const int top = (target.row - drag.height + 1) * kCreatureInventoryCellPx;
            overlay->SetProperty("display", "block");
            overlay->SetProperty("left",
                std::to_string(target.column * kCreatureInventoryCellPx) + "px");
            overlay->SetProperty("top", std::to_string(top) + "px");
            overlay->SetProperty("width",
                std::to_string(drag.width * kCreatureInventoryCellPx) + "px");
            overlay->SetProperty("height",
                std::to_string(drag.height * kCreatureInventoryCellPx) + "px");
            overlay->SetClass("valid", valid);
            overlay->SetClass("invalid", !valid);
        }
    } else if (target.kind == ProjectBlueprintDropTargetKind::equipment
        && static_cast<uint32_t>(target.slot) < 18) {
        const std::string id = "creature_equipment_slot_"
            + std::to_string(static_cast<uint32_t>(target.slot));
        if (auto* slot = find_el(doc, id.c_str())) {
            slot->SetClass("drop_valid", valid);
            slot->SetClass("drop_invalid", !valid);
        }
    } else if (target.kind == ProjectBlueprintDropTargetKind::encounter_spawns) {
        if (auto* spawns = find_el(doc, "encounter_spawn_collection")) {
            spawns->SetClass("drop_valid", valid);
            spawns->SetClass("drop_invalid", !valid);
        }
    } else if (target.kind == ProjectBlueprintDropTargetKind::sound_resources) {
        if (auto* sounds = find_el(doc, "sound_resource_collection")) {
            sounds->SetClass("drop_valid", valid);
            sounds->SetClass("drop_invalid", !valid);
        }
    } else if (target.kind == ProjectBlueprintDropTargetKind::store_inventory
        && target.category >= 0 && target.category < 5) {
        const std::string id = "store_inventory_drop_"
            + std::to_string(target.category);
        if (auto* store_target = find_el(doc, id.c_str())) {
            store_target->SetClass("drop_valid", valid);
            store_target->SetClass("drop_invalid", !valid);
        }
    }
}

bool materialize_project_blueprint_drag(ProjectBlueprintDragState& drag, ShellController& shell)
{
    if (drag.kind == ProjectBlueprintDragKind::sound_resource) {
        if (drag.sound_resource_edit) {
            return true;
        }
        if (drag.materialization_failed) {
            return false;
        }

        if (!drag.resource.valid()
            || drag.resource.type != nw::ResourceType::wav
            || drag.resource.resref.empty()) {
            drag.materialization_failed = true;
            shell.append_output("error",
                "Sound resource drag source or target list is invalid or full");
            return false;
        }

        const std::array additions{drag.resource.resref};
        drag.sound_resource_edit = nw::toolset::make_sound_resource_additions(
            nw::kernel::runtime(), drag.owner, additions);
        if (!drag.sound_resource_edit) {
            drag.materialization_failed = true;
            shell.append_output("error",
                "Sound resource drag source or target list is invalid or full");
            return false;
        }
        return true;
    }

    if (drag.kind == ProjectBlueprintDragKind::encounter_spawn) {
        if (drag.encounter_spawn_edit) {
            return true;
        }
        if (drag.materialization_failed) {
            return false;
        }

        std::ifstream input{drag.source_path};
        input >> std::ws;
        if (!input || input.peek() != '{') {
            drag.materialization_failed = true;
            shell.append_output("warn",
                "Encounter spawn drag requires an authored component/propset JSON Creature blueprint");
            return false;
        }

        auto* creature = nw::kernel::objects().load_file<nw::Creature>(
            drag.source_path);
        if (!creature) {
            drag.materialization_failed = true;
            shell.append_output("error",
                "Encounter spawn Creature blueprint could not be loaded");
            return false;
        }

        const std::array creature_handles{creature->handle()};
        auto rows = nw::toolset::make_encounter_spawn_records(
            nw::kernel::runtime(), creature_handles);
        nw::kernel::objects().destroy(creature->handle());
        auto before = nw::toolset::snapshot_encounter_spawns(
            nw::kernel::runtime(), drag.owner);
        if (!rows || rows->size() != 1 || !before || before->size() >= 1024) {
            drag.materialization_failed = true;
            shell.append_output("error",
                "Encounter spawn drag source or target list is invalid or full");
            return false;
        }

        auto after = *before;
        after.push_back(std::move(rows->front()));
        drag.encounter_spawn_edit = nw::toolset::EncounterSpawnEdit{
            .encounter = drag.owner,
            .before = std::move(*before),
            .after = std::move(after),
        };
        return true;
    }

    if (drag.kind != ProjectBlueprintDragKind::item) {
        return false;
    }
    if (drag.item.type == nw::ObjectType::item) {
        return true;
    }
    if (drag.materialization_failed) {
        return false;
    }

    std::ifstream input{drag.source_path};
    input >> std::ws;
    if (!input || input.peek() != '{') {
        drag.materialization_failed = true;
        shell.append_output("warn", "Item drag requires an authored component/propset JSON blueprint");
        return false;
    }

    auto* item = nw::kernel::objects().load_file<nw::Item>(drag.source_path);
    const auto* layout = item
        ? nw::kernel::objects().components().find_item_layout(item->handle())
        : nullptr;
    if (!item || !layout || layout->inventory_width <= 0
        || layout->inventory_height <= 0
        || layout->inventory_width > nw::Inventory::max_columns
        || layout->inventory_height > nw::Inventory::max_rows) {
        if (item) {
            nw::kernel::objects().destroy(item->handle());
        }
        drag.materialization_failed = true;
        shell.append_output("error", "Item drag blueprint has no valid inventory footprint");
        return false;
    }

    drag.item = item->handle();
    drag.width = layout->inventory_width;
    drag.height = layout->inventory_height;
    return true;
}

} // namespace

bool arm_project_blueprint_drag(ProjectBlueprintDragState& drag, const ProjectResourceDragContext& view, const Resource& resource, const std::filesystem::path& source_path, Rml::Vector2f point)
{
    if (project_blueprint_drag_resource(
            resource, source_path, nw::ResourceType::uti)
        && view.surface == ObjectWorkbenchSurface::store_inventory
        && view.object_matches_tab
        && view.details_object.type == nw::ObjectType::store) {
        drag = {
            .source_path = source_path,
            .resource = resource,
            .owner = view.details_object,
            .drag_start = point,
            .tab_id = std::string{view.active_tab_id},
            .kind = ProjectBlueprintDragKind::item,
            .phase = ProjectBlueprintDragPhase::armed,
        };
        return true;
    }

    if (project_blueprint_drag_resource(
            resource, source_path, nw::ResourceType::uti)
        && view.surface == ObjectWorkbenchSurface::inventory
        && view.inventory_matches_tab) {
        drag = {
            .source_path = source_path,
            .resource = resource,
            .owner = view.inventory_object,
            .drag_start = point,
            .tab_id = std::string{view.active_tab_id},
            .kind = ProjectBlueprintDragKind::item,
            .phase = ProjectBlueprintDragPhase::armed,
        };
        return true;
    }

    if (project_blueprint_drag_resource(
            resource, source_path, nw::ResourceType::utc)
        && view.surface == ObjectWorkbenchSurface::spawns
        && view.object_matches_tab
        && view.details_object.type == nw::ObjectType::encounter) {
        drag = {
            .source_path = source_path,
            .resource = resource,
            .owner = view.details_object,
            .drag_start = point,
            .tab_id = std::string{view.active_tab_id},
            .kind = ProjectBlueprintDragKind::encounter_spawn,
            .phase = ProjectBlueprintDragPhase::armed,
        };
        return true;
    }

    if (resource.valid()
        && resource.type == nw::ResourceType::wav
        && view.surface == ObjectWorkbenchSurface::sounds
        && view.object_matches_tab
        && view.details_object.type == nw::ObjectType::sound) {
        drag = {
            .source_path = source_path,
            .resource = resource,
            .owner = view.details_object,
            .drag_start = point,
            .tab_id = std::string{view.active_tab_id},
            .kind = ProjectBlueprintDragKind::sound_resource,
            .phase = ProjectBlueprintDragPhase::armed,
        };
        return true;
    }

    return false;
}

bool project_blueprint_drag_context_matches(const ProjectBlueprintDragState& drag, const ProjectResourceDragContext& view)
{
    if (!drag.active() || view.active_tab_id != drag.tab_id) {
        return false;
    }
    switch (drag.kind) {
    case ProjectBlueprintDragKind::item:
        if (drag.owner.type == nw::ObjectType::store) {
            return view.surface
                == ObjectWorkbenchSurface::store_inventory
                && view.details_object == drag.owner
                && view.object_matches_tab;
        }
        return view.surface == ObjectWorkbenchSurface::inventory
            && view.inventory_object == drag.owner
            && view.inventory_matches_tab;
    case ProjectBlueprintDragKind::encounter_spawn:
        return view.surface == ObjectWorkbenchSurface::spawns
            && view.details_object == drag.owner
            && view.object_matches_tab;
    case ProjectBlueprintDragKind::sound_resource:
        return view.surface == ObjectWorkbenchSurface::sounds
            && view.details_object == drag.owner
            && view.object_matches_tab;
    default:
        return false;
    }
}

void cancel_project_blueprint_drag(Rml::ElementDocument* doc, ProjectBlueprintDragState& drag)
{
    if (!drag.active()) {
        return;
    }
    clear_project_blueprint_drop_visuals(doc, drag);
    const auto item = drag.item;
    drag = {};
    if (nw::kernel::objects().valid(item)) {
        nw::kernel::objects().destroy(item);
    }
}

bool update_project_blueprint_drag(Rml::Context* context, Rml::ElementDocument* doc, ProjectBlueprintDragState& drag, const ProjectResourceDragContext& view, Rml::Vector2f point, int32_t inventory_page, int& pressed_recent_index, ShellController& shell)
{
    if (!drag.active()) {
        return false;
    }

    const float dx = point.x - drag.drag_start.x;
    const float dy = point.y - drag.drag_start.y;
    if (!drag.threshold_crossed
        && std::abs(dx) < kWorkspaceTabDragThresholdPx
        && std::abs(dy) < kWorkspaceTabDragThresholdPx) {
        return false;
    }
    drag.threshold_crossed = true;
    pressed_recent_index = -1;

    if (!project_blueprint_drag_context_matches(drag, view)) {
        set_project_blueprint_drop_target(doc, drag, {}, false);
        return true;
    }

    if (drag.kind == ProjectBlueprintDragKind::encounter_spawn) {
        if (!materialize_project_blueprint_drag(drag, shell)) {
            set_project_blueprint_drop_target(doc, drag, {}, false);
            return true;
        }
        auto* hit = context ? context->GetElementAtPoint(point) : nullptr;
        const bool over_spawn_collection = find_ancestor_with_id(hit, "encounter_spawn_collection") != nullptr;
        const ProjectBlueprintDropTarget target{
            .kind = over_spawn_collection
                ? ProjectBlueprintDropTargetKind::encounter_spawns
                : ProjectBlueprintDropTargetKind::none,
        };
        if (target == drag.target
            && (drag.phase == ProjectBlueprintDragPhase::target_valid
                || drag.phase == ProjectBlueprintDragPhase::target_invalid)) {
            return true;
        }
        set_project_blueprint_drop_target(doc, drag, target, over_spawn_collection);
        return true;
    }

    if (drag.kind == ProjectBlueprintDragKind::sound_resource) {
        if (!materialize_project_blueprint_drag(drag, shell)) {
            set_project_blueprint_drop_target(doc, drag, {}, false);
            return true;
        }
        auto* hit = context ? context->GetElementAtPoint(point) : nullptr;
        const bool over_sound_collection = find_ancestor_with_id(hit, "sound_resource_collection") != nullptr;
        const ProjectBlueprintDropTarget target{
            .kind = over_sound_collection
                ? ProjectBlueprintDropTargetKind::sound_resources
                : ProjectBlueprintDropTargetKind::none,
        };
        if (target == drag.target
            && (drag.phase == ProjectBlueprintDragPhase::target_valid
                || drag.phase == ProjectBlueprintDragPhase::target_invalid)) {
            return true;
        }
        set_project_blueprint_drop_target(doc, drag, target, over_sound_collection);
        return true;
    }

    if (!materialize_project_blueprint_drag(drag, shell)) {
        set_project_blueprint_drop_target(doc, drag, {}, false);
        return true;
    }

    if (drag.owner.type == nw::ObjectType::store) {
        auto* hit = context ? context->GetElementAtPoint(point) : nullptr;
        auto* store_target = find_ancestor_with_class(
            hit, "store_inventory_drop_target");
        const auto category = store_target
            ? parse_decimal_int32(
                  store_target->GetAttribute<Rml::String>("data-category", ""))
            : std::nullopt;
        ProjectBlueprintDropTarget target{
            .kind = category
                    && *category >= 0 && *category < 5
                ? ProjectBlueprintDropTargetKind::store_inventory
                : ProjectBlueprintDropTargetKind::none,
            .category = category.value_or(-1),
        };

        auto* store = nw::kernel::objects().get<nw::Store>(drag.owner);
        nw::Inventory* inventory = nullptr;
        if (store && category) {
            switch (*category) {
            case 0:
                inventory = &store->inventory().armor;
                break;
            case 1:
                inventory = &store->inventory().miscellaneous;
                break;
            case 2:
                inventory = &store->inventory().potions;
                break;
            case 3:
                inventory = &store->inventory().rings;
                break;
            case 4:
                inventory = &store->inventory().weapons;
                break;
            default:
                break;
            }
        }
        const bool valid = inventory
            && inventory->items.size() < inventory->items.capacity();
        if (target == drag.target
            && (drag.phase == ProjectBlueprintDragPhase::target_valid
                || drag.phase == ProjectBlueprintDragPhase::target_invalid)) {
            return true;
        }
        set_project_blueprint_drop_target(doc, drag, target, valid);
        return true;
    }

    auto* creature = drag.owner.type == nw::ObjectType::creature
        ? nw::kernel::objects().get<nw::Creature>(drag.owner)
        : nullptr;
    auto* owner_item = drag.owner.type == nw::ObjectType::item
        ? nw::kernel::objects().get<nw::Item>(drag.owner)
        : nullptr;
    auto* owner_placeable = drag.owner.type == nw::ObjectType::placeable
        ? nw::kernel::objects().get<nw::Placeable>(drag.owner)
        : nullptr;
    nw::Inventory* inventory = creature ? &creature->inventory()
        : owner_item                    ? &owner_item->inventory()
        : owner_placeable               ? &owner_placeable->inventory()
                                        : nullptr;
    if (!inventory) {
        set_project_blueprint_drop_target(doc, drag, {}, false);
        return true;
    }

    auto* hit = context ? context->GetElementAtPoint(point) : nullptr;
    if (creature) {
        if (auto* equipment = find_ancestor_with_class(hit, "creature_equipment_slot")) {
            const auto slot_value = parse_decimal_int32(
                equipment->GetAttribute<Rml::String>("data-slot", ""));
            ProjectBlueprintDropTarget target;
            target.kind = ProjectBlueprintDropTargetKind::equipment;
            if (slot_value && *slot_value >= 0 && *slot_value < 18) {
                target.slot = static_cast<nw::EquipIndex>(*slot_value);
            }
            if (target == drag.target
                && (drag.phase == ProjectBlueprintDragPhase::target_valid
                    || drag.phase == ProjectBlueprintDragPhase::target_invalid)) {
                return true;
            }
            const auto* layout = nw::kernel::objects().components().find_item_layout(drag.item);
            auto* item = nw::kernel::objects().get<nw::Item>(drag.item);
            const bool valid = static_cast<uint32_t>(target.slot) < 18
                && item && layout
                && !nw::get_equipped_item(creature, target.slot)
                && nw::toolset::can_place_creature_item_in_slot(drag.item, target.slot)
                && creature->inventory().find_slot(
                                            layout->inventory_width, layout->inventory_height)
                        .page
                    >= 0;
            set_project_blueprint_drop_target(doc, drag, target, valid);
            return true;
        }
    }

    auto* board = find_el(doc, "creature_inventory_board");
    if (!board || !board->IsVisible(true)
        || !board->IsPointWithinElement(point)) {
        set_project_blueprint_drop_target(doc, drag, {}, false);
        return true;
    }

    const float left = board->GetAbsoluteLeft() + board->GetClientLeft();
    const float top = board->GetAbsoluteTop() + board->GetClientTop();
    const int column = static_cast<int>((point.x - left) / kCreatureInventoryCellPx);
    const int visual_row = static_cast<int>((point.y - top) / kCreatureInventoryCellPx);
    ProjectBlueprintDropTarget target{
        .kind = ProjectBlueprintDropTargetKind::inventory,
        .page = inventory_page,
        .row = visual_row + drag.height - 1,
        .column = column,
    };
    if (target == drag.target
        && (drag.phase == ProjectBlueprintDragPhase::target_valid
            || drag.phase == ProjectBlueprintDragPhase::target_invalid)) {
        return true;
    }
    const bool in_bounds = target.page >= 0
        && target.page < inventory->pages()
        && visual_row >= 0
        && visual_row + drag.height <= inventory->rows()
        && column >= 0
        && column + drag.width <= inventory->columns();
    const bool valid = in_bounds
        && inventory->check_available(
            target.page, target.row, target.column, drag.width, drag.height);
    set_project_blueprint_drop_target(doc, drag, target, valid);
    return true;
}

void commit_project_blueprint_drag(Rml::ElementDocument* doc, ProjectBlueprintDragState& drag_state, ToolsetBackend& backend, const CommandContext& context, ShellController& shell)
{
    if (!drag_state.active()) {
        return;
    }
    if (drag_state.phase != ProjectBlueprintDragPhase::target_valid) {
        cancel_project_blueprint_drag(doc, drag_state);
        return;
    }

    clear_project_blueprint_drop_visuals(doc, drag_state);
    auto drag = std::move(drag_state);
    drag_state = {};
    if (drag.kind == ProjectBlueprintDragKind::encounter_spawn) {
        if (!drag.encounter_spawn_edit
            || drag.target.kind != ProjectBlueprintDropTargetKind::encounter_spawns) {
            return;
        }
        auto result = backend.replace_encounter_spawns(
            std::move(*drag.encounter_spawn_edit),
            context);
        append_command_results(shell, {&result, 1});
        return;
    }
    if (drag.kind == ProjectBlueprintDragKind::sound_resource) {
        if (!drag.sound_resource_edit
            || drag.target.kind != ProjectBlueprintDropTargetKind::sound_resources) {
            return;
        }
        auto result = backend.replace_sound_resources(
            std::move(*drag.sound_resource_edit),
            "Add sound resource",
            context);
        append_command_results(shell, {&result, 1});
        return;
    }

    if (drag.kind != ProjectBlueprintDragKind::item
        || !nw::kernel::objects().valid(drag.item)) {
        return;
    }
    const auto destroy_unowned_item = [&drag]() {
        if (nw::kernel::objects().valid(drag.item)) {
            nw::kernel::objects().destroy(drag.item);
        }
    };
    if (drag.target.kind == ProjectBlueprintDropTargetKind::store_inventory) {
        if (drag.target.category < 0 || drag.target.category >= 5) {
            destroy_unowned_item();
            return;
        }
        const std::array placements{nw::toolset::StoreItemPlacement{
            .item = drag.item,
            .category = static_cast<nw::toolset::StoreInventoryCategory>(
                drag.target.category),
        }};
        auto result = backend.place_store_items(
            drag.owner,
            placements,
            context);
        if (result.status != nw::toolset::CommandStatus::success) {
            destroy_unowned_item();
        }
        append_command_results(shell, {&result, 1});
        return;
    }
    nw::toolset::ItemPlacement placement{
        .item = drag.item,
        .target = drag.target.kind == ProjectBlueprintDropTargetKind::equipment
            ? nw::toolset::ItemPlacementTarget::equipment
            : nw::toolset::ItemPlacementTarget::inventory,
        .page = drag.target.page,
        .row = drag.target.row,
        .column = drag.target.column,
        .slot = drag.target.slot,
    };
    const std::array placements{placement};
    auto result = backend.place_items(
        drag.owner,
        placements,
        context);
    if (result.status != nw::toolset::CommandStatus::success) {
        destroy_unowned_item();
    }
    append_command_results(shell, {&result, 1});
}

} // namespace nw::toolset
