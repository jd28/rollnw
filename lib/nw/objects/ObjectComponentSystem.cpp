#include "ObjectComponentSystem.hpp"

#include "../kernel/Kernel.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "ObjectBase.hpp"
#include "ObjectManager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace nw {

namespace {

constexpr uint32_t object_visual_model_known_flags = static_cast<uint32_t>(ObjectVisualModelFlags::requires_wearer)
    | static_cast<uint32_t>(ObjectVisualModelFlags::hidden_in_game)
    | static_cast<uint32_t>(ObjectVisualModelFlags::hidden_in_toolset);
constexpr uint32_t object_visual_plt_color_known_mask = (1u << plt_layer_size) - 1u;

bool is_finite(glm::vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool is_finite(ObjectSpawnPoint value) noexcept
{
    return is_finite(value.position) && std::isfinite(value.orientation);
}

bool has_geometry_payload(const ObjectGeometryState& row) noexcept
{
    return !row.points.empty() || !row.spawn_points.empty() || row.highlight_height != 0.0f;
}

bool is_valid_scale(glm::vec3 value) noexcept
{
    return is_finite(value) && value.x > 0.0f && value.y > 0.0f && value.z > 0.0f;
}

bool use_handle_key(const ObjectBase& obj) noexcept
{
    return obj.handle().type != ObjectType::invalid
        && obj.handle().id != object_invalid;
}

uintptr_t ptr_key(const ObjectBase& obj) noexcept
{
    return reinterpret_cast<uintptr_t>(&obj);
}

void destroy(StoreInventory& inventory)
{
    inventory.armor.destroy();
    inventory.miscellaneous.destroy();
    inventory.potions.destroy();
    inventory.rings.destroy();
    inventory.weapons.destroy();
}

bool read_i32(const nlohmann::json& archive, const char* key, int32_t& out)
{
    auto it = archive.find(key);
    if (it == archive.end() || !it->is_number_integer()) { return false; }

    const auto value = it->get<int64_t>();
    if (value < std::numeric_limits<int32_t>::min()
        || value > std::numeric_limits<int32_t>::max()) {
        return false;
    }

    out = static_cast<int32_t>(value);
    return true;
}

bool read_u32(const nlohmann::json& archive, const char* key, uint32_t& out)
{
    auto it = archive.find(key);
    if (it == archive.end() || !it->is_number_integer()) { return false; }

    const auto value = it->get<int64_t>();
    if (value < 0 || value > std::numeric_limits<uint32_t>::max()) { return false; }

    out = static_cast<uint32_t>(value);
    return true;
}

bool is_valid_ability_loadout_entry(const ObjectAbilityLoadoutEntry& entry) noexcept
{
    return entry.source >= 0
        && entry.tier >= 0
        && entry.slot >= -1
        && entry.ability >= -1
        && entry.modifier >= 0;
}

bool same_unslotted_ability(const ObjectAbilityLoadoutEntry& lhs,
    const ObjectAbilityLoadoutEntry& rhs) noexcept
{
    return lhs.slot == -1
        && rhs.slot == -1
        && lhs.source == rhs.source
        && lhs.tier == rhs.tier
        && lhs.ability == rhs.ability;
}

bool same_slotted_ability_slot(const ObjectAbilityLoadoutEntry& lhs,
    const ObjectAbilityLoadoutEntry& rhs) noexcept
{
    return lhs.slot >= 0
        && rhs.slot >= 0
        && lhs.source == rhs.source
        && lhs.tier == rhs.tier
        && lhs.slot == rhs.slot;
}

bool duplicates_existing_ability_entry(const Vector<ObjectAbilityLoadoutEntry>& entries,
    const ObjectAbilityLoadoutEntry& entry) noexcept
{
    for (const ObjectAbilityLoadoutEntry& existing : entries) {
        if (same_unslotted_ability(existing, entry)
            || same_slotted_ability_slot(existing, entry)) {
            return true;
        }
    }
    return false;
}

void sort_ability_loadout(ObjectAbilityLoadoutState& row)
{
    std::sort(row.entries.begin(), row.entries.end(),
        [](const ObjectAbilityLoadoutEntry& lhs, const ObjectAbilityLoadoutEntry& rhs) {
            if (lhs.source != rhs.source) { return lhs.source < rhs.source; }
            if (lhs.tier != rhs.tier) { return lhs.tier < rhs.tier; }
            if (lhs.slot != rhs.slot) { return lhs.slot < rhs.slot; }
            if (lhs.ability != rhs.ability) { return lhs.ability < rhs.ability; }
            if (lhs.modifier != rhs.modifier) { return lhs.modifier < rhs.modifier; }
            return lhs.flags < rhs.flags;
        });
}

} // namespace

bool object_visual_model_flags_valid(uint32_t flags) noexcept
{
    return (flags & ~object_visual_model_known_flags) == 0;
}

bool object_visual_plt_color_mask_valid(uint32_t mask) noexcept
{
    return (mask & ~object_visual_plt_color_known_mask) == 0;
}

bool object_visual_model_visible_in_mode(const ObjectVisualModel& model, ObjectVisualRenderMode mode) noexcept
{
    switch (mode) {
    case ObjectVisualRenderMode::game:
        return (model.flags & ObjectVisualModelFlags::hidden_in_game) == 0;
    case ObjectVisualRenderMode::toolset:
        return (model.flags & ObjectVisualModelFlags::hidden_in_toolset) == 0;
    }
    return true;
}

ObjectSpatialState* ObjectComponentSystem::get_or_create_spatial(ObjectHandle obj)
{
    if (auto* existing = find_spatial(obj)) {
        return existing;
    }

    auto* base = kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }

    const uint64_t key = obj.to_ull();
    auto index = spatial_index_.emplace(key, spatial_.size()).first;
    spatial_.push_back(ObjectSpatialState{.owner = obj});
    return &spatial_[index->second];
}

ObjectSpatialState* ObjectComponentSystem::find_spatial(ObjectHandle obj) noexcept
{
    auto it = spatial_index_.find(obj.to_ull());
    return it != spatial_index_.end() ? &spatial_[it->second] : nullptr;
}

const ObjectSpatialState* ObjectComponentSystem::find_spatial(ObjectHandle obj) const noexcept
{
    auto it = spatial_index_.find(obj.to_ull());
    return it != spatial_index_.end() ? &spatial_[it->second] : nullptr;
}

