#include "functions.hpp"

#include "kernel/EventSystem.hpp"
#include "kernel/Kernel.hpp"
#include "objects/ObjectBase.hpp"

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
        auto* effect = handle.effect ? handle.effect : nw::kernel::effects().get(handle.runtime_handle);
        if (effect && effect->handle().creator == creator) {
            nw::kernel::events().add(nw::kernel::EventType::remove_effect, obj, effect);
            ++processed;
        }
    }
    return processed;
}

} // namespace nw
