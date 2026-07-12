#include "ObjectBase.hpp"

#include "../kernel/Kernel.hpp"
#include "ObjectManager.hpp"

namespace nw {

// == ObjectBase ==============================================================
// ============================================================================

ObjectBase::ObjectBase(nw::MemoryResource* allocator)
    : allocator_(allocator)
    , effects_(allocator)
{
}

void ObjectBase::clear()
{
    clear_effects();

    if (auto* objects = kernel::services().get_mut<ObjectManager>()) {
        objects->components().remove(*this);
    }

    uuid = uuids::uuid{};
    resref = Resref{};
    name = LocString{};
    tag = InternedString{};
    comment.clear();
    palette_id = 0xFF;
}

void ObjectBase::clear_effects()
{
    if (effects_.size() == 0) { return; }

    auto* effects = kernel::services().get_mut<EffectSystem>();
    if (!effects) {
        effects_.clear();
        return;
    }

    Vector<Effect*> to_remove;
    to_remove.reserve(effects_.size());
    for (const auto& handle : effects_) {
        if (auto* effect = handle.effect ? handle.effect : effects->get(handle.runtime_handle)) {
            to_remove.push_back(effect);
        }
    }

    if (!to_remove.empty()) {
        effects->remove_from(this, to_remove, true);
    }

    if (effects_.size() == 0) { return; }

    Vector<TypedHandle> stale_handles;
    stale_handles.reserve(effects_.size());
    for (const auto& handle : effects_) {
        if (handle.runtime_handle.is_valid()) {
            stale_handles.push_back(handle.runtime_handle);
        }
    }
    effects_.clear();

    for (const auto& handle : stale_handles) {
        if (auto* effect = effects->get(handle)) {
            effects->destroy(effect);
        }
    }
}

EffectArray& ObjectBase::effects() { return effects_; }
const EffectArray& ObjectBase::effects() const { return effects_; }

} // namespace nw
