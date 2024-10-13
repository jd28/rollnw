#include "Equips.hpp"

#include "../kernel/EffectSystem.hpp"
#include "../kernel/Objects.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "Creature.hpp"
#include "Item.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Equips::Equips(Creature* owner)
    : owner_{owner}
{
}

void Equips::destroy()
{
    for (auto& e : equips) {
        if (e.is<nw::Item*>()) {
            nw::kernel::objects().destroy(e.as<nw::Item*>()->handle());
        }
    }
}

bool Equips::instantiate()
{
    int i = 0;
    for (auto& e : equips) {
        if (e.is<Resref>() && e.as<Resref>().length()) {
            auto temp = nw::kernel::objects().load<Item>(e.as<Resref>().view());
            if (temp) {
                e = temp;
                for (const auto& ip : temp->properties) {
                    if (auto eff = nw::kernel::effects().generate(ip,
                            static_cast<EquipIndex>(i), temp->baseitem)) {
                        eff->creator = temp->handle();
                        eff->category = nw::EffectCategory::item;
                        if (!nw::kernel::effects().apply(owner_, eff)) {
                            nw::kernel::effects().destroy(eff);
                        } else {
                            owner_->effects().add(eff);
                        }
                    }
                }

            } else {
                e = static_cast<nw::Item*>(nullptr);
                LOG_F(WARNING, "failed to instantiate item, perhaps you're missing '{}.uti'?",
                    e.as<Resref>());
            }
        }
        ++i;
    }
    return true;
}

bool Equips::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        for (size_t i = 0; i < 18; ++i) {
            String lookup{equip_index_to_string(static_cast<EquipIndex>(i))};
            if (archive.find(lookup) != std::end(archive)) {
                if (profile == SerializationProfile::blueprint) {
                    equips[i] = archive.at(lookup).get<Resref>();
                } else {
                    auto item = kernel::objects().load<Item>(archive.at(lookup));
                    equips[i] = item;
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "from_json exception: {}", e.what());
        return false;
    }
    return true;
}

nlohmann::json Equips::to_json(SerializationProfile profile) const
{
    nlohmann::json j;

    for (size_t i = 0; i < 18; ++i) {
        String lookup{equip_index_to_string(static_cast<EquipIndex>(i))};
        if (profile == SerializationProfile::blueprint) {
            if (equips[i].is<Resref>()) {
                const auto& r = equips[i].as<Resref>();
                if (r.length()) {
                    j[lookup] = r;
                }
            } else if (equips[i].is<Item*>()) {
                j[lookup] = equips[i].as<Item*>()->common.resref;
            }
        } else {
            if (equips[i].is<Item*>() && equips[i].as<Item*>()) {
                Item::serialize(equips[i].as<Item*>(), j[lookup], profile);
            }
        }
    }

    return j;
}

bool deserialize(Equips& self, const GffStruct& archive, SerializationProfile profile)
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
            self.equips[idx] = r;
        } else {
            auto item = kernel::objects().load<Item>(st);
            self.equips[idx] = item;
        }
    }
    return true;
}

bool serialize(const Equips& self, GffBuilderStruct& archive, SerializationProfile profile)
{
    auto& list = archive.add_list("Equip_ItemList");
    size_t i = 0;
    for (const auto& equip : self.equips) {
        uint32_t struct_id = 1 << i;
        if (profile == SerializationProfile::blueprint) {
            if (equip.is<Resref>()) {
                const auto& r = equip.as<Resref>();
                if (r.length()) {
                    list.push_back(struct_id).add_field("EquippedRes", r);
                }
            } else if (equip.is<Item*>()) {
                list.push_back(struct_id).add_field("EquippedRes",
                    equip.as<Item*>()->common.resref);
            }
        } else if (equip.is<Item*>() && equip.as<Item*>()) {
            serialize(equip.as<Item*>(), list.push_back(struct_id), profile);
        }
        ++i;
    }

    return true;
}

} // namespace nw
