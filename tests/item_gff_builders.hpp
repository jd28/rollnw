#pragma once

#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace rollnw::tests {

enum class TestItemModelShape {
    simple,
    layered,
    composite,
};

struct TestItemPropertyGff {
    uint16_t type = std::numeric_limits<uint16_t>::max();
    uint16_t subtype = 0;
    uint8_t cost_table = 0;
    uint16_t cost_value = 0;
    uint8_t param_table = 0;
    uint8_t param_value = 0;
    std::string tag;
};

struct TestItemGff {
    nw::BaseItem base_item = nw::BaseItem::make(0);
    TestItemModelShape model_shape = TestItemModelShape::simple;
    std::array<uint8_t, 3> model_parts{};
    std::array<uint8_t, 6> model_colors{};
    std::vector<TestItemPropertyGff> properties;
};

inline void add_test_item_properties(nw::GffBuilderStruct& top,
    const std::vector<TestItemPropertyGff>& properties)
{
    auto& list = top.add_list("PropertiesList");
    for (const auto& property : properties) {
        auto& entry = list.push_back(0);
        entry.add_field("PropertyName", property.type)
            .add_field("Subtype", property.subtype)
            .add_field("CostTable", property.cost_table)
            .add_field("CostValue", property.cost_value)
            .add_field("Param1", property.param_table)
            .add_field("Param1Value", property.param_value);
        if (!property.tag.empty()) {
            entry.add_field("CustomTag", nw::String{property.tag});
        }
    }
}

inline void add_test_item_visuals(nw::GffBuilderStruct& top, const TestItemGff& spec)
{
    top.add_field("ModelPart1", spec.model_parts[0]);
    if (spec.model_shape == TestItemModelShape::composite) {
        top.add_field("ModelPart2", spec.model_parts[1])
            .add_field("ModelPart3", spec.model_parts[2]);
    }

    if (spec.model_shape == TestItemModelShape::layered) {
        top.add_field("Cloth1Color", spec.model_colors[0])
            .add_field("Cloth2Color", spec.model_colors[1])
            .add_field("Leather1Color", spec.model_colors[2])
            .add_field("Leather2Color", spec.model_colors[3])
            .add_field("Metal1Color", spec.model_colors[4])
            .add_field("Metal2Color", spec.model_colors[5]);
    }
}

inline void add_test_item_fields(nw::GffBuilderStruct& top, const TestItemGff& spec)
{
    top.add_field("TemplateResRef", nw::Resref{"test_item"})
        .add_field("LocalizedName", nw::LocString{})
        .add_field("Tag", nw::String{"test_item"})
        .add_field("Comment", nw::String{})
        .add_field("PaletteID", uint8_t{0})
        .add_field("Description", nw::LocString{})
        .add_field("DescIdentified", nw::LocString{})
        .add_field("Cost", uint32_t{0})
        .add_field("AddCost", uint32_t{0})
        .add_field("BaseItem", *spec.base_item)
        .add_field("StackSize", uint16_t{1})
        .add_field("Charges", uint8_t{0})
        .add_field("Identified", uint8_t{0})
        .add_field("Plot", uint8_t{0})
        .add_field("Stolen", uint8_t{0});
    add_test_item_visuals(top, spec);
    add_test_item_properties(top, spec.properties);
}

inline bool deserialize_item_from_gff(nw::Item* item, const TestItemGff& spec)
{
    if (!item) { return false; }

    nw::GffBuilder builder{nw::Item::serial_id};
    add_test_item_fields(builder.top, spec);
    builder.build();

    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff gff{std::move(rd)};
    if (!gff.valid()) {
        return false;
    }

    item->clear();
    return nw::deserialize(item, gff.toplevel(), nw::SerializationProfile::blueprint);
}

inline nw::Item* make_item_from_gff(const TestItemGff& spec)
{
    auto* item = nw::kernel::objects().make<nw::Item>();
    if (!item) { return nullptr; }

    if (!deserialize_item_from_gff(item, spec)) {
        nw::kernel::objects().destroy(item->handle());
        return nullptr;
    }

    return item;
}

} // namespace rollnw::tests
