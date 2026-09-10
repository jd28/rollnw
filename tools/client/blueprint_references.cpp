#include "blueprint_references.hpp"

#include "area_navigation.hpp"
#include "object_edits.hpp"

#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/profiles/nwn1/item_materialization.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/serialization/component_propset_json.hpp>

#include <absl/container/flat_hash_set.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace nw::toolset {
namespace {
using Json = nlohmann::json;

constexpr std::array store_sections{"armor", "miscellaneous", "potions", "rings", "weapons"};

} // namespace

ObjectType blueprint_document_object_type(ResourceType::type type) noexcept
{
    return type == ResourceType::caf ? ObjectType::area
                                     : blueprint_object_type(type);
}

namespace {

ObjectBase* allocate_root(ObjectType type)
{
    switch (type) {
    case ObjectType::creature:
        return kernel::objects().make<Creature>();
    case ObjectType::door:
        return kernel::objects().make<Door>();
    case ObjectType::encounter:
        return kernel::objects().make<Encounter>();
    case ObjectType::item:
        return kernel::objects().make<Item>();
    case ObjectType::placeable:
        return kernel::objects().make<Placeable>();
    case ObjectType::sound:
        return kernel::objects().make<Sound>();
    case ObjectType::store:
        return kernel::objects().make<Store>();
    case ObjectType::trigger:
        return kernel::objects().make<Trigger>();
    case ObjectType::waypoint:
        return kernel::objects().make<Waypoint>();
    case ObjectType::area:
        return kernel::objects().make<Area>();
    default:
        return nullptr;
    }
}

// Transient JSON references only live during this callback; persisted traversal
// state consists of paths/indices. No references survive hash-table insertion.
template <typename F>
void visit_children(const Json& value, F&& visit)
{
    const auto& components = value.at("components");
    if (!components.is_object()) { throw std::runtime_error("Invalid components section"); }
    const auto inventory = [&](const Json& rows, const std::string& prefix) {
        if (!rows.is_array() || rows.size() > 1024) { throw std::runtime_error("Inventory must have at most 1024 rows"); }
        for (size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            const auto& position = row.at("position");
            if (!position.is_array() || position.size() != 2) { throw std::runtime_error("Invalid inventory position"); }
            for (const auto& coordinate : position) {
                if (!coordinate.is_number_integer()) { throw std::runtime_error("Inventory positions must be integers"); }
                const auto number = coordinate.get<int64_t>();
                if (number < 0 || number > UINT16_MAX) { throw std::runtime_error("Inventory position is outside the stored range"); }
            }
            visit(row.at("item"), prefix + "/" + std::to_string(index) + "/item");
        }
    };
    if (components.contains("inventory")) { inventory(components.at("inventory"), "/components/inventory"); }
    if (components.contains("equipment") && !components.at("equipment").is_null()) {
        const auto& slots = components.at("equipment");
        if (!slots.is_object()) { throw std::runtime_error("Invalid equipment section"); }
        for (const auto& [key, item] : slots.items()) {
            bool known = false;
            for (int index = 0; index < 18; ++index) {
                if (key == equip_index_to_string(static_cast<EquipIndex>(index))) {
                    known = true;
                    break;
                }
            }
            if (!known) { throw std::runtime_error("Unknown equipment slot: " + key); }
            visit(item, "/components/equipment/" + key);
        }
    }
    if (components.contains("store_inventory")) {
        const auto& store = components.at("store_inventory");
        for (const auto* section : store_sections) {
            inventory(store.at(section), std::string{"/components/store_inventory/"} + section);
        }
    }
}

struct Node {
    std::string path;
    std::string owner_path;
    ObjectType type;
    ObjectType owner_type;
    bool placed = false;
    bool definition = false;
    bool covered = false;
};

std::vector<Node> roots(const Json& document, ResourceType::type type)
{
    std::vector<Node> result;
    if (type == ResourceType::caf) {
        if (document.at("$type") != "CAF" || document.at("$version") != Area::json_archive_version) {
            throw std::runtime_error("Unsupported area document version");
        }
        for (const auto& definition : blueprint_types()) {
            const auto& members = document.at(definition.area_category);
            if (!members.is_array()) { throw std::runtime_error("Invalid area member array"); }
            for (size_t index = 0; index < members.size(); ++index) {
                auto path = std::string{"/"} + std::string{definition.area_category}
                    + "/" + std::to_string(index);
                result.push_back({path, path, definition.object_type,
                    definition.object_type, true, false, false});
            }
        }
    } else {
        const auto kind = blueprint_document_object_type(type);
        if (kind == ObjectType::invalid) { throw std::runtime_error("Unsupported authored document"); }
        result.push_back({"", "", kind, kind, false, true, false});
    }
    return result;
}

Json expanded_instance(Resource source, const absl::flat_hash_map<Resource, Json>& blueprints)
{
    Json result = blueprints.at(source);
    struct Expansion {
        std::string path;
        Resource key;
        size_t depth;
    };
    std::vector<Expansion> pending{{"", source, 0}};
    while (!pending.empty()) {
        const auto row = std::move(pending.back());
        pending.pop_back();
        if (row.depth > 256) { throw std::runtime_error("Blueprint nesting exceeds the supported depth of 256"); }
        auto& value = result.at(Json::json_pointer{row.path});
        value = blueprints.at(row.key);
        value.at("object").erase("uuid");
        value.at("components")["location"] = Location{};
        visit_children(value, [&](const Json& child, const std::string& suffix) {
            if (!child.is_string() || child.get_ref<const std::string&>().empty()) {
                throw std::runtime_error("Expected a saved item reference in blueprint");
            }
            pending.push_back({row.path + suffix, Resource{child.get<std::string>(), ResourceType::uti}, row.depth + 1});
        });
    }
    return result;
}

// Check all expected children survived deserialization, including exact container
// positions and equipment slots. The engine loaders can otherwise warn and drop.
void check_children(const Json& expected, const Json& actual)
{
    const auto signature = [](const Json& root) {
        std::vector<std::pair<std::string, std::string>> result;
        std::vector<std::string> paths{""};
        while (!paths.empty()) {
            auto path = std::move(paths.back());
            paths.pop_back();
            const auto& value = root.at(Json::json_pointer{path});
            result.emplace_back(path, value.at("object").at("resref").get<std::string>());
            visit_children(value, [&](const Json& child, const std::string& suffix) {
                if (suffix.ends_with("/item")) {
                    const auto container_path = path + suffix.substr(0, suffix.size() - 5);
                    auto metadata = root.at(Json::json_pointer{container_path});
                    metadata.erase("item");
                    result.emplace_back(container_path, metadata.dump());
                }
                if (child.is_string()) {
                    result.emplace_back(path + suffix, child.get<std::string>());
                } else if (child.is_object()) {
                    paths.push_back(path + suffix);
                } else {
                    throw std::runtime_error("Invalid contained item");
                }
            });
        }
        return result;
    };
    if (signature(expected) != signature(actual)) { throw std::runtime_error("Object loading changed or dropped contained items"); }
}

void validate_owned_items(ObjectHandle root)
{
    std::vector<ObjectHandle> pending{root};
    absl::flat_hash_set<uint64_t> visited;
    for (size_t index = 0; index < pending.size(); ++index) {
        const auto handle = pending[index];
        if (!visited.insert(handle.to_ull()).second) { throw std::runtime_error("Repeated contained item ownership"); }
        auto* object = kernel::objects().get_object_base(handle);
        if (!object) { throw std::runtime_error("Missing contained object"); }
        const auto inventory = [&](const Inventory& source) {
            Inventory grid{source.pages(), source.rows(), source.columns()};
            for (const auto& row : source.items) {
                auto* item = inventory_item_ptr(row);
                if (!item) { throw std::runtime_error("Unresolved inventory item"); }
                const auto* layout = kernel::objects().components().find_item_layout(item->handle());
                const auto slot = grid.xy_to_slot(row.pos_x, row.pos_y);
                if (!layout || layout->inventory_width < 1 || layout->inventory_height < 1
                    || slot.row < 0 || slot.row >= grid.rows() || slot.col < 0 || slot.col >= grid.columns()
                    || !grid.check_available(slot.page, slot.row, slot.col, layout->inventory_width, layout->inventory_height)
                    || !grid.insert_item(slot.page, slot.row, slot.col, layout->inventory_width, layout->inventory_height)) {
                    throw std::runtime_error("Replacement items do not fit the existing inventory positions");
                }
                pending.push_back(item->handle());
            }
        };
        if (const auto* items = kernel::objects().components().find_inventory(*object)) { inventory(*items); }
        if (const auto* store = kernel::objects().components().find_store_inventory(*object)) {
            inventory(store->armor);
            inventory(store->miscellaneous);
            inventory(store->potions);
            inventory(store->rings);
            inventory(store->weapons);
        }
        if (const auto* creature = object->as_creature()) {
            for (int slot = 0; slot < 18; ++slot) {
                if (const auto* item = equip_item_ptr(creature->equipment.equips[slot])) {
                    if (!can_place_creature_item_in_slot(item->handle(), static_cast<EquipIndex>(slot))) {
                        throw std::runtime_error("Replacement item is not valid in its equipment slot");
                    }
                    pending.push_back(item->handle());
                }
            }
        }
    }
}

bool load_root(ObjectType type, const Json& value, SerializationProfile profile, ObjectDocument& owner, std::string& error)
{
    auto* object = allocate_root(type);
    if (!object || !owner.adopt(object->handle())) {
        if (object) { kernel::objects().destroy(object->handle()); }
        error = "Cannot allocate replacement document";
        return false;
    }
    if (type == ObjectType::area) {
        if (!deserialize(object->as_area(), value) || !object->instantiate()) {
            error = "Cannot load replacement area";
            return false;
        }
        std::vector<PlacedAreaObjectRow> members;
        build_placed_area_object_rows(*object->as_area(), members);
        for (const auto& member : members) {
            if (!kernel::objects().components().set_area(member.object, object->handle().id)) {
                error = "Cannot attach replacement area member";
                return false;
            }
        }
    } else {
        const auto loaded = object_from_component_propset_json(object, value, &kernel::runtime(), profile);
        if (!loaded || !object->instantiate()) {
            error = loaded ? "Cannot instantiate replacement" : loaded.error;
            return false;
        }
        if (auto* item = object->as_item()) {
            const auto result = nwn1::materialize_item_native_components(*item, kernel::runtime());
            if (!result) {
                error = result.error;
                return false;
            }
        }
    }
    return true;
}

Inventory* live_inventory(ObjectHandle owner, BlueprintInstanceOwner attachment)
{
    auto* object = kernel::objects().get_object_base(owner);
    if (!object) { return nullptr; }
    auto& components = kernel::objects().components();
    if (attachment == BlueprintInstanceOwner::inventory) { return components.find_inventory(*object); }
    auto* store = components.find_store_inventory(*object);
    if (!store) { return nullptr; }
    switch (attachment) {
    case BlueprintInstanceOwner::store_armor:
        return &store->armor;
    case BlueprintInstanceOwner::store_miscellaneous:
        return &store->miscellaneous;
    case BlueprintInstanceOwner::store_potions:
        return &store->potions;
    case BlueprintInstanceOwner::store_rings:
        return &store->rings;
    case BlueprintInstanceOwner::store_weapons:
        return &store->weapons;
    default:
        return nullptr;
    }
}

void append_live_children(ObjectHandle owner, std::vector<BlueprintInstanceRow>& rows)
{
    auto* object = kernel::objects().get_object_base(owner);
    if (!object) { throw std::runtime_error("Missing live object"); }
    constexpr std::array inventories{BlueprintInstanceOwner::inventory, BlueprintInstanceOwner::store_armor,
        BlueprintInstanceOwner::store_miscellaneous, BlueprintInstanceOwner::store_potions,
        BlueprintInstanceOwner::store_rings, BlueprintInstanceOwner::store_weapons};
    for (const auto attachment : inventories) {
        const auto* inventory = live_inventory(owner, attachment);
        if (!inventory) { continue; }
        for (size_t index = 0; index < inventory->items.size(); ++index) {
            const auto* item = inventory_item_ptr(inventory->items[index]);
            if (!item) { throw std::runtime_error("Unresolved live inventory item"); }
            rows.push_back({item->handle(), owner, attachment, index});
        }
    }
    if (const auto* creature = object->as_creature()) {
        for (size_t slot = 0; slot < creature->equipment.equips.size(); ++slot) {
            const auto& equipped = creature->equipment.equips[slot];
            if (const auto* item = equip_item_ptr(equipped)) {
                rows.push_back({item->handle(), owner, BlueprintInstanceOwner::equipment, slot});
            } else if ((equipped.is<Resref>() && equipped.as<Resref>().length())
                || (equipped.is<ObjectHandle>() && equipped.as<ObjectHandle>().type != ObjectType::invalid)) {
                throw std::runtime_error("Unresolved live equipment item");
            }
        }
    }
}

ObjectHandle live_member(const BlueprintInstanceRow& row)
{
    if (row.attachment == BlueprintInstanceOwner::area) {
        if (row.owner.type != ObjectType::area) { return ObjectHandle{}; }
        const auto* area = kernel::objects().get<Area>(row.owner);
        if (!area) { return ObjectHandle{}; }
        const auto member = [&](const auto& members) {
            return row.index < members.size() && members[row.index] ? members[row.index]->handle() : ObjectHandle{};
        };
        switch (row.object.type) {
        case ObjectType::creature:
            return member(area->creatures);
        case ObjectType::door:
            return member(area->doors);
        case ObjectType::encounter:
            return member(area->encounters);
        case ObjectType::placeable:
            return member(area->placeables);
        case ObjectType::item:
            return member(area->items);
        case ObjectType::sound:
            return member(area->sounds);
        case ObjectType::store:
            return member(area->stores);
        case ObjectType::trigger:
            return member(area->triggers);
        case ObjectType::waypoint:
            return member(area->waypoints);
        default:
            return ObjectHandle{};
        }
    }
    if (row.attachment == BlueprintInstanceOwner::equipment) {
        if (row.owner.type != ObjectType::creature) { return ObjectHandle{}; }
        const auto* creature = kernel::objects().get<Creature>(row.owner);
        const auto* item = creature && row.index < creature->equipment.equips.size()
            ? equip_item_ptr(creature->equipment.equips[row.index])
            : nullptr;
        return item ? item->handle() : ObjectHandle{};
    }
    const auto* inventory = live_inventory(row.owner, row.attachment);
    const auto* item = inventory && row.index < inventory->items.size()
        ? inventory_item_ptr(inventory->items[row.index])
        : nullptr;
    return item ? item->handle() : ObjectHandle{};
}

struct LiveInventoryGrid {
    ObjectHandle owner;
    BlueprintInstanceOwner attachment;
    std::vector<Inventory::Storage> cells;
};

std::vector<LiveInventoryGrid> replacement_inventory_grids(const LiveBlueprintUpdates& updates)
{
    std::vector<LiveInventoryGrid> result;
    absl::flat_hash_map<uint64_t, size_t> replacements;
    for (size_t index = 0; index < updates.rows.size(); ++index) {
        replacements.emplace(updates.rows[index].object.to_ull(), index);
    }
    for (const auto& row : updates.rows) {
        if (row.attachment == BlueprintInstanceOwner::area || row.attachment == BlueprintInstanceOwner::equipment) { continue; }
        if (std::any_of(result.begin(), result.end(), [&](const auto& grid) { return grid.owner == row.owner && grid.attachment == row.attachment; })) { continue; }
        const auto* inventory = live_inventory(row.owner, row.attachment);
        if (!inventory || inventory->pages() < 1 || inventory->rows() < 1 || inventory->rows() > Inventory::max_rows
            || inventory->columns() < 1 || inventory->columns() > Inventory::max_columns) {
            throw std::runtime_error("Invalid live inventory dimensions");
        }
        Inventory grid{inventory->pages(), inventory->rows(), inventory->columns()};
        for (const auto& item_row : inventory->items) {
            const auto* item = inventory_item_ptr(item_row);
            if (!item) { throw std::runtime_error("Unresolved live inventory item"); }
            auto handle = item->handle();
            if (const auto found = replacements.find(handle.to_ull()); found != replacements.end()) { handle = updates.replacements[found->second].object(); }
            const auto* layout = kernel::objects().components().find_item_layout(handle);
            const auto slot = grid.xy_to_slot(item_row.pos_x, item_row.pos_y);
            if (!layout || layout->inventory_width < 1 || layout->inventory_height < 1
                || slot.row < 0 || slot.row >= grid.rows() || slot.col < 0 || slot.col >= grid.columns()
                || !grid.check_available(slot.page, slot.row, slot.col, layout->inventory_width, layout->inventory_height)
                || !grid.insert_item(slot.page, slot.row, slot.col, layout->inventory_width, layout->inventory_height)) {
                throw std::runtime_error("Replacement items do not fit the existing inventory positions");
            }
        }
        result.push_back({row.owner, row.attachment, std::move(grid.inventory_bitset)});
    }
    return result;
}

// Native slot replacement avoids moving the outgoing item through a potentially
// full inventory. Reuse the existing effect and equipment-change callbacks.
bool replace_live_equipment(const BlueprintInstanceRow& row, ObjectHandle replacement)
{
    auto* creature = kernel::objects().get<Creature>(row.owner);
    auto* item = kernel::objects().get<Item>(replacement);
    if (!creature || !item || row.index >= creature->equipment.equips.size()) { return false; }
    const auto slot = static_cast<EquipIndex>(row.index);
    auto* previous = get_equipped_item(creature, slot);
    if (!previous) { return false; }
    Vector<smalls::Value> args{nwn1::bridge::make_object_arg(row.owner),
        nwn1::bridge::make_object_arg(previous->handle()), smalls::Value::make_int(static_cast<int32_t>(slot)), smalls::Value::make_bool(true)};
    if (!nwn1::bridge::call_nwn1_module_int("core.item", "process_item_properties", args)) { return false; }
    if (!equip_item_in_slot(creature, item, slot)) { return false; }
    args[1] = nwn1::bridge::make_object_arg(replacement);
    args[3] = smalls::Value::make_bool(false);
    if (!nwn1::bridge::call_nwn1_module_int("core.item", "process_item_properties", args)) { return false; }
    args.pop_back();
    return nwn1::bridge::call_nwn1_module_void("nwn1.item", "_dispatch_on_equip", args);
}
} // namespace

