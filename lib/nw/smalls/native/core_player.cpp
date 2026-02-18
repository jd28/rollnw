#include "../runtime.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_player(Runtime& rt)
{
    if (rt.get_native_module("core.player")) {
        return;
    }

    rt.module("core.player")
        .function("get_hp_current", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* player = base ? base->as_player() : nullptr;
            return player ? player->hp_current : 0; })
        .function("get_hp_max", +[](nw::ObjectHandle obj) -> int32_t {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* player = base ? base->as_player() : nullptr;
            return player ? player->hp_max : 0; })
        .function("is_pc", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            auto* player = base ? base->as_player() : nullptr;
            return player && player->pc; })
        .finalize();
}

} // namespace nw::smalls
