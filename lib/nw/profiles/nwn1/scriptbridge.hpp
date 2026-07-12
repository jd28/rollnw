#pragma once

#include "../../objects/ObjectHandle.hpp"
#include "../../rules/combat.hpp"
#include "../../smalls/runtime.hpp"
#include "../../util/string.hpp"

#include <optional>

namespace nw {
struct Creature;
struct Item;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

bool equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot);
nw::Item* unequip_item(nw::Creature* obj, nw::EquipIndex slot);
int process_item_properties(nw::Creature* obj, const nw::Item* item, nw::EquipIndex index, bool remove);
void refresh_combat_weapon_cache(nw::Creature* obj);

} // namespace nwn1

namespace nwn1::bridge {

bool ensure_nwn1_smalls_initialized();

nw::smalls::Value make_object_arg(nw::ObjectHandle handle);

std::optional<int32_t> call_nwn1_module_int(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args);

std::optional<float> call_nwn1_module_float(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args);

std::optional<bool> call_nwn1_module_bool(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args);

bool call_nwn1_module_void(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args);

} // namespace nwn1::bridge
