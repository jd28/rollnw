#pragma once

#include "../../objects/ObjectHandle.hpp"
#include "../../rules/attributes.hpp"
#include "../../rules/combat.hpp"
#include "../../smalls/runtime.hpp"
#include "../../util/string.hpp"

#include <optional>
#include <utility>

namespace nw {
struct Creature;
struct Item;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

int get_current_hitpoints(const nw::ObjectBase* obj);
int get_max_hitpoints(const nw::ObjectBase* obj);

int saving_throw(const nw::ObjectBase* obj, nw::Save type,
    nw::SaveVersus type_vs = nw::SaveVersus::invalid(),
    const nw::ObjectBase* versus = nullptr, bool base = false);

int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base = false);
int get_dex_modifier(const nw::Creature* obj);
int calculate_item_ac(const nw::Item* obj);
std::pair<bool, int> can_use_monk_abilities(const nw::Creature* obj);
int get_skill_rank(const nw::Creature* obj, nw::Skill skill,
    nw::ObjectBase* versus = nullptr, bool base = false);

bool equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot);
nw::Item* unequip_item(nw::Creature* obj, nw::EquipIndex slot);
void refresh_combat_weapon_cache(nw::Creature* obj);

} // namespace nwn1

namespace nwn1::bridge {

bool ensure_nwn1_smalls_initialized();

nw::smalls::Value make_object_arg(nw::ObjectHandle handle);
nw::smalls::Value make_nullable_object_arg(const nw::ObjectBase* obj);

std::optional<int32_t> call_nwn1_creature_int(nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args);

std::optional<int32_t> call_nwn1_module_int(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args);

std::optional<bool> call_nwn1_module_bool(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args);

bool call_nwn1_module_void(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args);

} // namespace nwn1::bridge
