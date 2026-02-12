#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_sound(Runtime& rt)
{
    if (rt.get_native_module("core.sound")) {
        return;
    }

    rt.module("core.sound")
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("is_sound", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_sound(); })
        .function("as_sound", +[](nw::ObjectHandle obj) -> nw::ObjectHandle {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return (base && base->as_sound()) ? obj : nw::ObjectHandle{}; })
        .function("get_sound_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* sound = base ? base->as_sound() : nullptr;
            return sound ? static_cast<int32_t>(sound->sounds.size()) : 0; })
        .function("is_active", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* sound = base ? base->as_sound() : nullptr;
            return sound && sound->active; })
        .function("is_looping", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* sound = base ? base->as_sound() : nullptr;
            return sound && sound->looping; })
        .function("type_id", +[]() -> int32_t { return static_cast<int32_t>(nw::ObjectType::sound); })
        .finalize();
}

} // namespace nw::smalls
