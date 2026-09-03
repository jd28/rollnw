#include "../runtime.hpp"

#include "../../kernel/Rules.hpp"

#include <limits>

namespace nw::smalls {

namespace {

struct ScriptPlaceableAppearanceInfo {
    int32_t id = -1;
    ScriptString label;
    int32_t string_ref = -1;
    nw::Resref model;
};

ScriptPlaceableAppearanceInfo placeable_info(int32_t id)
{
    auto& rt = nw::kernel::runtime();
    const auto* info = nw::kernel::rules().placeables.get(
        nw::PlaceableAppearance::make(id));
    if (!info) { return {}; }
    return {
        .id = id,
        .label = ScriptString{rt.alloc_string(info->label)},
        .string_ref = info->string_ref
                <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
            ? static_cast<int32_t>(info->string_ref)
            : -1,
        .model = info->model,
    };
}

bool publish_placeable_info(Value value)
{
    auto& rt = nw::kernel::runtime();
    const TypeID info_type = rt.type_id(
        "core.placeable.PlaceableAppearanceInfo");
    auto* array = rt.get_array_typed(value.data.hptr);
    if (!array || info_type == invalid_type_id
        || array->element_type() != info_type) {
        return false;
    }

    auto& rules = nw::kernel::rules();
    nw::PlaceableAppearanceArray next{rules.allocator()};
    next.entries.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        Value element;
        if (!array->get_value(index, element, rt)
            || element.type_id != info_type) {
            return false;
        }
        const auto source = detail::value_cast<ScriptPlaceableAppearanceInfo>(
            &rt, element);
        if (source.id == -1) {
            next.entries.emplace_back();
            continue;
        }
        if (source.id != static_cast<int32_t>(index)
            || source.string_ref < -1) {
            return false;
        }
        nw::PlaceableAppearanceInfo target;
        target.label = source.label.view(rt);
        target.string_ref = static_cast<uint32_t>(source.string_ref);
        target.model = source.model;
        next.entries.push_back(std::move(target));
    }
    rules.placeables = std::move(next);
    return true;
}

} // namespace

void register_core_placeable(Runtime& rt)
{
    if (rt.get_native_module("core.placeable")) { return; }

    rt.module("core.placeable")
        .native_struct<ScriptPlaceableAppearanceInfo>("PlaceableAppearanceInfo")
        .field("id", &ScriptPlaceableAppearanceInfo::id)
        .field("label", &ScriptPlaceableAppearanceInfo::label)
        .field("string_ref", &ScriptPlaceableAppearanceInfo::string_ref)
        .field("model", &ScriptPlaceableAppearanceInfo::model)
        .end_struct()
        .function("placeable_info", &placeable_info)
        .function("publish_placeable_info", &publish_placeable_info)
        .finalize();
}

} // namespace nw::smalls
