#include "functions.hpp"

#include "components/Creature.hpp"
#include "kernel/EffectSystem.hpp"

namespace nw {

// == Effects =================================================================
// ============================================================================

bool apply_effect(nw::ObjectBase* obj, nw::Effect* effect)
{
    if (!obj || !effect) { return false; }
    if (nw::kernel::effects().apply(obj, effect)) {
        obj->effects().add(effect);
        return true;
    }
    return false;
}

int effect_extract_int0(const nw::EffectHandle& handle)
{
    return handle.effect ? handle.effect->get_int(0) : 0;
}

bool has_effect_applied(nw::ObjectBase* obj, nw::EffectType type, int subtype)
{
    if (!obj || type == nw::EffectType::invalid()) { return false; }
    auto it = find_first_effect_of(std::begin(obj->effects()), std::end(obj->effects()),
        type, subtype);

    return it != std::end(obj->effects());
}

bool remove_effect(nw::ObjectBase* obj, nw::Effect* effect, bool destroy)
{
    if (nw::kernel::effects().remove(obj, effect)) {
        obj->effects().remove(effect);
        if (destroy) { nw::kernel::effects().destroy(effect); }
        return true;
    }
    return false;
}

int remove_effects_by(nw::ObjectBase* obj, nw::ObjectHandle creator)
{
    int result = 0;
    auto it = std::remove_if(std::begin(obj->effects()), std::end(obj->effects()),
        [&](const nw::EffectHandle& handle) {
            if (handle.creator == creator && nw::kernel::effects().remove(obj, handle.effect)) {
                nw::kernel::effects().destroy(handle.effect);
                ++result;
                return true;
            }
            return false;
        });

    obj->effects().erase(it, std::end(obj->effects()));

    return result;
}

// == Item Properties =========================================================
// ============================================================================

int process_item_properties(nw::Creature* obj, const nw::Item* item, nw::EquipIndex index, bool remove)
{
    if (!obj || !item) { return 0; }

    int processed = 0;
    if (!remove) {
        for (const auto& ip : item->properties) {
            if (auto eff = nw::kernel::effects().generate(ip, index)) {
                eff->creator = item->handle();
                eff->category = nw::EffectCategory::item;
                if (!apply_effect(obj, eff)) {
                    nw::kernel::effects().destroy(eff);
                }
                ++processed;
            }
        }
        return processed;
    } else {
        return remove_effects_by(obj, item->handle());
    }
}

} // namespace nw