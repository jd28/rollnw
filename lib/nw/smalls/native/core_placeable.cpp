#include "../runtime.hpp"

#include "../../kernel/Rules.hpp"

namespace nw::smalls {

namespace {

struct ScriptPlaceableAppearanceInfo {
    int32_t id = -1;
    nw::Resref model;
};

ScriptPlaceableAppearanceInfo placeable_info(int32_t id)
{
    const auto* info = nw::kernel::rules().placeables.get(
        nw::PlaceableAppearance::make(id));
    if (!info) { return {}; }
    return {
        .id = id,
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
        .field("model", &ScriptPlaceableAppearanceInfo::model)
        .end_struct()
        .function("placeable_info", &placeable_info)
        .finalize();
}

} // namespace nw::smalls
