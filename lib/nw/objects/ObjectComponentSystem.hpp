#pragma once

#include "../formats/Plt.hpp"
#include "../resources/assets.hpp"
#include "Inventory.hpp"
#include "LocalData.hpp"
#include "ObjectHandle.hpp"

#include <absl/container/flat_hash_map.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>

namespace nw {

struct ObjectBase;

struct ObjectSpatialState {
    ObjectHandle owner{};
    ObjectID area = object_invalid;
    glm::vec3 position{0.0f};
    glm::vec3 orientation{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angular_velocity{0.0f};
    uint32_t flags = 0;
};

struct ObjectLocalDataState {
    ObjectHandle owner{};
    LocalData locals;
};

struct ObjectVitalsState {
    ObjectHandle owner{};
    int32_t hp_current = 0;
    int32_t hp_max = 1;
};

struct ObjectSpawnPoint {
    glm::vec3 position{0.0f};
    float orientation = 0.0f;
};

struct ObjectGeometryState {
    ObjectHandle owner{};
    Vector<glm::vec3> points;
    Vector<ObjectSpawnPoint> spawn_points;
    float highlight_height = 0.0f;
};

struct ObjectVisualModelFlags {
    enum type : uint32_t {
        none = 0,
        requires_wearer = 1u << 0,
        hidden_in_game = 1u << 1,
        hidden_in_toolset = 1u << 2,
    };
};

struct ObjectVisualModelKind {
    enum type : int32_t {
        root = 0,
        item_model = 1,
        creature_model_part = 2,
        creature_attachment = 3,
    };
};

struct ObjectVisualModelSlot {
    enum type : int32_t {
        none = -1,
    };
};

struct ObjectVisualCreatureAttachmentPart {
    enum type : int32_t {
        wing = 1,
        tail = 2,
    };
};

enum class ObjectVisualRenderMode : uint8_t {
    game,
    toolset,
};

struct ObjectVisualModel {
    Resref model;
    Resref attach_to;
    Resref attach_from;
    int32_t kind = ObjectVisualModelKind::root;
    int32_t slot = ObjectVisualModelSlot::none;
    int32_t part = 0;
    int32_t source_part = 0;
    int32_t model_part = 0;
    uint32_t flags = 0;
    PltColors plt_colors{};
    uint32_t plt_color_mask = 0;
};

struct ObjectVisualLight {
    glm::vec3 light_offset{0.0f};
    int32_t light_color = -1;
};

struct ObjectVisualState {
    ObjectHandle owner{};
    Vector<ObjectVisualModel> models;
    Vector<ObjectVisualLight> lights;
    PltColors base_plt_colors{};
    uint32_t base_plt_color_mask = 0;
    int32_t appearance = -1;
    int32_t body_variant = 0;
};

[[nodiscard]] bool object_visual_model_flags_valid(uint32_t flags) noexcept;
[[nodiscard]] bool object_visual_plt_color_mask_valid(uint32_t mask) noexcept;
[[nodiscard]] bool object_visual_model_visible_in_mode(
    const ObjectVisualModel& model,
    ObjectVisualRenderMode mode) noexcept;

struct ObjectInventoryState {
    ObjectHandle owner{};
    ObjectBase* owner_ptr = nullptr;
    Inventory inventory;
};

struct ObjectItemLayoutState {
    ObjectHandle owner{};
    int32_t inventory_width = 0;
    int32_t inventory_height = 0;
};

struct ObjectStoreInventoryState {
    ObjectHandle owner{};
    ObjectBase* owner_ptr = nullptr;
    StoreInventory inventory;
};

struct ObjectAbilityLoadoutEntry {
    int32_t source = -1;
    int32_t tier = -1;
    int32_t slot = -1;
    int32_t ability = -1;
    int32_t modifier = 0;
    uint32_t flags = 0;
};

struct ObjectAbilityLoadoutState {
    ObjectHandle owner{};
    Vector<ObjectAbilityLoadoutEntry> entries;
};

struct ObjectComponentSystem {
    ObjectSpatialState* get_or_create_spatial(ObjectHandle obj);
    ObjectSpatialState* find_spatial(ObjectHandle obj) noexcept;
    const ObjectSpatialState* find_spatial(ObjectHandle obj) const noexcept;
    Location location(ObjectHandle obj) const noexcept;

