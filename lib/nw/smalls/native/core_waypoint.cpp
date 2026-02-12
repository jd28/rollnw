#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_waypoint(Runtime& rt)
{
    if (rt.get_native_module("core.waypoint")) {
        return;
    }

    rt.module("core.waypoint")
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("is_waypoint", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_waypoint(); })
        .function("as_waypoint", +[](nw::ObjectHandle obj) -> nw::ObjectHandle {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return (base && base->as_waypoint()) ? obj : nw::ObjectHandle{}; })
        .function("get_appearance", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* waypoint = base ? base->as_waypoint() : nullptr;
            return waypoint ? waypoint->appearance : 0; })
        .function("has_map_note", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* waypoint = base ? base->as_waypoint() : nullptr;
            return waypoint && waypoint->has_map_note; })
        .function("is_map_note_enabled", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* waypoint = base ? base->as_waypoint() : nullptr;
            return waypoint && waypoint->map_note_enabled; })
        .function("type_id", +[]() -> int32_t { return static_cast<int32_t>(nw::ObjectType::waypoint); })
        .finalize();
}

} // namespace nw::smalls
