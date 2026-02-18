#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"
#include "../../scriptapi.hpp"

namespace nw::smalls {

void register_core_object(Runtime& rt)
{
    if (rt.get_native_module("core.object")) {
        return;
    }

    rt.module("core.object")
        .function("apply_effect", +[](nw::ObjectHandle obj, nw::TypedHandle eff) -> bool {
            auto* target = nw::kernel::objects().get_object_base(obj);
            auto* effect = nw::kernel::effects().get(eff);
            if (!target || !effect) {
                return false;
            }

            bool applied = nw::apply_effect(target, effect);
            if (applied) {
                nw::kernel::runtime().set_handle_ownership(eff, OwnershipMode::ENGINE_OWNED);
            }
            return applied; })
        .function("has_effect_applied", +[](nw::ObjectHandle obj, nw::TypedHandle eff) -> bool {
            auto* target = nw::kernel::objects().get_object_base(obj);
            auto* effect = nw::kernel::effects().get(eff);
            if (!target || !effect) {
                return false;
            }

            return nw::has_effect_applied(target, effect->handle().type, effect->handle().subtype); })
        .function("remove_effect", +[](nw::ObjectHandle obj, nw::TypedHandle eff) -> bool {
            auto* target = nw::kernel::objects().get_object_base(obj);
            auto* effect = nw::kernel::effects().get(eff);
            if (!target || !effect) {
                return false;
            }

            bool removed = nw::remove_effect(target, effect, false);
            if (removed) {
                nw::kernel::runtime().set_handle_ownership(eff, OwnershipMode::VM_OWNED);
            }
            return removed; })
        .function("is_pc", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            if (!base) {
                return false;
            }
            if (auto* creature = base->as_creature()) {
                return creature->pc;
            }
            if (auto* player = base->as_player()) {
                return player->pc;
            }
            return false; })
        .finalize();
}

} // namespace nw::smalls
