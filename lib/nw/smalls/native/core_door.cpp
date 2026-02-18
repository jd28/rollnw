#include "../runtime.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_door(Runtime& rt)
{
    if (rt.get_native_module("core.door")) {
        return;
    }

    rt.module("core.door")
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("is_door", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_door(); })
        .function("as_door", +[](nw::ObjectHandle obj) -> nw::ObjectHandle {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return (base && base->as_door()) ? obj : nw::ObjectHandle{}; })
        .function("get_hp", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* door = base ? base->as_door() : nullptr;
            return door ? door->hp : 0; })
        .function("get_hp_current", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* door = base ? base->as_door() : nullptr;
            return door ? door->hp_current : 0; })
        .function("is_open", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* door = base ? base->as_door() : nullptr;
            return door && door->animation_state != nw::DoorAnimationState::closed; })
        .function("type_id", +[]() -> int32_t { return static_cast<int32_t>(nw::ObjectType::door); })
        .finalize();
}

} // namespace nw::smalls
