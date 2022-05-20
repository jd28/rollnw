#include "Equips.hpp"

#include "../../kernel/Kernel.hpp"
#include "../Item.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Equips::instantiate()
{
    for (auto& e : equips) {
        if (std::holds_alternative<Resref>(e) && std::get<Resref>(e).length()) {
            auto temp = nw::kernel::objects().load(std::get<Resref>(e).view(), ObjectType::item);
            if (temp) {
                e = temp;
            } else {
                LOG_F(WARNING, "failed to instantiate item, perhaps you're missing '{}.uti'?",
                    std::get<Resref>(e));
            }
        }
    }
    return true;
}

bool Equips::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    size_t sz = archive["Equip_ItemList"].size();
    for (size_t i = 0; i < sz; ++i) {
        auto st = archive["Equip_ItemList"][i];
        auto slot = static_cast<EquipSlot>(st.id());
        auto index = equip_slot_to_index(slot);
        if (index == EquipIndex::invalid) {
            LOG_F(ERROR, "equips invalid equipment slot: {}", st.id());
            return false;
        }
        uint32_t idx = static_cast<uint32_t>(index);
        if (profile == SerializationProfile::blueprint) {
            Resref r;
            st.get_to("EquippedRes", r);
            equips[idx] = r;
        } else {
            equips[idx] = nw::kernel::objects().deserialize(ObjectType::item, st, profile);
        }
    }
    return true;
}

bool Equips::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        for (size_t i = 0; i < 18; ++i) {
            std::string lookup{equip_index_to_string(static_cast<EquipIndex>(i))};
            if (archive.find(lookup) != std::end(archive)) {
                if (profile == SerializationProfile::blueprint) {
                    equips[i] = archive.at(lookup).get<Resref>();
                } else {
                    equips[i] = nw::kernel::objects().deserialize(ObjectType::item, archive.at(lookup), profile);
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;
}

bool Equips::to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const
{
    auto& list = archive.add_list("Equip_ItemList");
    size_t i = 0;
    for (const auto& equip : equips) {
        uint32_t struct_id = 1 << i;
        if (profile == SerializationProfile::blueprint) {
            if (std::holds_alternative<Resref>(equip)) {
                const auto& r = std::get<Resref>(equip);
                if (r.length()) {
                    list.push_back(struct_id).add_field("EquippedRes", r);
                }
            } else if (std::get<flecs::entity>(equip)) {
                list.push_back(struct_id).add_field("EquippedRes",
                    std::get<flecs::entity>(equip).get<Common>()->resref);
            }
        } else if (std::holds_alternative<flecs::entity>(equip) && std::get<flecs::entity>(equip)) {
            nw::kernel::objects().serialize(std::get<flecs::entity>(equip), list.push_back(struct_id), profile);
        }
        ++i;
    }

    return true;
}

nlohmann::json Equips::to_json(SerializationProfile profile) const
{
    nlohmann::json j;

    for (size_t i = 0; i < 18; ++i) {
        std::string lookup{equip_index_to_string(static_cast<EquipIndex>(i))};
        if (profile == SerializationProfile::blueprint) {
            if (std::holds_alternative<Resref>(equips[i])) {
                const auto& r = std::get<Resref>(equips[i]);
                if (r.length()) { j[lookup] = r; }
            } else if (std::get<flecs::entity>(equips[i])) {
                j[lookup] = std::get<flecs::entity>(equips[i]).get<Common>()->resref;
            }
        } else {
            if (std::holds_alternative<flecs::entity>(equips[i]) && std::get<flecs::entity>(equips[i]).is_alive()) {
                nw::kernel::objects().serialize(std::get<flecs::entity>(equips[i]), j[lookup], profile);
            }
        }
    }

    return j;
}

} // namespace nw