    bool deserialize_spatial(ObjectHandle obj, const GffStruct& archive, SerializationProfile profile);
    bool from_json_spatial(ObjectHandle obj, const nlohmann::json& component_archive, SerializationProfile profile);
    bool serialize_position(ObjectHandle obj, GffBuilderStruct& archive, SerializationProfile profile) const;
    bool serialize_position_orientation(ObjectHandle obj, GffBuilderStruct& archive, SerializationProfile profile) const;
    void to_json_spatial(ObjectHandle obj, nlohmann::json& component_archive, SerializationProfile profile) const;

    LocalData* get_or_create_locals(ObjectHandle obj);
    LocalData* find_locals(ObjectHandle obj) noexcept;
    const LocalData* find_locals(ObjectHandle obj) const noexcept;

    bool set_location(ObjectHandle obj, Location location);
    bool set_area(ObjectHandle obj, ObjectID area);
    bool deserialize_locals(ObjectHandle obj, const GffStruct& archive);
    bool from_json_locals(ObjectHandle obj, const nlohmann::json& component_archive);
    bool serialize_locals(ObjectHandle obj, GffBuilderStruct& archive, SerializationProfile profile) const;
    void to_json_locals(ObjectHandle obj, nlohmann::json& component_archive, SerializationProfile profile) const;

    ObjectVitalsState* get_or_create_vitals(ObjectHandle obj);
    ObjectVitalsState* find_vitals(ObjectHandle obj) noexcept;
    const ObjectVitalsState* find_vitals(ObjectHandle obj) const noexcept;
    bool set_vitals(ObjectHandle obj, int32_t hp_current, int32_t hp_max);
    void remove_vitals(ObjectHandle obj) noexcept;

    ObjectGeometryState* get_or_create_geometry(ObjectHandle obj);
    ObjectGeometryState* find_geometry(ObjectHandle obj) noexcept;
    const ObjectGeometryState* find_geometry(ObjectHandle obj) const noexcept;
    bool set_geometry(ObjectHandle obj, std::span<const glm::vec3> points);
    bool set_spawn_points(ObjectHandle obj, std::span<const ObjectSpawnPoint> spawn_points);
    bool set_highlight_height(ObjectHandle obj, float value);
    void remove_geometry(ObjectHandle obj) noexcept;

    ObjectVisualState* get_or_create_visual(ObjectHandle obj);
    ObjectVisualState* find_visual(ObjectHandle obj) noexcept;
    const ObjectVisualState* find_visual(ObjectHandle obj) const noexcept;
    bool clear_visual(ObjectHandle obj, int32_t appearance);
    bool clear_visual_slot(ObjectHandle obj, int32_t slot);
    bool set_visual_body_variant(ObjectHandle obj, int32_t body_variant);
    bool set_visual_base_plt_colors(ObjectHandle obj, uint32_t mask, PltColors colors);
    bool add_visual_model(ObjectHandle obj, ObjectVisualModel model);
    bool add_visual_model_row(ObjectHandle obj, ObjectVisualModel model);
    bool add_visual_part(ObjectHandle obj, ObjectVisualModel model);
    bool set_visual_plt_colors(ObjectHandle obj, int32_t slot, int32_t kind, int32_t part,
        uint32_t mask, PltColors colors);
    bool add_visual_light(ObjectHandle obj, int32_t light_color, glm::vec3 light_offset);
    void remove_visual(ObjectHandle obj) noexcept;

    ObjectItemLayoutState* get_or_create_item_layout(ObjectHandle obj);
    ObjectItemLayoutState* find_item_layout(ObjectHandle obj) noexcept;
    const ObjectItemLayoutState* find_item_layout(ObjectHandle obj) const noexcept;
    bool set_item_layout(ObjectHandle obj, int32_t inventory_width, int32_t inventory_height);
    void remove_item_layout(ObjectHandle obj) noexcept;

    Inventory* get_or_create_inventory(ObjectBase& obj, int pages, int rows, int columns);
    Inventory* find_inventory(ObjectBase& obj) noexcept;
    const Inventory* find_inventory(const ObjectBase& obj) const noexcept;

    StoreInventory* get_or_create_store_inventory(ObjectBase& obj);
    StoreInventory* find_store_inventory(ObjectBase& obj) noexcept;
    const StoreInventory* find_store_inventory(const ObjectBase& obj) const noexcept;

    ObjectAbilityLoadoutState* get_or_create_ability_loadout(ObjectHandle obj);
    ObjectAbilityLoadoutState* find_ability_loadout(ObjectHandle obj) noexcept;
    const ObjectAbilityLoadoutState* find_ability_loadout(ObjectHandle obj) const noexcept;
    bool from_json_ability_loadout(ObjectHandle obj, const nlohmann::json& archive);
    nlohmann::json ability_loadout_to_json(ObjectHandle obj) const;

