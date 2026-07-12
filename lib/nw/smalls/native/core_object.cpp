#include "../stdlib.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/ObjectBase.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../rules/effects.hpp"

namespace nw::smalls {

void register_core_object(Runtime& rt)
{
    if (rt.get_native_module("core.object")) {
        return;
    }

    rt.module("core.object")
        .function("__invalid", +[]() -> nw::ObjectHandle { return nw::ObjectHandle{}; })
        .function("is_valid", +[](nw::ObjectHandle obj) -> bool { return nw::kernel::objects().get_object_base(obj) != nullptr; })
        .function("apply_effect", +[](nw::ObjectHandle obj, nw::TypedHandle eff) -> bool {
            auto* target = nw::kernel::objects().get_object_base(obj);
            auto* effect = nw::kernel::effects().get(eff);
            if (!target || !effect) {
                return false;
            }

            bool applied = nw::kernel::effects().apply_to(target, effect);
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

            for (const auto& applied : target->effects()) {
                if (applied.type == effect->handle().type && applied.subtype == effect->handle().subtype) {
                    return true;
                }
            }
            return false; })
        .function("remove_effect", +[](nw::ObjectHandle obj, nw::TypedHandle eff) -> bool {
            auto* target = nw::kernel::objects().get_object_base(obj);
            auto* effect = nw::kernel::effects().get(eff);
            if (!target || !effect) {
                return false;
            }

            bool removed = nw::kernel::effects().remove_from(target, effect);
            if (removed) {
                nw::kernel::runtime().set_handle_ownership(eff, OwnershipMode::VM_OWNED);
            }
            return removed; })
        .function("is_creature", +[](nw::ObjectHandle obj) -> bool {
            auto* base = nw::kernel::objects().get_object_base(obj);
            return base && base->as_creature(); })
        .function("get_position", +[](nw::ObjectHandle obj) -> glm::vec3 {
            auto* row = nw::kernel::objects().components().get_or_create_spatial(obj);
            return row ? row->position : glm::vec3{0.0f}; })
        .function("set_position", +[](nw::ObjectHandle obj, glm::vec3 position) -> bool { return nw::kernel::objects().components().set_position(obj, position); })
        .function("get_orientation", +[](nw::ObjectHandle obj) -> glm::vec3 {
            auto* row = nw::kernel::objects().components().get_or_create_spatial(obj);
            return row ? row->orientation : glm::vec3{0.0f}; })
        .function("set_orientation", +[](nw::ObjectHandle obj, glm::vec3 orientation) -> bool { return nw::kernel::objects().components().set_orientation(obj, orientation); })
        .function("get_scale", +[](nw::ObjectHandle obj) -> glm::vec3 {
            auto* row = nw::kernel::objects().components().get_or_create_spatial(obj);
            return row ? row->scale : glm::vec3{1.0f}; })
        .function("set_scale", +[](nw::ObjectHandle obj, glm::vec3 scale) -> bool { return nw::kernel::objects().components().set_scale(obj, scale); })
        .function("get_velocity", +[](nw::ObjectHandle obj) -> glm::vec3 {
            auto* row = nw::kernel::objects().components().get_or_create_spatial(obj);
            return row ? row->velocity : glm::vec3{0.0f}; })
        .function("set_velocity", +[](nw::ObjectHandle obj, glm::vec3 velocity) -> bool { return nw::kernel::objects().components().set_velocity(obj, velocity); })
        .function("get_angular_velocity", +[](nw::ObjectHandle obj) -> glm::vec3 {
            auto* row = nw::kernel::objects().components().get_or_create_spatial(obj);
            return row ? row->angular_velocity : glm::vec3{0.0f}; })
        .function("set_angular_velocity", +[](nw::ObjectHandle obj, glm::vec3 velocity) -> bool { return nw::kernel::objects().components().set_angular_velocity(obj, velocity); })
        .finalize();
}

} // namespace nw::smalls