Location ObjectComponentSystem::location(ObjectHandle obj) const noexcept
{
    Location result;
    if (const ObjectSpatialState* row = find_spatial(obj)) {
        result.area = row->area;
        result.position = row->position;
        result.orientation = row->orientation;
    }
    return result;
}

bool ObjectComponentSystem::deserialize_spatial(ObjectHandle obj, const GffStruct& archive, SerializationProfile profile)
{
    if (profile != SerializationProfile::any
        && profile != SerializationProfile::instance) {
        remove_spatial(obj);
        return true;
    }

    Location parsed;
    if (!deserialize(parsed, archive, profile)) {
        remove_spatial(obj);
        return true;
    }

    return set_location(obj, parsed);
}

bool ObjectComponentSystem::from_json_spatial(ObjectHandle obj, const nlohmann::json& component_archive, SerializationProfile profile)
{
    if (profile != SerializationProfile::instance
        && profile != SerializationProfile::savegame) {
        remove_spatial(obj);
        return true;
    }

    auto it = component_archive.find("location");
    if (it == component_archive.end()) {
        return false;
    }

    Location parsed = it->get<Location>();
    return set_location(obj, parsed);
}

bool ObjectComponentSystem::serialize_position(ObjectHandle obj, GffBuilderStruct& archive, SerializationProfile profile) const
{
    if (profile == SerializationProfile::blueprint) {
        return true;
    }

    const Location loc = location(obj);
    archive.add_field("PositionX", loc.position.x)
        .add_field("PositionY", loc.position.y)
        .add_field("PositionZ", loc.position.z);
    return true;
}

bool ObjectComponentSystem::serialize_position_orientation(ObjectHandle obj, GffBuilderStruct& archive, SerializationProfile profile) const
{
    if (!serialize_position(obj, archive, profile)) {
        return false;
    }

    if (profile == SerializationProfile::blueprint) {
        return true;
    }

    const Location loc = location(obj);
    archive.add_field("OrientationX", loc.orientation.x)
        .add_field("OrientationY", loc.orientation.y);
    return true;
}

void ObjectComponentSystem::to_json_spatial(ObjectHandle obj, nlohmann::json& component_archive, SerializationProfile profile) const
{
    if (profile == SerializationProfile::instance
        || profile == SerializationProfile::savegame) {
        component_archive["location"] = location(obj);
    }
}

bool ObjectComponentSystem::set_location(ObjectHandle obj, Location location)
{
    ObjectSpatialState* row = get_or_create_spatial(obj);
    if (!row) {
        return false;
    }

    row->area = location.area;
    row->position = location.position;
    row->orientation = location.orientation;
    return true;
}

bool ObjectComponentSystem::set_area(ObjectHandle obj, ObjectID area)
{
    ObjectSpatialState* row = get_or_create_spatial(obj);
    if (!row) {
        return false;
    }

    row->area = area;
    return true;
}

LocalData* ObjectComponentSystem::get_or_create_locals(ObjectHandle obj)
{
    if (auto* existing = find_locals(obj)) {
        return existing;
    }

    if (!kernel::objects().get_object_base(obj)) {
        return nullptr;
    }

    const uint64_t key = obj.to_ull();
    auto index = local_data_index_.emplace(key, local_data_.size()).first;
    local_data_.push_back(ObjectLocalDataState{.owner = obj});
    return &local_data_[index->second].locals;
}

LocalData* ObjectComponentSystem::find_locals(ObjectHandle obj) noexcept
{
    auto it = local_data_index_.find(obj.to_ull());
    return it != local_data_index_.end() ? &local_data_[it->second].locals : nullptr;
}

const LocalData* ObjectComponentSystem::find_locals(ObjectHandle obj) const noexcept
{
    auto it = local_data_index_.find(obj.to_ull());
    return it != local_data_index_.end() ? &local_data_[it->second].locals : nullptr;
}

bool ObjectComponentSystem::deserialize_locals(ObjectHandle obj, const GffStruct& archive)
{
    if (!archive["VarTable"].valid()) {
        remove_locals(obj);
        return true;
    }

    LocalData parsed;
    if (!deserialize(parsed, archive)) {
        return false;
    }

    if (!parsed.size()) {
        remove_locals(obj);
        return true;
    }

    LocalData* row = get_or_create_locals(obj);
    if (!row) {
        return false;
    }

    *row = std::move(parsed);
    return true;
}

bool ObjectComponentSystem::from_json_locals(ObjectHandle obj, const nlohmann::json& component_archive)
{
    auto it = component_archive.find("locals");
    if (it == component_archive.end()) {
        remove_locals(obj);
        return true;
    }

    LocalData parsed;
    if (!parsed.from_json(*it)) {
        return false;
    }

    if (!parsed.size()) {
        remove_locals(obj);
        return true;
    }

    LocalData* row = get_or_create_locals(obj);
    if (!row) {
        return false;
    }

    *row = std::move(parsed);
    return true;
}

bool ObjectComponentSystem::serialize_locals(ObjectHandle obj, GffBuilderStruct& archive, SerializationProfile profile) const
{
    const LocalData* row = find_locals(obj);
    return !row || !row->size() || serialize(*row, archive, profile);
}

void ObjectComponentSystem::to_json_locals(ObjectHandle obj, nlohmann::json& component_archive, SerializationProfile profile) const
{
    if (const LocalData* row = find_locals(obj)) {
        component_archive["locals"] = row->to_json(profile);
    } else {
        component_archive["locals"] = nlohmann::json::object();
    }
}

ObjectVitalsState* ObjectComponentSystem::get_or_create_vitals(ObjectHandle obj)
{
    if (auto* existing = find_vitals(obj)) {
        return existing;
    }

    auto* base = kernel::objects().get_object_base(obj);
    if (!base || !base->as_creature()) {
        return nullptr;
    }

    const uint64_t key = obj.to_ull();
    auto index = vitals_index_.emplace(key, vitals_.size()).first;
    vitals_.push_back(ObjectVitalsState{.owner = obj});
    return &vitals_[index->second];
}

ObjectVitalsState* ObjectComponentSystem::find_vitals(ObjectHandle obj) noexcept
{
    auto it = vitals_index_.find(obj.to_ull());
    return it != vitals_index_.end() ? &vitals_[it->second] : nullptr;
}

