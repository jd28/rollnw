#include "scriptbridge.hpp"

#include "scriptapi.hpp"

#include "../../functions.hpp"
#include "../../kernel/Config.hpp"
#include "../../kernel/Kernel.hpp"
#include "../../kernel/Rules.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectBase.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../rules/combat_scheduler.hpp"
#include "../../scriptapi.hpp"
#include "../../smalls/Array.hpp"

namespace nwn1::bridge {
namespace {

bool is_int_compatible_type(nw::smalls::Runtime& rt, nw::smalls::TypeID type_id) noexcept
{
    while (type_id != rt.int_type()) {
        const auto* type = rt.get_type(type_id);
        if (!type || type->type_kind != nw::smalls::TK_alias || type->type_params.empty()) {
            return false;
        }

        const auto alias_target = type->type_params[0];
        if (!alias_target.is<nw::smalls::TypeID>()) {
            return false;
        }

        type_id = alias_target.as<nw::smalls::TypeID>();
    }
    return true;
}

} // namespace

bool ensure_nwn1_smalls_initialized()
{
    auto& rt = nw::kernel::runtime();
    auto result = rt.execute_script("nwn1.init", "init");
    if (!result.ok()) {
        LOG_F(WARNING, "[nwn1.bridge] nwn1.init.init failed: {}", result.error_message);
        return false;
    }
    return true;
}

nw::smalls::Value make_object_arg(nw::ObjectHandle handle)
{
    auto& rt = nw::kernel::runtime();
    auto value = nw::smalls::Value::make_object(handle);
    value.type_id = rt.object_subtype_for_tag(handle.type);
    return value;
}

nw::smalls::Value make_nullable_object_arg(const nw::ObjectBase* obj)
{
    auto& rt = nw::kernel::runtime();
    if (!obj) {
        auto value = nw::smalls::Value::make_object(nw::ObjectHandle{});
        value.type_id = rt.object_type();
        return value;
    }
    return make_object_arg(obj->handle());
}

std::optional<int32_t> call_nwn1_creature_int(nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args)
{
    return call_nwn1_module_int("nwn1.creature", fn, args);
}

std::optional<int32_t> call_nwn1_module_int(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args)
{
    if (!ensure_nwn1_smalls_initialized()) {
        return std::nullopt;
    }

    auto& rt = nw::kernel::runtime();
    auto result = rt.execute_script(module, fn, args);
    if (!result.ok()) {
        LOG_F(WARNING, "[nwn1.bridge] {}.{} failed: {}", module, fn, result.error_message);
        return std::nullopt;
    }

    if (!is_int_compatible_type(rt, result.value.type_id)) {
        LOG_F(WARNING, "[nwn1.bridge] {}.{} returned non-int-compatible type", module, fn);
        return std::nullopt;
    }

    return result.value.data.ival;
}

std::optional<nw::smalls::Value> call_nwn1_module_value(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args)
{
    if (!ensure_nwn1_smalls_initialized()) {
        return std::nullopt;
    }

    auto& rt = nw::kernel::runtime();
    auto result = rt.execute_script(module, fn, args);
    if (!result.ok()) {
        LOG_F(WARNING, "[nwn1.bridge] {}.{} failed: {}", module, fn, result.error_message);
        return std::nullopt;
    }

    return result.value;
}

std::optional<bool> call_nwn1_module_bool(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args)
{
    auto result = call_nwn1_module_value(module, fn, args);
    if (!result) {
        return std::nullopt;
    }

    auto& rt = nw::kernel::runtime();
    if (result->type_id == rt.bool_type()) {
        return result->data.bval;
    }
    if (is_int_compatible_type(rt, result->type_id)) {
        return result->data.ival != 0;
    }

    LOG_F(WARNING, "[nwn1.bridge] {}.{} returned non-bool-compatible type", module, fn);
    return std::nullopt;
}

bool call_nwn1_module_void(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args)
{
    if (!ensure_nwn1_smalls_initialized()) {
        return false;
    }

    auto& rt = nw::kernel::runtime();
    auto result = rt.execute_script(module, fn, args);
    if (!result.ok()) {
        LOG_F(WARNING, "[nwn1.bridge] {}.{} failed: {}", module, fn, result.error_message);
        return false;
    }

    return true;
}

} // namespace nwn1::bridge

