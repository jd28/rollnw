#pragma once

#include "../rules/items.hpp"
#include "Common.hpp"
#include "Inventory.hpp"
#include "LocalData.hpp"
#include "Location.hpp"
#include "ObjectBase.hpp"

#include <array>

namespace nw {

struct Image;
struct PltColors;

struct Item : public ObjectBase {
    Item();
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::item;
    static constexpr ResourceType::type restype = ResourceType::uti;

    virtual Common* as_common() override { return &common; }
    virtual const Common* as_common() const override { return &common; }
    virtual Item* as_item() override { return this; }
    virtual const Item* as_item() const override { return this; }
    virtual void destroy() override;
    virtual bool instantiate() override;
    virtual InternedString tag() const override { return common.tag; }

    // Serialization
    static bool deserialize(Item* obj, const nlohmann::json& archive, SerializationProfile profile);
    static bool serialize(const Item* obj, nlohmann::json& archive, SerializationProfile profile);
    static String get_name_from_file(const std::filesystem::path& path);

    /// Gets image by item model part.
    /// @note Caller takes ownership of the resulting image.  Merging icons is also the responsibility of the caller.
    Image* get_icon_by_part(ItemModelParts::type part = ItemModelParts::model1, bool female = false) const;

    /// Converts model colors to Plt colors
    PltColors model_to_plt_colors() const noexcept;

    Common common;
    Inventory inventory;

    LocString description;
    LocString description_id;
    Vector<ItemProperty> properties;

    // Transient
    int armor_id = -1;

    uint32_t cost = 0;
    uint32_t additional_cost = 0;
    nw::BaseItem baseitem;

    uint16_t stacksize = 1;

    uint8_t charges = 0;
    bool cursed = false;
    bool identified = false;
    bool plot = false;
    bool stolen = false;

    ItemModelType model_type = ItemModelType::simple;
    std::array<uint8_t, 6> model_colors;
    std::array<uint8_t, 19> model_parts;

    bool instantiated_ = false;
};

// == Item - Serialization - Gff ==============================================
// ============================================================================

bool deserialize(Item* obj, const GffStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Item* obj, SerializationProfile profile);
bool serialize(const Item* obj, GffBuilderStruct& archive, SerializationProfile profile);

} // namespace nw
