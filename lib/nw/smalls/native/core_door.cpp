#include "../runtime.hpp"

#include "../../kernel/Rules.hpp"
#include "../../util/string.hpp"

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

nw::Resref normalize_door_model(nw::Resref model)
{
    const auto value = model.view();
    if (value.empty() || nw::string::icmp(value, "****")
        || nw::string::icmp(value, "null")
        || nw::string::icmp(value, "none")) {
        return {};
    }
    return model;
}

template <typename Array>
bool publish_door_info(Value value, Array& destination)
{
    auto& rt = nw::kernel::runtime();
    const TypeID info_type = rt.type_id("core.door.DoorAppearanceInfo");
    auto* array = rt.get_array_typed(value.data.hptr);
    if (!array || info_type == invalid_type_id
        || array->element_type() != info_type) {
        return false;
    }

    Array next{nw::kernel::rules().allocator()};
    next.entries.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        Value element;
        if (!array->get_value(index, element, rt)
            || element.type_id != info_type) {
            return false;
        }
        const auto source = detail::value_cast<ScriptDoorAppearanceInfo>(
            &rt, element);
        if (source.id == -1) {
            next.entries.emplace_back();
            continue;
        }
        if (source.id != static_cast<int32_t>(index)
            || source.string_ref < -1) {
            return false;
        }
        typename decltype(next.entries)::value_type target;
        target.string_ref = static_cast<uint32_t>(source.string_ref);
        target.model = normalize_door_model(source.model);
        next.entries.push_back(std::move(target));
    }
    destination = std::move(next);
    return true;
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
        .function("publish_door_type_info", +[](Value value) -> bool { return publish_door_info(value, nw::kernel::rules().doortypes); })
        .function("publish_generic_door_info", +[](Value value) -> bool { return publish_door_info(value, nw::kernel::rules().genericdoors); })
        .finalize();
}

} // namespace nw::smalls
