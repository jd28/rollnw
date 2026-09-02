#include "../runtime.hpp"

#include "../../kernel/Rules.hpp"

#include <limits>

namespace nw::smalls {

namespace {

struct ScriptDoorAppearanceInfo {
    int32_t id = -1;
    int32_t string_ref = -1;
    nw::Resref model;
};

template <typename Info>
ScriptDoorAppearanceInfo make_door_info(int32_t id, const Info* info)
{
    if (!info || !info->valid()) { return {}; }
    return {
        .id = id,
        .string_ref = info->string_ref
                <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
            ? static_cast<int32_t>(info->string_ref)
            : -1,
        .model = info->model,
    };
}

int32_t bounded_count(size_t count) noexcept
{
    return count <= static_cast<size_t>(std::numeric_limits<int32_t>::max())
        ? static_cast<int32_t>(count)
        : 0;
}

} // namespace

void register_core_door(Runtime& rt)
{
    if (rt.get_native_module("core.door")) { return; }

    rt.module("core.door")
        .native_struct<ScriptDoorAppearanceInfo>("DoorAppearanceInfo")
        .field("id", &ScriptDoorAppearanceInfo::id)
        .field("string_ref", &ScriptDoorAppearanceInfo::string_ref)
        .field("model", &ScriptDoorAppearanceInfo::model)
        .end_struct()
        .function("door_type_count", +[]() -> int32_t { return bounded_count(nw::kernel::rules().doortypes.entries.size()); })
        .function("door_type_info", +[](int32_t id) -> ScriptDoorAppearanceInfo { return make_door_info(id,
                                                                                      nw::kernel::rules().doortypes.get(nw::DoorType::make(id))); })
        .function("generic_door_count", +[]() -> int32_t { return bounded_count(nw::kernel::rules().genericdoors.entries.size()); })
        .function("generic_door_info", +[](int32_t id) -> ScriptDoorAppearanceInfo { return make_door_info(id,
                                                                                         nw::kernel::rules().genericdoors.get(nw::GenericDoor::make(id))); })
        .finalize();
}

} // namespace nw::smalls
