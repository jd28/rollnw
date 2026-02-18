#include "../stdlib.hpp"

#include "../../rules/effects.hpp"

#include <algorithm>

namespace nw::smalls {


void register_core_effects(Runtime& rt)
{
    if (rt.get_native_module("core.effects")) {
        return;
    }

    rt.module("core.effects")
        .handle_type("Effect", nw::RuntimeObjectPool::TYPE_EFFECT)
        .function("invalid", +[]() -> nw::TypedHandle { return {}; })
        .function("create", +[]() -> nw::TypedHandle {
            auto* eff = nw::kernel::effects().create(nw::EffectType::invalid());
            if (!eff) { return {}; }
            return eff->handle().to_typed_handle(); })
        .function("apply", +[](nw::TypedHandle eff) {
            auto* e = nw::kernel::effects().get(eff);
            if (!e) {
                return;
            }

            nw::TypedHandle typed = e->handle().to_typed_handle();
            nw::kernel::runtime().set_handle_ownership(typed, OwnershipMode::ENGINE_OWNED); })
        .function("is_valid", +[](nw::TypedHandle eff) -> bool { return nw::kernel::effects().get(eff) != nullptr; })
        .function("destroy", +[](nw::TypedHandle eff) {
            auto* e = nw::kernel::effects().get(eff);
            if (!e) {
                return;
            }

            auto mode = nw::kernel::runtime().handle_ownership(eff);
            if (!mode || *mode != OwnershipMode::VM_OWNED) {
                return;
            }

            nw::kernel::effects().destroy(e); })
        .function("get_int", +[](nw::TypedHandle eff, int32_t index) -> int32_t {
            auto* e = nw::kernel::effects().get(eff);
            if (!e || index < 0) {
                return 0;
            }
            return e->get_int(static_cast<size_t>(index)); })
        .function("set_int", +[](nw::TypedHandle eff, int32_t index, int32_t value) {
            auto* e = nw::kernel::effects().get(eff);
            if (!e || index < 0) {
                return;
            }
            e->set_int(static_cast<size_t>(index), value); })
        .function("set_ints", +[](nw::TypedHandle eff, IArray* values) {
            auto* e = nw::kernel::effects().get(eff);
            if (!e || !values) {
                return;
            }

            const size_t count = std::min(values->size(), size_t(nw::Effect::ints_count));
            Value temp;
            auto& rt = nw::kernel::runtime();
            for (size_t i = 0; i < count; ++i) {
                if (!values->get_value(i, temp, rt)) {
                    break;
                }
                if (temp.type_id != rt.int_type()) {
                    continue;
                }
                e->set_int(i, temp.data.ival);
            } })
        .function("get_type", +[](nw::TypedHandle eff) -> int32_t {
            auto* e = nw::kernel::effects().get(eff);
            if (!e) {
                return -1;
            }
            return *e->handle().type; })
        .function("set_type", +[](nw::TypedHandle eff, int32_t effect_type) {
            auto* e = nw::kernel::effects().get(eff);
            if (!e) {
                return;
            }
            e->handle().type = nw::EffectType::make(effect_type); })
        .function("get_subtype", +[](nw::TypedHandle eff) -> int32_t {
            auto* e = nw::kernel::effects().get(eff);
            if (!e) {
                return -1;
            }
            return e->handle().subtype; })
        .function("set_subtype", +[](nw::TypedHandle eff, int32_t subtype) {
            auto* e = nw::kernel::effects().get(eff);
            if (!e) {
                return;
            }
            e->handle().subtype = subtype; })
        .finalize();
}

} // namespace nw::smalls