const ObjectVitalsState* ObjectComponentSystem::find_vitals(ObjectHandle obj) const noexcept
{
    auto it = vitals_index_.find(obj.to_ull());
    return it != vitals_index_.end() ? &vitals_[it->second] : nullptr;
}

bool ObjectComponentSystem::set_vitals(ObjectHandle obj, int32_t hp_current, int32_t hp_max)
{
    if (hp_max <= 0) {
        return false;
    }

    ObjectVitalsState* row = get_or_create_vitals(obj);
    if (!row) {
        return false;
    }

    row->hp_current = hp_current;
    row->hp_max = hp_max;
    return true;
}

ObjectGeometryState* ObjectComponentSystem::get_or_create_geometry(ObjectHandle obj)
{
    if (auto* existing = find_geometry(obj)) {
        return existing;
    }

    if (!kernel::objects().get_object_base(obj)) {
        return nullptr;
    }

    const uint64_t key = obj.to_ull();
    auto index = geometry_index_.emplace(key, geometry_.size()).first;
    geometry_.push_back(ObjectGeometryState{.owner = obj});
    return &geometry_[index->second];
}

ObjectGeometryState* ObjectComponentSystem::find_geometry(ObjectHandle obj) noexcept
{
    auto it = geometry_index_.find(obj.to_ull());
    return it != geometry_index_.end() ? &geometry_[it->second] : nullptr;
}

const ObjectGeometryState* ObjectComponentSystem::find_geometry(ObjectHandle obj) const noexcept
{
    auto it = geometry_index_.find(obj.to_ull());
    return it != geometry_index_.end() ? &geometry_[it->second] : nullptr;
}

bool ObjectComponentSystem::set_geometry(ObjectHandle obj, std::span<const glm::vec3> points)
{
    for (const glm::vec3 point : points) {
        if (!is_finite(point)) {
            return false;
        }
    }

    ObjectGeometryState* row = find_geometry(obj);
    if (points.empty()) {
        if (!row) {
            return true;
        }
        row->points.clear();
        if (!has_geometry_payload(*row)) {
            remove_geometry(obj);
        }
        return true;
    }

    if (!row) {
        row = get_or_create_geometry(obj);
    }
    if (!row) {
        return false;
    }

    row->points.assign(points.begin(), points.end());
    return true;
}

bool ObjectComponentSystem::set_spawn_points(ObjectHandle obj, std::span<const ObjectSpawnPoint> spawn_points)
{
    for (const ObjectSpawnPoint point : spawn_points) {
        if (!is_finite(point)) {
            return false;
        }
    }

    ObjectGeometryState* row = find_geometry(obj);
    if (spawn_points.empty()) {
        if (!row) {
            return true;
        }
        row->spawn_points.clear();
        if (!has_geometry_payload(*row)) {
            remove_geometry(obj);
        }
        return true;
    }

    if (!row) {
        row = get_or_create_geometry(obj);
    }
    if (!row) {
        return false;
    }

    row->spawn_points.assign(spawn_points.begin(), spawn_points.end());
    return true;
}

bool ObjectComponentSystem::set_highlight_height(ObjectHandle obj, float value)
{
    if (!std::isfinite(value)) { return false; }

    ObjectGeometryState* row = find_geometry(obj);
    if (value == 0.0f) {
        if (!row) { return true; }
        row->highlight_height = 0.0f;
        if (!has_geometry_payload(*row)) {
            remove_geometry(obj);
        }
        return true;
    }

    if (!row) {
        row = get_or_create_geometry(obj);
    }
    if (!row) { return false; }

    row->highlight_height = value;
    return true;
}

ObjectVisualState* ObjectComponentSystem::get_or_create_visual(ObjectHandle obj)
{
    if (auto* existing = find_visual(obj)) {
        return existing;
    }

    if (!kernel::objects().get_object_base(obj)) {
        return nullptr;
    }

    const uint64_t key = obj.to_ull();
    auto index = visual_index_.emplace(key, visual_.size()).first;
    visual_.push_back(ObjectVisualState{.owner = obj});
    return &visual_[index->second];
}

ObjectVisualState* ObjectComponentSystem::find_visual(ObjectHandle obj) noexcept
{
    auto it = visual_index_.find(obj.to_ull());
    return it != visual_index_.end() ? &visual_[it->second] : nullptr;
}

const ObjectVisualState* ObjectComponentSystem::find_visual(ObjectHandle obj) const noexcept
{
    auto it = visual_index_.find(obj.to_ull());
    return it != visual_index_.end() ? &visual_[it->second] : nullptr;
}

bool ObjectComponentSystem::clear_visual(ObjectHandle obj, int32_t appearance)
{
    ObjectVisualState* row = get_or_create_visual(obj);
    if (!row) {
        return false;
    }

    row->appearance = appearance;
    row->body_variant = 0;
    row->base_plt_colors = {};
    row->base_plt_color_mask = 0;
    row->models.clear();
    row->lights.clear();
    return true;
}

bool ObjectComponentSystem::clear_visual_slot(ObjectHandle obj, int32_t slot)
{
    if (slot < 0) { return false; }

    ObjectVisualState* row = get_or_create_visual(obj);
    if (!row) {
        return false;
    }

    auto tail = std::remove_if(row->models.begin(), row->models.end(),
        [slot](const ObjectVisualModel& model) {
            return model.slot == slot;
        });
    row->models.erase(tail, row->models.end());
    return true;
}

bool ObjectComponentSystem::set_visual_body_variant(ObjectHandle obj, int32_t body_variant)
{
    if (body_variant < 0) { return false; }

    ObjectVisualState* row = get_or_create_visual(obj);
    if (!row) {
        return false;
    }

    row->body_variant = body_variant;
    return true;
}

bool ObjectComponentSystem::set_visual_base_plt_colors(ObjectHandle obj, uint32_t mask, PltColors colors)
{
    if (!object_visual_plt_color_mask_valid(mask)) {
        return false;
    }

    ObjectVisualState* row = get_or_create_visual(obj);
    if (!row) {
        return false;
    }

    row->base_plt_colors = colors;
    row->base_plt_color_mask = mask;
    return true;
}