bool collect_live_blueprint_references(ObjectHandle area, Resource source, LiveBlueprintUpdates& output, std::string& error)
{
    output = {};
    error.clear();
    try {
        auto* root = area.type == ObjectType::area ? kernel::objects().get<Area>(area) : nullptr;
        if (!root || blueprint_object_type(source.type) == ObjectType::invalid) { throw std::runtime_error("Invalid live area or blueprint"); }
        output.area = area;
        output.source = source;
        std::vector<BlueprintInstanceRow> pending;
        const auto append = [&](const auto& members) {
            for (size_t index = 0; index < members.size(); ++index) {
                if (!members[index]) { throw std::runtime_error("Missing live area member"); }
                pending.push_back({members[index]->handle(), area, BlueprintInstanceOwner::area, index});
            }
        };
        append(root->creatures);
        append(root->doors);
        append(root->encounters);
        append(root->items);
        append(root->placeables);
        append(root->sounds);
        append(root->stores);
        append(root->triggers);
        append(root->waypoints);
        absl::flat_hash_set<uint64_t> visited;
        absl::flat_hash_set<uint64_t> covered;
        for (size_t index = 0; index < pending.size(); ++index) {
            const auto row = pending[index];
            const auto* object = kernel::objects().get_object_base(row.object);
            if (!object || !visited.insert(row.object.to_ull()).second) { throw std::runtime_error("Missing or repeated live object ownership"); }
            const bool parent_covered = covered.contains(row.owner.to_ull());
            const bool match = row.object.type == blueprint_object_type(source.type) && object->resref == source.resref;
            if (match) {
                if (parent_covered) {
                    ++output.covered;
                } else {
                    output.rows.push_back(row);
                }
            }
            if (match || parent_covered) { covered.insert(row.object.to_ull()); }
            append_live_children(row.object, pending);
        }
        if (!output.rows.empty()) {
            const std::array sources{source};
            if (!snapshot_blueprints(sources, output.blueprints, error)) { throw std::runtime_error(error); }
            std::vector<Resource> dependencies{source};
            for (size_t index = 0; index < dependencies.size(); ++index) {
                const auto key = dependencies[index];
                visit_children(output.blueprints.at(key), [&](const Json& child, const std::string&) {
                    dependencies.emplace_back(child.get<std::string>(), ResourceType::uti);
                });
            }
            output.expected_objects = dependencies.size();
            output.replacements.reserve(output.rows.size());
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        output = {};
        return false;
    }
}

bool prepare_live_blueprint_updates(LiveBlueprintUpdates& updates, size_t count, std::string& error)
{
    error.clear();
    try {
        if (updates.applied || updates.replacements.size() > updates.rows.size()) { throw std::runtime_error("Invalid live replacement batch"); }
        const auto end = updates.replacements.size() + std::min(count, updates.rows.size() - updates.replacements.size());
        while (updates.replacements.size() < end) {
            const auto& row = updates.rows[updates.replacements.size()];
            if (live_member(row) != row.object) { throw std::runtime_error("Live instance ownership changed during preparation"); }
            ObjectDocument replacement;
            ObjectBase* loaded = nullptr;
            switch (row.object.type) {
            case ObjectType::creature:
                loaded = kernel::objects().load<Creature>(updates.source.resref);
                break;
            case ObjectType::door:
                loaded = kernel::objects().load<Door>(updates.source.resref);
                break;
            case ObjectType::encounter:
                loaded = kernel::objects().load<Encounter>(updates.source.resref);
                break;
            case ObjectType::placeable:
                loaded = kernel::objects().load<Placeable>(updates.source.resref);
                break;
            case ObjectType::item:
                loaded = kernel::objects().load<Item>(updates.source.resref);
                break;
            case ObjectType::sound:
                loaded = kernel::objects().load<Sound>(updates.source.resref);
                break;
            case ObjectType::store:
                loaded = kernel::objects().load<Store>(updates.source.resref);
                break;
            case ObjectType::trigger:
                loaded = kernel::objects().load<Trigger>(updates.source.resref);
                break;
            case ObjectType::waypoint:
                loaded = kernel::objects().load<Waypoint>(updates.source.resref);
                break;
            default:
                break;
            }
            if (!loaded) { throw std::runtime_error("Cannot instantiate replacement blueprint"); }
            if (!replacement.adopt(loaded->handle())) {
                kernel::objects().destroy(loaded->handle());
                throw std::runtime_error("Cannot own replacement instance");
            }
            validate_owned_items(replacement.object());
            std::vector<BlueprintInstanceRow> children{{replacement.object(), ObjectHandle{}, BlueprintInstanceOwner::area, 0}};
            for (size_t index = 0; index < children.size(); ++index) {
                append_live_children(children[index].object, children);
            }
            if (children.size() != updates.expected_objects) { throw std::runtime_error("Blueprint loading dropped contained items"); }
            const auto* previous = kernel::objects().get_object_base(row.object);
            auto* next = kernel::objects().get_object_base(replacement.object());
            next->uuid = previous->uuid;
            auto& components = kernel::objects().components();
            if (const auto* spatial = components.find_spatial(row.object)) {
                // Component insertion may relocate the spatial table.
                const auto placement = *spatial;
                auto* target = components.get_or_create_spatial(replacement.object());
                target->area = placement.area;
                target->position = placement.position;
                target->orientation = placement.orientation;
                target->scale = placement.scale;
            }
            updates.replacements.push_back(std::move(replacement));
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        updates.replacements.clear();
        return false;
    }
}

bool validate_live_blueprint_updates(const LiveBlueprintUpdates& updates, std::string& error)
{
    error.clear();
    try {
        if (updates.area.type != ObjectType::area || !kernel::objects().valid(updates.area) || updates.applied || updates.rows.size() != updates.replacements.size()) { throw std::runtime_error("Live replacements are not ready"); }
        if (!updates.rows.empty()) {
            absl::flat_hash_map<Resource, Json> current;
            const std::array sources{updates.source};
            if (!snapshot_blueprints(sources, current, error)) { throw std::runtime_error(error); }
            if (current != updates.blueprints) { throw std::runtime_error("Blueprint dependencies changed during preparation"); }
        }
        for (size_t index = 0; index < updates.rows.size(); ++index) {
            const auto& row = updates.rows[index];
            const auto replacement = updates.replacements[index].object();
            if (live_member(row) != row.object || !kernel::objects().valid(replacement)) { throw std::runtime_error("Live instance ownership changed during preparation"); }
            const auto* previous = kernel::objects().get_object_base(row.object);
            if (previous->resref != updates.source.resref || previous->uuid != kernel::objects().get_object_base(replacement)->uuid) { throw std::runtime_error("Live instance identity changed during preparation"); }
            const auto* before = kernel::objects().components().find_spatial(row.object);
            const auto* after = kernel::objects().components().find_spatial(replacement);
            if (before && (!after || before->position != after->position || before->orientation != after->orientation || before->scale != after->scale || before->area != after->area)) { throw std::runtime_error("Live instance placement changed during preparation"); }
            const auto finite = [](glm::vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); };
            if (row.attachment == BlueprintInstanceOwner::area && (!before || before->area != updates.area.id || !finite(before->position) || !finite(before->orientation) || !finite(before->scale) || before->scale.x <= 0 || before->scale.y <= 0 || before->scale.z <= 0)) { throw std::runtime_error("Invalid live instance placement"); }
            if (row.attachment == BlueprintInstanceOwner::equipment && !can_place_creature_item_in_slot(replacement, static_cast<EquipIndex>(row.index))) { throw std::runtime_error("Replacement item is not valid in its equipment slot"); }
        }
        (void)replacement_inventory_grids(updates);
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

bool publish_live_blueprint_updates(LiveBlueprintUpdates& updates, ObjectHandle& selection, std::string& error)
{
    if (!validate_live_blueprint_updates(updates, error)) { return false; }
    try {
        auto grids = replacement_inventory_grids(updates);
        std::vector<size_t> equipped;
        equipped.reserve(updates.rows.size());
        for (size_t index = 0; index < updates.rows.size(); ++index) {
            const auto& row = updates.rows[index];
            if (row.attachment != BlueprintInstanceOwner::equipment) { continue; }
            equipped.push_back(index);
            if (!replace_live_equipment(row, updates.replacements[index].object())) {
                error = "Cannot refresh replacement equipment";
                for (auto it = equipped.rbegin(); it != equipped.rend(); ++it) {
                    const auto& previous = updates.rows[*it];
                    if (!replace_live_equipment(previous, previous.object)) {
                        // Even a rule callback failure must restore native ownership;
                        // detached replacement owners must never destroy equipped items.
                        equip_item_in_slot(kernel::objects().get<Creature>(previous.owner),
                            kernel::objects().get<Item>(previous.object), static_cast<EquipIndex>(previous.index));
                        error += "; equipment effect/visual rollback failed";
                    }
                }
                return false;
            }
        }
        // Every owner slot and grid is validated; the remaining swaps allocate
        // nothing. Ownership transfers before the outgoing graph is destroyed.
        for (size_t index = 0; index < updates.rows.size(); ++index) {
            const auto& row = updates.rows[index];
            const auto replacement = updates.replacements[index].object();
            if (row.attachment == BlueprintInstanceOwner::area) {
                auto* area = kernel::objects().get<Area>(row.owner);
                switch (row.object.type) {
                case ObjectType::creature:
                    area->creatures[row.index] = kernel::objects().get<Creature>(replacement);
                    break;
                case ObjectType::door:
                    area->doors[row.index] = kernel::objects().get<Door>(replacement);
                    break;
                case ObjectType::encounter:
                    area->encounters[row.index] = kernel::objects().get<Encounter>(replacement);
                    break;
                case ObjectType::placeable:
                    area->placeables[row.index] = kernel::objects().get<Placeable>(replacement);
                    break;
                case ObjectType::item:
                    area->items[row.index] = kernel::objects().get<Item>(replacement);
                    break;
                case ObjectType::sound:
                    area->sounds[row.index] = kernel::objects().get<Sound>(replacement);
                    break;
                case ObjectType::store:
                    area->stores[row.index] = kernel::objects().get<Store>(replacement);
                    break;
                case ObjectType::trigger:
                    area->triggers[row.index] = kernel::objects().get<Trigger>(replacement);
                    break;
                case ObjectType::waypoint:
                    area->waypoints[row.index] = kernel::objects().get<Waypoint>(replacement);
                    break;
                default:
                    throw std::runtime_error("Unsupported live area blueprint type");
                }
            } else if (row.attachment != BlueprintInstanceOwner::equipment) {
                live_inventory(row.owner, row.attachment)->items[row.index].item = replacement;
            }
            if (selection == row.object) { selection = replacement; }
            (void)updates.replacements[index].release();
        }
        for (auto& grid : grids) {
            live_inventory(grid.owner, grid.attachment)->inventory_bitset.swap(grid.cells);
        }
        for (const auto& row : updates.rows) {
            kernel::objects().destroy(row.object);
        }
        if (!kernel::objects().valid(selection)) { selection = ObjectHandle{}; }
        updates.applied = true;
        if (!updates.rows.empty()) { publish_area_structure_changes(updates.area, selection); }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

BlueprintReferences collect_blueprint_references(const Json& document, ResourceType::type type, Resource blueprint)
{
    BlueprintReferences result;
    try {
        auto pending = roots(document, type);
        for (size_t index = 0; index < pending.size(); ++index) {
            const auto row = pending[index];
            const auto& value = document.at(Json::json_pointer{row.path});
            const auto reference = value.at("object").at("resref").get<Resref>();
            const bool match = !row.definition && blueprint_resource_type(row.type) == blueprint.type && reference == blueprint.resref;
            if (match) { result.rows.push_back({row.path, row.owner_path, row.owner_type, row.placed, row.covered}); }
            visit_children(value, [&](const Json& child, const std::string& suffix) {
                if (child.is_string()) {
                    if (!row.definition) { throw std::runtime_error("Placed objects must contain embedded item instances"); }
                    if (blueprint.type == ResourceType::uti && child.get<Resref>() == blueprint.resref) { ++result.reference_uses; }
                } else if (child.is_object()) {
                    pending.push_back({row.path + suffix, row.owner_path, ObjectType::item, row.owner_type,
                        false, false, row.covered || match});
                } else {
                    throw std::runtime_error("Invalid item reference or instance");
                }
            });
        }
    } catch (const std::exception& ex) {
        result.error = ex.what();
        result.rows.clear();
    }
    return result;
}

bool load_blueprint_update_documents(std::span<const BlueprintUpdateDocument> documents,
    std::vector<ObjectDocument>& output, std::string& error)
{
    output.clear();
    error.clear();
    try {
        for (const auto& row : documents) {
            ObjectDocument owner;
            const auto type = blueprint_document_object_type(row.type);
            if (!load_root(type, row.value, SerializationProfile::blueprint, owner, error)) { break; }
            Json loaded;
            if (type == ObjectType::area) {
                serialize(kernel::objects().get<Area>(owner.object()), loaded);
                for (const auto& root : roots(row.value, row.type)) {
                    check_children(row.value.at(Json::json_pointer{root.path}), loaded.at(Json::json_pointer{root.path}));
                }
                for (const auto& definition : blueprint_types()) {
                    if (row.value.at(definition.area_category).size()
                        != loaded.at(definition.area_category).size()) {
                        throw std::runtime_error("Area loading dropped authored objects");
                    }
                }
            } else {
                const auto result = object_to_component_propset_json(kernel::objects().get_object_base(owner.object()), loaded, &kernel::runtime(), SerializationProfile::blueprint);
                if (!result) { throw std::runtime_error(result.error); }
                check_children(row.value, loaded);
            }
            output.push_back(std::move(owner));
        }
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    if (!error.empty()) { output.clear(); }
    return error.empty();
}

bool prepare_blueprint_updates(std::span<BlueprintUpdateDocument> documents, Resource source,
    const absl::flat_hash_map<Resource, Json>& blueprints, std::string& error)
{
    error.clear();
    try {
        const auto expanded = expanded_instance(source, blueprints);
        Json fresh;
        {
            ObjectDocument prototype;
            if (!load_root(blueprint_object_type(source.type), expanded, SerializationProfile::instance, prototype, error)) { throw std::runtime_error(error); }
            const auto result = object_to_component_propset_json(kernel::objects().get_object_base(prototype.object()), fresh, &kernel::runtime(), SerializationProfile::instance);
            if (!result) { throw std::runtime_error(result.error); }
            check_children(expanded, fresh);
            validate_owned_items(prototype.object());
        }
        for (auto& document : documents) {
            if (!document.references.error.empty()) { throw std::runtime_error(document.references.error); }
            absl::flat_hash_set<std::string> owners;
            for (const auto& row : document.references.rows) {
                if (row.covered) { continue; }
                auto& target = document.value.at(Json::json_pointer{row.path});
                Json replacement = fresh;
                if (target.at("object").contains("uuid")) { replacement["object"]["uuid"] = target["object"]["uuid"]; }
                for (const auto* field : {"location", "scale"}) {
                    if (target.at("components").contains(field)) {
                        replacement["components"][field] = target["components"][field];
                    } else {
                        replacement["components"].erase(field);
                    }
                }
                target = std::move(replacement);
                owners.insert(row.owner_path);
            }
            if (document.type == ResourceType::caf) {
                std::vector<ObjectDocument> loaded;
                if (!load_blueprint_update_documents(std::span{&document, 1}, loaded, error)) { throw std::runtime_error(error); }
                auto* area = kernel::objects().get<Area>(loaded[0].object());
                // Membership rows and generated paths share the CAF category order.
                // Validate only changed owners, reusing the loaded document graph.
                std::vector<PlacedAreaObjectRow> member_rows;
                build_placed_area_object_rows(*area, member_rows);
                const auto paths = roots(document.value, document.type);
                if (member_rows.size() != paths.size()) { throw std::runtime_error("Area membership changed during validation"); }
                for (size_t index = 0; index < paths.size(); ++index) {
                    if (owners.contains(paths[index].path)) { validate_owned_items(member_rows[index].object); }
                }
                std::vector<ObjectSpatialState> placements;
                const auto append = [&](const auto& members, const char* category) {
                    for (size_t index = 0; index < members.size(); ++index) {
                        const auto path = std::string{"/"} + category + "/" + std::to_string(index);
                        const bool changed = std::any_of(document.references.rows.begin(), document.references.rows.end(), [&](const auto& row) { return row.placed && !row.covered && row.path == path; });
                        if (changed) {
                            const auto* spatial = kernel::objects().components().find_spatial(members[index]->handle());
                            if (!spatial) { throw std::runtime_error("Missing replacement placement"); }
                            placements.push_back(*spatial);
                        }
                    }
                };
                append(area->creatures, "creatures");
                append(area->doors, "doors");
                append(area->encounters, "encounters");
                append(area->items, "items");
                append(area->placeables, "placeables");
                append(area->sounds, "sounds");
                append(area->stores, "stores");
                append(area->triggers, "triggers");
                append(area->waypoints, "waypoints");
                AreaPlacementNavigation navigation;
                const auto admitted = validate_area_placements(navigation, area->handle(), placements);
                if (!admitted.ok()) { throw std::runtime_error(admitted.diagnostic); }
            } else {
                for (const auto& row : document.references.rows) {
                    if (!owners.erase(row.owner_path)) { continue; }
                    ObjectDocument owner;
                    if (!load_root(row.owner_type, document.value.at(Json::json_pointer{row.owner_path}), SerializationProfile::instance, owner, error)) { throw std::runtime_error(error); }
                    validate_owned_items(owner.object());
                }
            }
        }
    } catch (const std::exception& ex) {
        error = ex.what();
        for (auto& document : documents) {
            document.value = nullptr;
        }
    }
    return error.empty();
}
} // namespace nw::toolset
