#include "../stdlib.hpp"

#include "../../rules/effects.hpp"

namespace nw::smalls {

void register_core_effects(Runtime& rt)
{
    if (rt.get_native_module("core.effects")) {
        return;
    }

    rt.module("core.effects")
        .handle_type("Effect", nw::RuntimeObjectPool::TYPE_EFFECT)
        .function("create", +[]() -> nw::TypedHandle {
            auto* eff = nw::kernel::effects().create(nw::EffectType::invalid());
            if (!eff) { return {}; }
            return eff->handle().to_typed_handle();
        })
        .function("apply", +[](nw::TypedHandle eff) {
            auto* e = nw::kernel::effects().get(eff);
            if (!e) {
                return;
            }

            nw::TypedHandle typed = e->handle().to_typed_handle();
            nw::kernel::runtime().set_handle_ownership(typed, OwnershipMode::ENGINE_OWNED);
        })
        .finalize();
}

} // namespace nw::smalls
