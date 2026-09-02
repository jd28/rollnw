#include "../runtime.hpp"

#include "../../kernel/Rules.hpp"

#include <limits>

namespace nw::smalls {

namespace {

struct ScriptPlaceableAppearanceInfo {
    int32_t id = -1;
    int32_t string_ref = -1;
    nw::Resref model;
};

ScriptPlaceableAppearanceInfo placeable_info(int32_t id)
{
    const auto* info = nw::kernel::rules().placeables.get(
        nw::PlaceableAppearance::make(id));
    if (!info) { return {}; }
    return {
        .id = id,
        .string_ref = info->string_ref
                <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
            ? static_cast<int32_t>(info->string_ref)
            : -1,
        .model = info->model,
    };
}

} // namespace

void register_core_placeable(Runtime& rt)
{
    if (rt.get_native_module("core.placeable")) { return; }

    rt.module("core.placeable")
        .native_struct<ScriptPlaceableAppearanceInfo>("PlaceableAppearanceInfo")
        .field("id", &ScriptPlaceableAppearanceInfo::id)
        .field("string_ref", &ScriptPlaceableAppearanceInfo::string_ref)
        .field("model", &ScriptPlaceableAppearanceInfo::model)
        .end_struct()
        .function("placeable_info", &placeable_info)
        .finalize();
}

} // namespace nw::smalls
