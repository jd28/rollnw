#include "Equips.hpp"

#include "../kernel/EffectSystem.hpp"
#include "../kernel/Objects.hpp"
#include "Creature.hpp"
#include "Item.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Equips::Equips(Creature* owner)
    : owner_{owner}
{
}

bool Equips::instantiate()
{
    int i = 0;
    for (auto& e : equips) {
        if (std::holds_alternative<Resref>(e) && std::get<Resref>(e).length()) {
            auto temp = nw::kernel::objects().load<Item>(std::get<Resref>(e).view());
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
                    std::get<Resref>(e));
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
            std::string lookup{equip_index_to_string(static_cast<EquipIndex>(i))};
            if (archive.find(lookup) != std::end(archive)) {
                if (profile == SerializationProfile::blueprint) {
                    equips[i] = archive.at(lookup).get<Resref>();
                } else {
                    auto item = kernel::objects().make<Item>();
                    Item::deserialize(item, archive.at(lookup), profile);
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
        std::string lookup{equip_index_to_string(static_cast<EquipIndex>(i))};
        if (profile == SerializationProfile::blueprint) {
            if (std::holds_alternative<Resref>(equips[i])) {
                const auto& r = std::get<Resref>(equips[i]);
                if (r.length()) {
                    j[lookup] = r;
                }
            } else if (std::get<Item*>(equips[i])) {
                j[lookup] = std::get<Item*>(equips[i])->common.resref;
            }
        } else {
            if (std::holds_alternative<Item*>(equips[i]) && std::get<Item*>(equips[i])) {
                Item::serialize(std::get<Item*>(equips[i]), j[lookup], profile);
            }
        }
    }

    return j;
}

#ifdef ROLLNW_ENABLE_LEGACY
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
            auto item = kernel::objects().make<Item>();
            deserialize(item, st, profile);
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
            if (std::holds_alternative<Resref>(equip)) {
                const auto& r = std::get<Resref>(equip);
                if (r.length()) {
                    list.push_back(struct_id).add_field("EquippedRes", r);
                }
            } else if (std::get<Item*>(equip)) {
                list.push_back(struct_id).add_field("EquippedRes",
                    std::get<Item*>(equip)->common.resref);
            }
        } else if (std::holds_alternative<Item*>(equip) && std::get<Item*>(equip)) {
            serialize(std::get<Item*>(equip), list.push_back(struct_id), profile);
        }
        ++i;
    }

    return true;
}
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