bool ObjectComponentSystem::add_visual_model(ObjectHandle obj, ObjectVisualModel model)
{
    if (model.model.empty()
        || model.slot < -1
        || !object_visual_model_flags_valid(model.flags)
        || !object_visual_plt_color_mask_valid(model.plt_color_mask)) {
        return false;
    }

    ObjectVisualState* row = get_or_create_visual(obj);
    if (!row) {
        return false;
    }

    row->models.push_back(model);
    return true;
}

bool ObjectComponentSystem::add_visual_model_row(ObjectHandle obj, ObjectVisualModel model)
{
    if (model.model.empty()
        || model.slot < -1
        || model.source_part < 0
        || model.model_part < 0
        || !object_visual_model_flags_valid(model.flags)
        || !object_visual_plt_color_mask_valid(model.plt_color_mask)) {
        return false;
    }

    ObjectVisualState* row = get_or_create_visual(obj);
    if (!row) {
        return false;
    }

    row->models.push_back(model);
    return true;
}

bool ObjectComponentSystem::add_visual_part(ObjectHandle obj, ObjectVisualModel model)
{
    if (model.slot < 0
        || model.source_part < 0
        || model.model_part < 0
        || !object_visual_model_flags_valid(model.flags)
        || !object_visual_plt_color_mask_valid(model.plt_color_mask)) {
        return false;
    }

    ObjectVisualState* row = get_or_create_visual(obj);
    if (!row) {
        return false;
    }

    row->models.push_back(model);
    return true;
}

bool ObjectComponentSystem::set_visual_plt_colors(ObjectHandle obj, int32_t slot, int32_t kind, int32_t part,
    uint32_t mask, PltColors colors)
{
    if (slot < -1 || !object_visual_plt_color_mask_valid(mask)) {
        return false;
    }

    ObjectVisualState* row = find_visual(obj);
    if (!row) {
        return false;
    }

    for (auto it = row->models.rbegin(); it != row->models.rend(); ++it) {
        if (it->slot == slot && it->kind == kind && it->part == part) {
            it->plt_colors = colors;
            it->plt_color_mask = mask;
            return true;
        }
    }
    return false;
}

bool ObjectComponentSystem::add_visual_light(ObjectHandle obj, int32_t light_color, glm::vec3 light_offset)
{
    if (light_color < 0 || !is_finite(light_offset)) {
        return false;
    }

    ObjectVisualState* row = get_or_create_visual(obj);
    if (!row) {
        return false;
    }

    row->lights.push_back(ObjectVisualLight{
        .light_offset = light_offset,
        .light_color = light_color,
    });
    return true;
}

ObjectItemLayoutState* ObjectComponentSystem::get_or_create_item_layout(ObjectHandle obj)
{
    if (auto* existing = find_item_layout(obj)) {
        return existing;
    }

    auto* base = kernel::objects().get_object_base(obj);
    if (!base || !base->as_item()) {
        return nullptr;
    }

    const uint64_t key = obj.to_ull();
    auto index = item_layout_index_.emplace(key, item_layouts_.size()).first;
    item_layouts_.push_back(ObjectItemLayoutState{.owner = obj});
    return &item_layouts_[index->second];
}

ObjectItemLayoutState* ObjectComponentSystem::find_item_layout(ObjectHandle obj) noexcept
{
    auto it = item_layout_index_.find(obj.to_ull());
    return it != item_layout_index_.end() ? &item_layouts_[it->second] : nullptr;
}

const ObjectItemLayoutState* ObjectComponentSystem::find_item_layout(ObjectHandle obj) const noexcept
{
    auto it = item_layout_index_.find(obj.to_ull());
    return it != item_layout_index_.end() ? &item_layouts_[it->second] : nullptr;
}

bool ObjectComponentSystem::set_item_layout(ObjectHandle obj,
    int32_t inventory_width, int32_t inventory_height)
{
    if (inventory_width <= 0 || inventory_height <= 0) {
        return false;
    }

    ObjectItemLayoutState* row = get_or_create_item_layout(obj);
    if (!row) {
        return false;
    }

    row->inventory_width = inventory_width;
    row->inventory_height = inventory_height;
    return true;
}

Inventory* ObjectComponentSystem::get_or_create_inventory(ObjectBase& obj, int pages, int rows, int columns)
{
    if (auto* existing = find_inventory(obj)) {
        existing->owner = &obj;
        return existing;
    }

    const size_t index = inventory_.size();
    inventory_.push_back(std::make_unique<ObjectInventoryState>(ObjectInventoryState{
        .owner = obj.handle(),
        .owner_ptr = &obj,
        .inventory = Inventory{pages, rows, columns, &obj},
    }));

    if (use_handle_key(obj)) {
        inventory_handle_index_.emplace(obj.handle().to_ull(), index);
    } else {
        inventory_ptr_index_.emplace(ptr_key(obj), index);
    }

    return &inventory_[index]->inventory;
}

Inventory* ObjectComponentSystem::find_inventory(ObjectBase& obj) noexcept
{
    if (use_handle_key(obj)) {
        const uint64_t key = obj.handle().to_ull();
        auto it = inventory_handle_index_.find(key);
        if (it != inventory_handle_index_.end()) {
            return &inventory_[it->second]->inventory;
        }

        auto ptr_it = inventory_ptr_index_.find(ptr_key(obj));
        if (ptr_it == inventory_ptr_index_.end()) {
            return nullptr;
        }

        const size_t index = ptr_it->second;
        inventory_ptr_index_.erase(ptr_it);
        inventory_handle_index_.emplace(key, index);

        ObjectInventoryState& row = *inventory_[index];
        row.owner = obj.handle();
        row.owner_ptr = &obj;
        row.inventory.owner = &obj;
        return &row.inventory;
    }

    auto it = inventory_ptr_index_.find(ptr_key(obj));
    return it != inventory_ptr_index_.end() ? &inventory_[it->second]->inventory : nullptr;
}

const Inventory* ObjectComponentSystem::find_inventory(const ObjectBase& obj) const noexcept
{
    if (use_handle_key(obj)) {
        auto it = inventory_handle_index_.find(obj.handle().to_ull());
        if (it != inventory_handle_index_.end()) {
            return &inventory_[it->second]->inventory;
        }

        auto ptr_it = inventory_ptr_index_.find(ptr_key(obj));
        return ptr_it != inventory_ptr_index_.end() ? &inventory_[ptr_it->second]->inventory : nullptr;
    }

    auto it = inventory_ptr_index_.find(ptr_key(obj));
    return it != inventory_ptr_index_.end() ? &inventory_[it->second]->inventory : nullptr;
}

