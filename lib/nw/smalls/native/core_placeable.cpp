#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_placeable(Runtime& rt)
{
    if (rt.get_native_module("core.placeable")) {
        return;
    }

    rt.module("core.placeable")
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("is_placeable", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_placeable(); })
        .function("as_placeable", +[](nw::ObjectHandle obj) -> nw::ObjectHandle {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return (base && base->as_placeable()) ? obj : nw::ObjectHandle{}; })
        .function("get_hp", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* placeable = base ? base->as_placeable() : nullptr;
            return placeable ? placeable->hp : 0; })
        .function("get_hp_current", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* placeable = base ? base->as_placeable() : nullptr;
            return placeable ? placeable->hp_current : 0; })
        .function("has_inventory", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* placeable = base ? base->as_placeable() : nullptr;
            return placeable && placeable->has_inventory; })
        .function("type_id", +[]() -> int32_t { return static_cast<int32_t>(nw::ObjectType::placeable); })
        .finalize();
}

} // namespace nw::smalls
