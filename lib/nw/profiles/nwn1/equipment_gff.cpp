#include "equipment_gff.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/Equips.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../serialization/Gff.hpp"
#include "../../serialization/GffBuilder.hpp"

namespace nwn1 {

bool import_creature_equipment_from_gff(
    nw::Equips& equipment,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile)
{
    const size_t sz = archive["Equip_ItemList"].size();
    for (size_t i = 0; i < sz; ++i) {
        auto st = archive["Equip_ItemList"][i];
        auto slot = static_cast<nw::EquipSlot>(st.id());
        auto index = nw::equip_slot_to_index(slot);
        if (index == nw::EquipIndex::invalid) {
            LOG_F(ERROR, "equips invalid equipment slot: {}", st.id());
            return false;
        }

        const uint32_t idx = static_cast<uint32_t>(index);
        if (profile == nw::SerializationProfile::blueprint) {
            nw::Resref r;
            st.get_to("EquippedRes", r);
            equipment.equips[idx] = r;
        } else {
            auto item = nw::kernel::objects().load_instance<nw::Item>(st);
            if (item) {
                equipment.equips[idx] = item->handle();
            }
        }
    }
    return true;
}

bool export_creature_equipment_to_gff(
    const nw::Equips& equipment,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile)
{
    auto& list = archive.add_list("Equip_ItemList");
    size_t i = 0;
    for (const auto& equip : equipment.equips) {
        const uint32_t struct_id = 1u << i;
        if (profile == nw::SerializationProfile::blueprint) {
            if (equip.is<nw::Resref>()) {
                const auto& r = equip.as<nw::Resref>();
                if (r.length()) {
                    list.push_back(struct_id).add_field("EquippedRes", r);
                }
            } else if (auto* item = nw::equip_item_ptr(equip)) {
                list.push_back(struct_id).add_field("EquippedRes", item->resref);
            }
        } else if (auto* item = nw::equip_item_ptr(equip)) {
            nw::serialize(item, list.push_back(struct_id), profile);
        }
        ++i;
    }

    return true;
}

} // namespace nwn1