StoreInventory* ObjectComponentSystem::get_or_create_store_inventory(ObjectBase& obj)
{
    if (auto* existing = find_store_inventory(obj)) {
        existing->set_owner(&obj);
        return existing;
    }

    const size_t index = store_inventory_.size();
    store_inventory_.push_back(std::make_unique<ObjectStoreInventoryState>(ObjectStoreInventoryState{
        .owner = obj.handle(),
        .owner_ptr = &obj,
        .inventory = StoreInventory{&obj, nw::kernel::global_allocator()},
    }));

    if (use_handle_key(obj)) {
        store_inventory_handle_index_.emplace(obj.handle().to_ull(), index);
    } else {
        store_inventory_ptr_index_.emplace(ptr_key(obj), index);
    }

    return &store_inventory_[index]->inventory;
}

StoreInventory* ObjectComponentSystem::find_store_inventory(ObjectBase& obj) noexcept
{
    if (use_handle_key(obj)) {
        const uint64_t key = obj.handle().to_ull();
        auto it = store_inventory_handle_index_.find(key);
        if (it != store_inventory_handle_index_.end()) {
            return &store_inventory_[it->second]->inventory;
        }

        auto ptr_it = store_inventory_ptr_index_.find(ptr_key(obj));
        if (ptr_it == store_inventory_ptr_index_.end()) {
            return nullptr;
        }

        const size_t index = ptr_it->second;
        store_inventory_ptr_index_.erase(ptr_it);
        store_inventory_handle_index_.emplace(key, index);

        ObjectStoreInventoryState& row = *store_inventory_[index];
        row.owner = obj.handle();
        row.owner_ptr = &obj;
        row.inventory.set_owner(&obj);
        return &row.inventory;
    }

    auto it = store_inventory_ptr_index_.find(ptr_key(obj));
    return it != store_inventory_ptr_index_.end() ? &store_inventory_[it->second]->inventory : nullptr;
}

const StoreInventory* ObjectComponentSystem::find_store_inventory(const ObjectBase& obj) const noexcept
{
    if (use_handle_key(obj)) {
        auto it = store_inventory_handle_index_.find(obj.handle().to_ull());
        if (it != store_inventory_handle_index_.end()) {
            return &store_inventory_[it->second]->inventory;
        }

        auto ptr_it = store_inventory_ptr_index_.find(ptr_key(obj));
        return ptr_it != store_inventory_ptr_index_.end() ? &store_inventory_[ptr_it->second]->inventory : nullptr;
    }

    auto it = store_inventory_ptr_index_.find(ptr_key(obj));
    return it != store_inventory_ptr_index_.end() ? &store_inventory_[it->second]->inventory : nullptr;
}

ObjectAbilityLoadoutState* ObjectComponentSystem::get_or_create_ability_loadout(ObjectHandle obj)
{
    if (auto* existing = find_ability_loadout(obj)) {
        return existing;
    }

    if (!kernel::objects().get_object_base(obj)) {
        return nullptr;
    }

    const uint64_t key = obj.to_ull();
    auto index = ability_loadout_index_.emplace(key, ability_loadouts_.size()).first;
    ability_loadouts_.push_back(ObjectAbilityLoadoutState{.owner = obj});
    return &ability_loadouts_[index->second];
}

ObjectAbilityLoadoutState* ObjectComponentSystem::find_ability_loadout(ObjectHandle obj) noexcept
{
    auto it = ability_loadout_index_.find(obj.to_ull());
    return it != ability_loadout_index_.end() ? &ability_loadouts_[it->second] : nullptr;
}

const ObjectAbilityLoadoutState* ObjectComponentSystem::find_ability_loadout(ObjectHandle obj) const noexcept
{
    auto it = ability_loadout_index_.find(obj.to_ull());
    return it != ability_loadout_index_.end() ? &ability_loadouts_[it->second] : nullptr;
}

bool ObjectComponentSystem::from_json_ability_loadout(ObjectHandle obj, const nlohmann::json& archive)
{
    if (!archive.is_array()) { return false; }

    Vector<ObjectAbilityLoadoutEntry> parsed;
    parsed.reserve(archive.size());
    for (const auto& entry_json : archive) {
        if (!entry_json.is_object()) { return false; }

        ObjectAbilityLoadoutEntry entry;
        if (!read_i32(entry_json, "source", entry.source)
            || !read_i32(entry_json, "tier", entry.tier)
            || !read_i32(entry_json, "slot", entry.slot)
            || !read_i32(entry_json, "ability", entry.ability)
            || !read_i32(entry_json, "modifier", entry.modifier)
            || !read_u32(entry_json, "flags", entry.flags)
            || !is_valid_ability_loadout_entry(entry)
            || duplicates_existing_ability_entry(parsed, entry)) {
            return false;
        }

        parsed.push_back(entry);
    }

    if (parsed.empty()) {
        remove_ability_loadout(obj);
        return true;
    }

    ObjectAbilityLoadoutState* row = get_or_create_ability_loadout(obj);
    if (!row) { return false; }

    row->entries = std::move(parsed);
    sort_ability_loadout(*row);
    return true;
}

nlohmann::json ObjectComponentSystem::ability_loadout_to_json(ObjectHandle obj) const
{
    nlohmann::json result = nlohmann::json::array();
    const ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return result; }

    for (const ObjectAbilityLoadoutEntry& entry : row->entries) {
        result.push_back({
            {"source", entry.source},
            {"tier", entry.tier},
            {"slot", entry.slot},
            {"ability", entry.ability},
            {"modifier", entry.modifier},
            {"flags", entry.flags},
        });
    }

    return result;
}

bool ObjectComponentSystem::add_unslotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t ability)
{
    ObjectAbilityLoadoutEntry entry{
        .source = source,
        .tier = tier,
        .slot = -1,
        .ability = ability,
    };
    if (!is_valid_ability_loadout_entry(entry) || ability < 0) { return false; }

    ObjectAbilityLoadoutState* row = get_or_create_ability_loadout(obj);
    if (!row || duplicates_existing_ability_entry(row->entries, entry)) { return false; }

    row->entries.push_back(entry);
    sort_ability_loadout(*row);
    return true;
}