    bool add_unslotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t ability);
    void remove_unslotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t ability);
    bool has_unslotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t ability) const;
    size_t unslotted_ability_count(ObjectHandle obj, int32_t source, int32_t tier = -1) const;
    bool set_slotted_ability_count(ObjectHandle obj, int32_t source, int32_t tier, size_t count);
    int available_ability_slots(ObjectHandle obj, int32_t source, int32_t tier) const;
    int first_empty_ability_slot(ObjectHandle obj, int32_t source, int32_t tier) const;
    bool set_slotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t slot,
        int32_t ability, int32_t modifier, uint32_t flags);
    bool add_slotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t ability,
        int32_t modifier, uint32_t flags);
    void clear_slotted_ability(ObjectHandle obj, int32_t source, int32_t tier, int32_t slot);
    void clear_slotted_ability_from_tier(ObjectHandle obj, int32_t source, int32_t min_tier, int32_t ability);
    int find_slotted_ability_slot(ObjectHandle obj, int32_t source, int32_t tier, int32_t ability,
        int32_t modifier) const;
    int count_slotted_ability(ObjectHandle obj, int32_t source, int32_t ability, int32_t modifier) const;
    size_t slotted_ability_count(ObjectHandle obj, int32_t source) const;

    bool set_position(ObjectHandle obj, glm::vec3 position);
    bool set_orientation(ObjectHandle obj, glm::vec3 orientation);
    bool set_scale(ObjectHandle obj, glm::vec3 scale);
    bool set_velocity(ObjectHandle obj, glm::vec3 velocity);
    bool set_angular_velocity(ObjectHandle obj, glm::vec3 velocity);

    void remove(ObjectHandle obj) noexcept;
    void remove(ObjectBase& obj) noexcept;
    void clear() noexcept;

    size_t spatial_count() const noexcept { return spatial_.size(); }
    size_t local_data_count() const noexcept { return local_data_.size(); }
    size_t vitals_count() const noexcept { return vitals_.size(); }
    size_t geometry_count() const noexcept { return geometry_.size(); }
    size_t visual_count() const noexcept { return visual_.size(); }
    size_t item_layout_count() const noexcept { return item_layouts_.size(); }
    size_t inventory_count() const noexcept { return inventory_.size(); }
    size_t store_inventory_count() const noexcept { return store_inventory_.size(); }
    size_t ability_loadout_count() const noexcept { return ability_loadouts_.size(); }
    nlohmann::json stats() const;

private:
    void remove_spatial(ObjectHandle obj) noexcept;
    void remove_locals(ObjectHandle obj) noexcept;
    void remove_inventory(ObjectHandle obj) noexcept;
    void remove_inventory(ObjectBase& obj) noexcept;
    void remove_store_inventory(ObjectHandle obj) noexcept;
    void remove_store_inventory(ObjectBase& obj) noexcept;
    void remove_ability_loadout(ObjectHandle obj) noexcept;

    absl::flat_hash_map<uint64_t, size_t> spatial_index_;
    Vector<ObjectSpatialState> spatial_;

    absl::flat_hash_map<uint64_t, size_t> local_data_index_;
    Vector<ObjectLocalDataState> local_data_;

    absl::flat_hash_map<uint64_t, size_t> vitals_index_;
    Vector<ObjectVitalsState> vitals_;

    absl::flat_hash_map<uint64_t, size_t> geometry_index_;
    Vector<ObjectGeometryState> geometry_;

    absl::flat_hash_map<uint64_t, size_t> visual_index_;
    Vector<ObjectVisualState> visual_;

    absl::flat_hash_map<uint64_t, size_t> item_layout_index_;
    Vector<ObjectItemLayoutState> item_layouts_;

    absl::flat_hash_map<uint64_t, size_t> inventory_handle_index_;
    absl::flat_hash_map<uintptr_t, size_t> inventory_ptr_index_;
    Vector<std::unique_ptr<ObjectInventoryState>> inventory_;

    absl::flat_hash_map<uint64_t, size_t> store_inventory_handle_index_;
    absl::flat_hash_map<uintptr_t, size_t> store_inventory_ptr_index_;
    Vector<std::unique_ptr<ObjectStoreInventoryState>> store_inventory_;

    absl::flat_hash_map<uint64_t, size_t> ability_loadout_index_;
    Vector<ObjectAbilityLoadoutState> ability_loadouts_;

    friend struct Creature;
    friend struct Item;
    friend struct Placeable;
    friend struct Store;
};

} // namespace nw
