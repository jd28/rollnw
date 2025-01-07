#include "functions.hpp"

#include "kernel/EffectSystem.hpp"
#include "kernel/EventSystem.hpp"
#include "objects/Creature.hpp"
#include "scriptapi.hpp"

namespace nw {

// == Effects =================================================================
// ============================================================================

int effect_extract_int0(const nw::EffectHandle& handle)
{
    return handle.effect ? handle.effect->get_int(0) : 0;
}

int queue_remove_effect_by(nw::ObjectBase* obj, nw::ObjectHandle creator)
{
    if (!obj) { return 0; }
    int processed = 0;
    for (const auto& handle : obj->effects()) {
        if (handle.effect->creator == creator) {
            nw::kernel::events().add(nw::kernel::EventType::remove_effect, obj, handle.effect);
            ++processed;
        }
    }
    return processed;
}

// == Item Properties =========================================================
// ============================================================================

int process_item_properties(nw::Creature* obj, const nw::Item* item, nw::EquipIndex index, bool remove)
{
    if (!obj || !item) { return 0; }

    int processed = 0;
    if (!remove) {
        for (const auto& ip : item->properties) {
            if (auto eff = nw::kernel::effects().generate(ip, index, item->baseitem)) {
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