void ObjectComponentSystem::remove_unslotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t ability)
{
    ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return; }

    auto it = std::remove_if(row->entries.begin(), row->entries.end(),
        [source, tier, ability](const ObjectAbilityLoadoutEntry& entry) {
            return entry.slot == -1
                && entry.source == source
                && entry.tier == tier
                && entry.ability == ability;
        });
    row->entries.erase(it, row->entries.end());

    if (row->entries.empty()) {
        remove_ability_loadout(obj);
    }
}

bool ObjectComponentSystem::has_unslotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t ability) const
{
    const ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return false; }

    for (const ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot == -1
            && entry.source == source
            && entry.tier == tier
            && entry.ability == ability) {
            return true;
        }
    }
    return false;
}

size_t ObjectComponentSystem::unslotted_ability_count(ObjectHandle obj, int32_t source, int32_t tier) const
{
    const ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return 0; }

    size_t result = 0;
    for (const ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot == -1
            && entry.source == source
            && (tier < 0 || entry.tier == tier)) {
            ++result;
        }
    }
    return result;
}

bool ObjectComponentSystem::set_slotted_ability_count(ObjectHandle obj, int32_t source, int32_t tier, size_t count)
{
    if (source < 0 || tier < 0 || count > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return false;
    }

    ObjectAbilityLoadoutState* row = get_or_create_ability_loadout(obj);
    if (!row) { return false; }

    auto it = std::remove_if(row->entries.begin(), row->entries.end(),
        [source, tier, count](const ObjectAbilityLoadoutEntry& entry) {
            return entry.slot >= 0
                && entry.source == source
                && entry.tier == tier
                && static_cast<size_t>(entry.slot) >= count;
        });
    row->entries.erase(it, row->entries.end());

    for (size_t slot = 0; slot < count; ++slot) {
        bool found = false;
        for (const ObjectAbilityLoadoutEntry& entry : row->entries) {
            if (entry.slot == static_cast<int32_t>(slot)
                && entry.source == source
                && entry.tier == tier) {
                found = true;
                break;
            }
        }

        if (!found) {
            row->entries.push_back(ObjectAbilityLoadoutEntry{
                .source = source,
                .tier = tier,
                .slot = static_cast<int32_t>(slot),
            });
        }
    }

    sort_ability_loadout(*row);
    if (row->entries.empty()) {
        remove_ability_loadout(obj);
    }
    return true;
}

int ObjectComponentSystem::available_ability_slots(ObjectHandle obj, int32_t source, int32_t tier) const
{
    const ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return 0; }

    int result = 0;
    for (const ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot >= 0
            && entry.source == source
            && entry.tier == tier
            && entry.ability < 0) {
            ++result;
        }
    }
    return result;
}

int ObjectComponentSystem::first_empty_ability_slot(ObjectHandle obj, int32_t source, int32_t tier) const
{
    const ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return -1; }

    for (const ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot >= 0
            && entry.source == source
            && entry.tier == tier
            && entry.ability < 0) {
            return entry.slot;
        }
    }
    return -1;
}

bool ObjectComponentSystem::set_slotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t slot,
    int32_t ability, int32_t modifier, uint32_t flags)
{
    if (source < 0 || tier < 0 || slot < 0 || ability < 0 || modifier < 0) { return false; }

    ObjectAbilityLoadoutState* row = get_or_create_ability_loadout(obj);
    if (!row) { return false; }

    for (ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot == slot
            && entry.source == source
            && entry.tier == tier) {
            entry.ability = ability;
            entry.modifier = modifier;
            entry.flags = flags;
            sort_ability_loadout(*row);
            return true;
        }
    }

    row->entries.push_back(ObjectAbilityLoadoutEntry{
        .source = source,
        .tier = tier,
        .slot = slot,
        .ability = ability,
        .modifier = modifier,
        .flags = flags,
    });
    sort_ability_loadout(*row);
    return true;
}

bool ObjectComponentSystem::add_slotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t ability,
    int32_t modifier, uint32_t flags)
{
    if (source < 0 || tier < 0 || ability < 0 || modifier < 0) { return false; }

    const int slot = first_empty_ability_slot(obj, source, tier);
    if (slot < 0) { return false; }

    ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return false; }

    for (ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot == slot
            && entry.source == source
            && entry.tier == tier) {
            entry.ability = ability;
            entry.modifier = modifier;
            entry.flags = flags;
            sort_ability_loadout(*row);
            return true;
        }
    }
    return false;
}

void ObjectComponentSystem::clear_slotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t slot)
{
    ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return; }

    for (ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot == slot
            && entry.source == source
            && entry.tier == tier) {
            entry.ability = -1;
            entry.modifier = 0;
            entry.flags = 0;
            return;
        }
    }
}

void ObjectComponentSystem::clear_slotted_ability_from_tier(ObjectHandle obj, int32_t source, int32_t min_tier, int32_t ability)
{
    ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return; }

    for (ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot >= 0
            && entry.source == source
            && entry.tier >= min_tier
            && entry.ability == ability) {
            entry.ability = -1;
            entry.modifier = 0;
            entry.flags = 0;
        }
    }
}

int ObjectComponentSystem::find_slotted_ability_slot(ObjectHandle obj, int32_t source, int32_t tier, int32_t ability,
    int32_t modifier) const
{
    const ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return -1; }

    for (const ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot >= 0
            && entry.source == source
            && entry.tier == tier
            && entry.ability == ability
            && (modifier < 0 || entry.modifier == modifier)) {
            return entry.slot;
        }
    }
    return -1;
}

int ObjectComponentSystem::count_slotted_ability(ObjectHandle obj, int32_t source, int32_t ability, int32_t modifier) const
{
    const ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return 0; }

    int result = 0;
    for (const ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot >= 0
            && entry.source == source
            && entry.ability == ability
            && (modifier < 0 || entry.modifier == modifier)) {
            ++result;
        }
    }
    return result;
}

