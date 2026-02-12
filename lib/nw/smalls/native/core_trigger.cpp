#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_trigger(Runtime& rt)
{
    if (rt.get_native_module("core.trigger")) {
        return;
    }

    rt.module("core.trigger")
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("is_trigger", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_trigger(); })
        .function("as_trigger", +[](nw::ObjectHandle obj) -> nw::ObjectHandle {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return (base && base->as_trigger()) ? obj : nw::ObjectHandle{}; })
        .function("get_geometry_point_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* trigger = base ? base->as_trigger() : nullptr;
            return trigger ? static_cast<int32_t>(trigger->geometry.size()) : 0; })
        .function("get_trigger_type", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* trigger = base ? base->as_trigger() : nullptr;
            return trigger ? trigger->type : 0; })
        .function("get_highlight_height", +[](nw::ObjectHandle obj) -> float {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* trigger = base ? base->as_trigger() : nullptr;
            return trigger ? trigger->highlight_height : 0.0f; })
        .function("type_id", +[]() -> int32_t { return static_cast<int32_t>(nw::ObjectType::trigger); })
        .finalize();
}

} // namespace nw::smalls
