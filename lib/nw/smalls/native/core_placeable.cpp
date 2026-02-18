#include "../runtime.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_placeable(Runtime& rt)
{
    if (rt.get_native_module("core.placeable")) {
        return;
    }

    rt.module("core.placeable")
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
        .finalize();
}

} // namespace nw::smalls