size_t ObjectComponentSystem::slotted_ability_count(ObjectHandle obj, int32_t source) const
{
    const ObjectAbilityLoadoutState* row = find_ability_loadout(obj);
    if (!row) { return 0; }

    size_t result = 0;
    for (const ObjectAbilityLoadoutEntry& entry : row->entries) {
        if (entry.slot >= 0 && entry.source == source) {
            ++result;
        }
    }
    return result;
}

bool ObjectComponentSystem::set_position(ObjectHandle obj, glm::vec3 position)
{
    if (!is_finite(position)) {
        return false;
    }

    ObjectSpatialState* row = get_or_create_spatial(obj);
    if (!row) {
        return false;
    }

    row->position = position;
    return true;
}

bool ObjectComponentSystem::set_orientation(ObjectHandle obj, glm::vec3 orientation)
{
    if (!is_finite(orientation)) {
        return false;
    }

    ObjectSpatialState* row = get_or_create_spatial(obj);
    if (!row) {
        return false;
    }

    row->orientation = orientation;
    return true;
}

bool ObjectComponentSystem::set_scale(ObjectHandle obj, glm::vec3 scale)
{
    if (!is_valid_scale(scale)) {
        return false;
    }

    ObjectSpatialState* row = get_or_create_spatial(obj);
    if (!row) {
        return false;
    }

    row->scale = scale;
    return true;
}

bool ObjectComponentSystem::set_velocity(ObjectHandle obj, glm::vec3 velocity)
{
    if (!is_finite(velocity)) {
        return false;
    }

    ObjectSpatialState* row = get_or_create_spatial(obj);
    if (!row) {
        return false;
    }

    row->velocity = velocity;
    return true;
}

bool ObjectComponentSystem::set_angular_velocity(ObjectHandle obj, glm::vec3 velocity)
{
    if (!is_finite(velocity)) {
        return false;
    }

    ObjectSpatialState* row = get_or_create_spatial(obj);
    if (!row) {
        return false;
    }

    row->angular_velocity = velocity;
    return true;
}

void ObjectComponentSystem::remove(ObjectHandle obj) noexcept
{
    remove_spatial(obj);
    remove_locals(obj);
    remove_vitals(obj);
    remove_geometry(obj);
    remove_visual(obj);
    remove_item_layout(obj);
    remove_inventory(obj);
    remove_store_inventory(obj);
    remove_ability_loadout(obj);
}

void ObjectComponentSystem::remove(ObjectBase& obj) noexcept
{
    if (use_handle_key(obj)) {
        remove(obj.handle());
    }

    remove_inventory(obj);
    remove_store_inventory(obj);
}

void ObjectComponentSystem::remove_spatial(ObjectHandle obj) noexcept
{
    auto it = spatial_index_.find(obj.to_ull());
    if (it != spatial_index_.end()) {
        const size_t index = it->second;
        const size_t last = spatial_.size() - 1;
        spatial_index_.erase(it);

        if (index != last) {
            spatial_[index] = spatial_[last];
            auto moved = spatial_index_.find(spatial_[index].owner.to_ull());
            if (moved != spatial_index_.end()) {
                moved->second = index;
            }
        }

        spatial_.pop_back();
    }
}

void ObjectComponentSystem::remove_locals(ObjectHandle obj) noexcept
{
    auto local_it = local_data_index_.find(obj.to_ull());
    if (local_it == local_data_index_.end()) {
        return;
    }

    const size_t local_index = local_it->second;
    const size_t local_last = local_data_.size() - 1;
    local_data_index_.erase(local_it);

    if (local_index != local_last) {
        local_data_[local_index] = std::move(local_data_[local_last]);
        auto moved = local_data_index_.find(local_data_[local_index].owner.to_ull());
        if (moved != local_data_index_.end()) {
            moved->second = local_index;
        }
    }

    local_data_.pop_back();
}

void ObjectComponentSystem::remove_vitals(ObjectHandle obj) noexcept
{
    auto it = vitals_index_.find(obj.to_ull());
    if (it == vitals_index_.end()) {
        return;
    }

    const size_t index = it->second;
    const size_t last = vitals_.size() - 1;
    vitals_index_.erase(it);

    if (index != last) {
        vitals_[index] = vitals_[last];
        auto moved = vitals_index_.find(vitals_[index].owner.to_ull());
        if (moved != vitals_index_.end()) {
            moved->second = index;
        }
    }

    vitals_.pop_back();
}

void ObjectComponentSystem::remove_geometry(ObjectHandle obj) noexcept
{
    auto geometry_it = geometry_index_.find(obj.to_ull());
    if (geometry_it == geometry_index_.end()) {
        return;
    }

    const size_t index = geometry_it->second;
    const size_t last = geometry_.size() - 1;
    geometry_index_.erase(geometry_it);

    if (index != last) {
        geometry_[index] = std::move(geometry_[last]);
        auto moved = geometry_index_.find(geometry_[index].owner.to_ull());
        if (moved != geometry_index_.end()) {
            moved->second = index;
        }
    }

    geometry_.pop_back();
}

void ObjectComponentSystem::remove_visual(ObjectHandle obj) noexcept
{
    auto visual_it = visual_index_.find(obj.to_ull());
    if (visual_it == visual_index_.end()) {
        return;
    }

    const size_t index = visual_it->second;
    const size_t last = visual_.size() - 1;
    visual_index_.erase(visual_it);

    if (index != last) {
        visual_[index] = std::move(visual_[last]);
        auto moved = visual_index_.find(visual_[index].owner.to_ull());
        if (moved != visual_index_.end()) {
            moved->second = index;
        }
    }

    visual_.pop_back();
}

void ObjectComponentSystem::remove_item_layout(ObjectHandle obj) noexcept
{
    auto it = item_layout_index_.find(obj.to_ull());
    if (it == item_layout_index_.end()) {
        return;
    }

    const size_t index = it->second;
    const size_t last = item_layouts_.size() - 1;
    item_layout_index_.erase(it);

    if (index != last) {
        item_layouts_[index] = item_layouts_[last];
        auto moved = item_layout_index_.find(item_layouts_[index].owner.to_ull());
        if (moved != item_layout_index_.end()) {
            moved->second = index;
        }
    }

    item_layouts_.pop_back();
}

