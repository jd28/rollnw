#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"

namespace nw::smalls {

void register_core_player(Runtime& rt)
{
    if (rt.get_native_module("core.player")) {
        return;
    }

    rt.module("core.player")
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("is_player", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_player(); })
        .function("as_player", +[](nw::ObjectHandle obj) -> nw::ObjectHandle {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return (base && base->as_player()) ? obj : nw::ObjectHandle{}; })
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
        .function("type_id", +[]() -> int32_t { return static_cast<int32_t>(nw::ObjectType::player); })
        .finalize();
}

} // namespace nw::smalls
