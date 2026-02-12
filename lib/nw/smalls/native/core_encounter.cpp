#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_encounter(Runtime& rt)
{
    if (rt.get_native_module("core.encounter")) {
        return;
    }

    rt.module("core.encounter")
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("is_encounter", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_encounter(); })
        .function("as_encounter", +[](nw::ObjectHandle obj) -> nw::ObjectHandle {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return (base && base->as_encounter()) ? obj : nw::ObjectHandle{}; })
        .function("is_active", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* encounter = base ? base->as_encounter() : nullptr;
            return encounter && encounter->active; })
        .function("get_spawn_point_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* encounter = base ? base->as_encounter() : nullptr;
            return encounter ? static_cast<int32_t>(encounter->spawn_points.size()) : 0; })
        .function("get_creatures_max", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* encounter = base ? base->as_encounter() : nullptr;
            return encounter ? encounter->creatures_max : 0; })
        .function("type_id", +[]() -> int32_t { return static_cast<int32_t>(nw::ObjectType::encounter); })
        .finalize();
}

} // namespace nw::smalls