void ObjectComponentSystem::remove_inventory(ObjectHandle obj) noexcept
{
    auto it = inventory_handle_index_.find(obj.to_ull());
    if (it == inventory_handle_index_.end()) {
        return;
    }

    const size_t index = it->second;
    const size_t last = inventory_.size() - 1;
    auto removed = std::move(inventory_[index]);
    inventory_handle_index_.erase(it);
    if (removed->owner_ptr) {
        inventory_ptr_index_.erase(ptr_key(*removed->owner_ptr));
    }

    if (index != last) {
        inventory_[index] = std::move(inventory_[last]);
        if (use_handle_key(*inventory_[index]->owner_ptr)) {
            auto moved = inventory_handle_index_.find(inventory_[index]->owner.to_ull());
            if (moved != inventory_handle_index_.end()) {
                moved->second = index;
            }
        } else {
            auto moved = inventory_ptr_index_.find(ptr_key(*inventory_[index]->owner_ptr));
            if (moved != inventory_ptr_index_.end()) {
                moved->second = index;
            }
        }
    }

    inventory_.pop_back();
    removed->inventory.destroy();
}

void ObjectComponentSystem::remove_inventory(ObjectBase& obj) noexcept
{
    if (use_handle_key(obj)) {
        remove_inventory(obj.handle());
    }

    auto it = inventory_ptr_index_.find(ptr_key(obj));
    if (it == inventory_ptr_index_.end()) {
        return;
    }

    const size_t index = it->second;
    const size_t last = inventory_.size() - 1;
    auto removed = std::move(inventory_[index]);
    inventory_ptr_index_.erase(it);

    if (index != last) {
        inventory_[index] = std::move(inventory_[last]);
        if (use_handle_key(*inventory_[index]->owner_ptr)) {
            auto moved = inventory_handle_index_.find(inventory_[index]->owner.to_ull());
            if (moved != inventory_handle_index_.end()) {
                moved->second = index;
            }
        } else {
            auto moved = inventory_ptr_index_.find(ptr_key(*inventory_[index]->owner_ptr));
            if (moved != inventory_ptr_index_.end()) {
                moved->second = index;
            }
        }
    }

    inventory_.pop_back();
    removed->inventory.destroy();
}

void ObjectComponentSystem::remove_store_inventory(ObjectHandle obj) noexcept
{
    auto it = store_inventory_handle_index_.find(obj.to_ull());
    if (it == store_inventory_handle_index_.end()) {
        return;
    }

    const size_t index = it->second;
    const size_t last = store_inventory_.size() - 1;
    auto removed = std::move(store_inventory_[index]);
    store_inventory_handle_index_.erase(it);
    if (removed->owner_ptr) {
        store_inventory_ptr_index_.erase(ptr_key(*removed->owner_ptr));
    }

    if (index != last) {
        store_inventory_[index] = std::move(store_inventory_[last]);
        if (use_handle_key(*store_inventory_[index]->owner_ptr)) {
            auto moved = store_inventory_handle_index_.find(store_inventory_[index]->owner.to_ull());
            if (moved != store_inventory_handle_index_.end()) {
                moved->second = index;
            }
        } else {
            auto moved = store_inventory_ptr_index_.find(ptr_key(*store_inventory_[index]->owner_ptr));
            if (moved != store_inventory_ptr_index_.end()) {
                moved->second = index;
            }
        }
    }

    store_inventory_.pop_back();
    destroy(removed->inventory);
}

void ObjectComponentSystem::remove_store_inventory(ObjectBase& obj) noexcept
{
    if (use_handle_key(obj)) {
        remove_store_inventory(obj.handle());
    }

    auto it = store_inventory_ptr_index_.find(ptr_key(obj));
    if (it == store_inventory_ptr_index_.end()) {
        return;
    }

    const size_t index = it->second;
    const size_t last = store_inventory_.size() - 1;
    auto removed = std::move(store_inventory_[index]);
    store_inventory_ptr_index_.erase(it);

    if (index != last) {
        store_inventory_[index] = std::move(store_inventory_[last]);
        if (use_handle_key(*store_inventory_[index]->owner_ptr)) {
            auto moved = store_inventory_handle_index_.find(store_inventory_[index]->owner.to_ull());
            if (moved != store_inventory_handle_index_.end()) {
                moved->second = index;
            }
        } else {
            auto moved = store_inventory_ptr_index_.find(ptr_key(*store_inventory_[index]->owner_ptr));
            if (moved != store_inventory_ptr_index_.end()) {
                moved->second = index;
            }
        }
    }

    store_inventory_.pop_back();
    destroy(removed->inventory);
}

void ObjectComponentSystem::remove_ability_loadout(ObjectHandle obj) noexcept
{
    auto it = ability_loadout_index_.find(obj.to_ull());
    if (it == ability_loadout_index_.end()) {
        return;
    }

    const size_t index = it->second;
    const size_t last = ability_loadouts_.size() - 1;
    ability_loadout_index_.erase(it);

    if (index != last) {
        ability_loadouts_[index] = std::move(ability_loadouts_[last]);
        auto moved = ability_loadout_index_.find(ability_loadouts_[index].owner.to_ull());
        if (moved != ability_loadout_index_.end()) {
            moved->second = index;
        }
    }

    ability_loadouts_.pop_back();
}

void ObjectComponentSystem::clear() noexcept
{
    spatial_index_.clear();
    spatial_.clear();
    local_data_index_.clear();
    local_data_.clear();
    vitals_index_.clear();
    vitals_.clear();
    geometry_index_.clear();
    geometry_.clear();
    visual_index_.clear();
    visual_.clear();
    item_layout_index_.clear();
    item_layouts_.clear();
    inventory_handle_index_.clear();
    inventory_ptr_index_.clear();
    inventory_.clear();
    store_inventory_handle_index_.clear();
    store_inventory_ptr_index_.clear();
    store_inventory_.clear();
    ability_loadout_index_.clear();
    ability_loadouts_.clear();
}

nlohmann::json ObjectComponentSystem::stats() const
{
    return {
        {"spatial", spatial_.size()},
        {"local_data", local_data_.size()},
        {"vitals", vitals_.size()},
        {"geometry", geometry_.size()},
        {"visual", visual_.size()},
        {"item_layout", item_layouts_.size()},
        {"inventory", inventory_.size()},
        {"store_inventory", store_inventory_.size()},
        {"ability_loadout", ability_loadouts_.size()},
    };
}

} // namespace nw
