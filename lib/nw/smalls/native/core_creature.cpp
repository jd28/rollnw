#include "../stdlib.hpp"

#include "../../objects/ObjectManager.hpp"
#include "../../profiles/nwn1/scriptapi.hpp"

namespace nw::smalls {

namespace {

nw::Creature* as_creature(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_creature();
}

nw::Item* as_item(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_item();
}

} // namespace

void register_core_creature(Runtime& rt)
{
    if (rt.get_native_module("core.creature")) {
        return;
    }

    rt.module("core.creature")
        .function("get_ability_score", +[](nw::ObjectHandle obj, int32_t ability, bool base) -> int32_t { return nwn1::get_ability_score(as_creature(obj), nw::Ability::make(ability), base); })
        .function("get_ability_modifier", +[](nw::ObjectHandle obj, int32_t ability, bool base) -> int32_t { return nwn1::get_ability_modifier(as_creature(obj), nw::Ability::make(ability), base); })
        .function("can_equip_item", +[](nw::ObjectHandle creature, nw::ObjectHandle item, int32_t slot) -> bool { return nwn1::can_equip_item(as_creature(creature), as_item(item), static_cast<nw::EquipIndex>(slot)); })
        .function("equip_item", +[](nw::ObjectHandle creature, nw::ObjectHandle item, int32_t slot) -> bool { return nwn1::equip_item(as_creature(creature), as_item(item), static_cast<nw::EquipIndex>(slot)); })
        .function("get_equipped_item", +[](nw::ObjectHandle creature, int32_t slot) -> nw::ObjectHandle {
            auto* item = nwn1::get_equipped_item(as_creature(creature), static_cast<nw::EquipIndex>(slot));
            return item ? item->handle() : nw::ObjectHandle{}; })
        .function("unequip_item", +[](nw::ObjectHandle creature, int32_t slot) -> nw::ObjectHandle {
            auto* item = nwn1::unequip_item(as_creature(creature), static_cast<nw::EquipIndex>(slot));
            return item ? item->handle() : nw::ObjectHandle{}; })
        .finalize();
}

} // namespace nw::smalls