namespace nwn1 {

int get_current_hitpoints(const nw::ObjectBase* obj)
{
    if (!obj) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    if (auto bridged = bridge::call_nwn1_module_int("nwn1.hitpoints", "get_current_hitpoints", args)) {
        return *bridged;
    }

    return 0;
}

int get_max_hitpoints(const nw::ObjectBase* obj)
{
    if (!obj) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    if (auto bridged = bridge::call_nwn1_module_int("nwn1.hitpoints", "get_max_hitpoints", args)) {
        return *bridged;
    }

    return 0;
}

int saving_throw(const nw::ObjectBase* obj, nw::Save type, nw::SaveVersus type_vs,
    const nw::ObjectBase* versus, bool base)
{
    if (!obj) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    args.push_back(nw::smalls::Value::make_int(*type));
    args.push_back(nw::smalls::Value::make_int(*type_vs));
    args.push_back(bridge::make_nullable_object_arg(versus));
    args.push_back(nw::smalls::Value::make_bool(base));

    if (auto bridged = bridge::call_nwn1_module_int("nwn1.saving_throws", "get_saving_throw", args)) {
        return *bridged;
    }

    return 0;
}

int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base)
{
    if (!obj || ability == nw::Ability::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    args.push_back(nw::smalls::Value::make_int(*ability));
    args.push_back(nw::smalls::Value::make_bool(base));
    if (auto bridged = bridge::call_nwn1_creature_int("get_ability_score", args)) {
        return *bridged;
    }

    return 0;
}

int get_spell_dc(const nw::Creature* obj, nw::Class class_, nw::Spell spell)
{
    if (!obj || class_ == nw::Class::invalid() || spell == nw::Spell::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    args.push_back(nw::smalls::Value::make_int(*class_));
    args.push_back(nw::smalls::Value::make_int(*spell));
    if (auto bridged = bridge::call_nwn1_creature_int("get_spell_dc", args)) {
        return *bridged;
    }

    return 0;
}

int get_dex_modifier(const nw::Creature* obj)
{
    if (!obj) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    if (auto bridged = bridge::call_nwn1_creature_int("get_dex_modifier", args)) {
        return *bridged;
    }

    return 0;
}

int calculate_item_ac(const nw::Item* obj)
{
    if (!obj) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    if (auto bridged = bridge::call_nwn1_module_int("nwn1.item", "calculate_item_ac", args)) {
        return *bridged;
    }

    return 0;
}

std::pair<bool, int> can_use_monk_abilities(const nw::Creature* obj)
{
    if (!obj) { return {false, 0}; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    if (auto bridged = bridge::call_nwn1_creature_int("get_monk_ability_level", args)) {
        if (*bridged > 0) {
            return {true, *bridged};
        }
    }

    return {false, 0};
}

int get_skill_rank(const nw::Creature* obj, nw::Skill skill, nw::ObjectBase* versus, bool base)
{
    if (!obj) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    args.push_back(nw::smalls::Value::make_int(*skill));
    if (base) {
        if (auto bridged = bridge::call_nwn1_creature_int("get_skill_rank", args)) {
            return *bridged;
        }
    } else {
        args.push_back(bridge::make_nullable_object_arg(versus));
        if (auto bridged = bridge::call_nwn1_creature_int("get_skill_rank_full", args)) {
            return *bridged;
        }
    }

    return 0;
}

bool equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot)
{
    if (!obj || !item) { return false; }

    auto& rt = nw::kernel::runtime();

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    args.push_back(bridge::make_object_arg(item->handle()));
    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(slot)));

    if (auto result = bridge::call_nwn1_module_value("core.creature", "equip_item", args)) {
        if (result->type_id == rt.bool_type()) {
            return result->data.bval;
        }
        if (result->type_id == rt.int_type()) {
            return result->data.ival != 0;
        }
    }

    return false;
}

nw::Item* unequip_item(nw::Creature* obj, nw::EquipIndex slot)
{
    if (!obj) { return nullptr; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(slot)));

    if (auto result = bridge::call_nwn1_module_value("core.creature", "unequip_item", args)) {
        auto handle = result->data.oval;
        if (handle.type == nw::ObjectType::item && nw::kernel::objects().valid(handle)) {
            return nw::kernel::objects().get<nw::Item>(handle);
        }
    }

    return nullptr;
}

void refresh_combat_weapon_cache(nw::Creature* obj)
{
    if (!obj) { return; }
    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    bridge::call_nwn1_module_value("nwn1.combat", "refresh_combat_weapon_cache", args);
}

} // namespace nwn1
