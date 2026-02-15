#include "functions.hpp"

#include "kernel/EventSystem.hpp"
#include "objects/Creature.hpp"
#include "rules/effects.hpp"
#include "scriptapi.hpp"
#include "smalls/runtime.hpp"

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
        if (handle.effect->handle().creator == creator) {
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

    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;

    auto creature_value = nw::smalls::Value::make_object(obj->handle());
    creature_value.type_id = rt.object_subtype_for_tag(obj->handle().type);
    args.push_back(creature_value);

    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(index)));
    args.push_back(nw::smalls::Value::make_bool(remove));

    auto result = rt.execute_script("core.item", "process_item_properties", args);
    if (result.ok()) { return result.value.data.ival; }
    LOG_F(ERROR, "[functions] process_item_properties failed: {}", result.error_message);
    return 0;
}

} // namespace nw
