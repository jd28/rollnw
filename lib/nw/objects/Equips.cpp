#include "Equips.hpp"

#include "../rules/effects.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "../util/profile.hpp"
#include "Creature.hpp"
#include "Item.hpp"
#include "ObjectManager.hpp"

#include <nlohmann/json.hpp>

namespace nw {

namespace {

Item* item_from_handle(ObjectHandle handle) noexcept
{
    if (handle.type != ObjectType::item) { return nullptr; }
    return nw::kernel::objects().get<Item>(handle);
}

} // namespace

Item* equip_item_ptr(const EquipItem& item) noexcept
{
    if (!item.is<ObjectHandle>()) { return nullptr; }
    return item_from_handle(item.as<ObjectHandle>());
}

Equips::Equips(Creature* owner, nw::MemoryResource* allocator)
    : owner_{owner}
{
}

void Equips::destroy()
{
    for (auto& e : equips) {
        if (auto* item = equip_item_ptr(e)) {
            nw::kernel::objects().destroy(item->handle());
        }
    }
    equips = std::array<EquipItem, 18>{};
}

bool Equips::instantiate()
{
    NW_PROFILE_SCOPE_N("Equips::instantiate");
    ERRARE("[objects] instantiating creature equipment");
    for (auto& e : equips) {
        if (e.is<Resref>() && e.as<Resref>().length()) {
            auto temp = nw::kernel::objects().load<Item>(e.as<Resref>());
            if (temp) {
                e = temp->handle();
            } else {
                LOG_F(WARNING, "failed to instantiate item, perhaps you're missing '{}.uti'?", e.as<Resref>());
                e = EquipItem{};
            }
        }
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
                    auto item = kernel::objects().load_instance<Item>(archive.at(lookup));
                    if (item) {
                        equips[i] = item->handle();
                    }
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
            } else if (auto* item = equip_item_ptr(equips[i])) {
                j[lookup] = item->common.resref;
            }
        } else {
            if (auto* item = equip_item_ptr(equips[i])) {
                serialize(item, j[lookup], profile);
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
            auto item = kernel::objects().load_instance<Item>(st);
            if (item) {
                self.equips[idx] = item->handle();
            }
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
            } else if (auto* item = equip_item_ptr(equip)) {
                list.push_back(struct_id).add_field("EquippedRes",
                    item->common.resref);
            }
        } else if (auto* item = equip_item_ptr(equip)) {
            serialize(item, list.push_back(struct_id), profile);
        }
        ++i;
    }

    return true;
}

} // namespace nw
