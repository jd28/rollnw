#include "Equips.hpp"

#include "../Item.hpp"

namespace nw {

Equips::Equips(const GffStruct gff, SerializationProfile profile)
{

    size_t sz = gff["Equip_ItemList"].size();
    for (size_t i = 0; i < sz; ++i) {
        auto st = gff["Equip_ItemList"][i];
        switch (static_cast<EquipSlot>(st.id())) {
        default:
            LOG_F(ERROR, "equips invalid equipment slot: {}", st.id());
            break;
        case EquipSlot::head:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                head = r;
            } else {
                head = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::chest:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                chest = r;
            } else {
                chest = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::boots:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                boots = r;
            } else {
                boots = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::arms:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                arms = r;
            } else {
                arms = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::righthand:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                righthand = r;
            } else {
                righthand = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::lefthand:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                lefthand = r;
            } else {
                lefthand = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::cloak:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                cloak = r;
            } else {
                cloak = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::leftring:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                leftring = r;
            } else {
                leftring = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::rightring:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                rightring = r;
            } else {
                rightring = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::neck:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                neck = r;
            } else {
                neck = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::belt:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                belt = r;
            } else {
                belt = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::arrows:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                arrows = r;
            } else {
                arrows = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::bullets:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                bullets = r;
            } else {
                bullets = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::bolts:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                bolts = r;
            } else {
                bolts = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::creature_left:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                creature_left = r;
            } else {
                creature_left = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::creature_right:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                creature_right = r;
            } else {
                creature_right = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::creature_bite:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                creature_bite = r;
            } else {
                creature_bite = std::make_unique<Item>(st, profile);
            }
            break;
        case EquipSlot::creature_skin:
            if (profile == SerializationProfile::blueprint) {
                Resref r;
                st.get_to("EquippedRes", r);
                creature_skin = r;
            } else {
                creature_skin = std::make_unique<Item>(st, profile);
            }
            break;
        }
    }
}

} // namespace nw
