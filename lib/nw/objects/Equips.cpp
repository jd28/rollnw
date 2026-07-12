#include "Equips.hpp"

#include "../rules/effects.hpp"
#include "../smalls/runtime.hpp"
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

bool equip_index_valid(EquipIndex slot) noexcept
{
    return static_cast<uint32_t>(slot) < 18;
}

bool serialize_item_to_json(const Item* item, nlohmann::json& out, SerializationProfile profile)
{
    bool (*serialize_json)(const Item*, nlohmann::json&, SerializationProfile) = nw::serialize;
    return serialize_json(item, out, profile);
}

} // namespace

Item* equip_item_ptr(const EquipItem& item) noexcept
{
    if (!item.is<ObjectHandle>()) { return nullptr; }
    return item_from_handle(item.as<ObjectHandle>());
}

bool can_equip_item(const Creature* obj, Item* item, EquipIndex slot)
{
    if (!obj || !item) { return false; }
    if (!equip_index_valid(slot)) { return false; }

    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, obj->handle()));
    args.push_back(nw::smalls::detail::make_value(&rt, item->handle()));
    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(slot)));

    auto result = rt.execute_script("core.item", "can_equip_item", args);
    return result.ok() && result.value.type_id == rt.bool_type() && result.value.data.bval;
}

bool equip_item_in_slot(Creature* obj, Item* item, EquipIndex slot)
{
    if (!obj || !item) { return false; }
    if (!equip_index_valid(slot)) { return false; }
    if (!can_equip_item(obj, item, slot)) { return false; }

    auto& it = obj->equipment.equips[size_t(slot)];
    if (nw::equip_item_ptr(it) == item) {
        return true;
    }

    nw::unequip_item_in_slot(obj, slot);
    obj->equipment.equips[size_t(slot)] = item->handle();
    ++obj->equipment.equip_version;
    return true;
}

Item* get_equipped_item(const Creature* obj, EquipIndex slot)
{
    Item* result = nullptr;
    if (!obj) { return result; }
    if (!equip_index_valid(slot)) { return result; }

    auto& it = obj->equipment.equips[size_t(slot)];
    result = nw::equip_item_ptr(it);
    return result;
}

Item* unequip_item_in_slot(Creature* obj, EquipIndex slot)
{
    Item* result = nullptr;
    if (!obj) { return result; }
    if (!equip_index_valid(slot)) { return result; }

    auto& it = obj->equipment.equips[size_t(slot)];
    result = nw::equip_item_ptr(it);
    if (result) {
        it = nw::EquipItem{};
        ++obj->equipment.equip_version;
    }
    return result;
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
                j[lookup] = item->resref;
            }
        } else {
            if (auto* item = equip_item_ptr(equips[i])) {
                serialize_item_to_json(item, j[lookup], profile);
            }
        }
    }

    return j;
}

} // namespace nw
