#include "scriptbridge.hpp"

#include "../../kernel/Config.hpp"
#include "../../kernel/Kernel.hpp"
#include "../../kernel/Rules.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectBase.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../rules/combat_scheduler.hpp"
#include "../../smalls/Array.hpp"
#include "../../util/profile.hpp"

namespace nwn1::bridge {
namespace {

bool is_int_compatible_type(nw::smalls::Runtime& rt, nw::smalls::TypeID type_id) noexcept
{
    while (type_id != rt.int_type()) {
        const auto* type = rt.get_type(type_id);
        if (!type) {
            return false;
        }

        if (type->type_kind != nw::smalls::TK_alias && type->type_kind != nw::smalls::TK_newtype) {
            return type->primitive_kind == nw::smalls::PK_int;
        }

        if (type->type_params.empty()) {
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

std::optional<float> call_nwn1_module_float(nw::StringView module, nw::StringView fn,
    const nw::Vector<nw::smalls::Value>& args)
{
    auto result = call_nwn1_module_value(module, fn, args);
    if (!result) {
        return std::nullopt;
    }

    auto& rt = nw::kernel::runtime();
    if (result->type_id == rt.float_type()) {
        return result->data.fval;
    }
    if (is_int_compatible_type(rt, result->type_id)) {
        return static_cast<float>(result->data.ival);
    }

    LOG_F(WARNING, "[nwn1.bridge] {}.{} returned non-float-compatible type", module, fn);
    return std::nullopt;
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

namespace {

void push_object_arg(nw::Vector<nw::smalls::Value>& args, const nw::ObjectBase* obj)
{
    args.push_back(nwn1::bridge::make_object_arg(obj->handle()));
}

} // namespace

namespace nwn1 {

bool equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot)
{
    if (!obj || !item) { return false; }

    auto& rt = nw::kernel::runtime();

    nw::Vector<nw::smalls::Value> args;
    push_object_arg(args, obj);
    push_object_arg(args, item);
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
    push_object_arg(args, obj);
    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(slot)));

    if (auto result = bridge::call_nwn1_module_value("core.creature", "unequip_item", args)) {
        auto handle = result->data.oval;
        if (handle.type == nw::ObjectType::item && nw::kernel::objects().valid(handle)) {
            return nw::kernel::objects().get<nw::Item>(handle);
        }
    }

    return nullptr;
}

int process_item_properties(nw::Creature* obj, const nw::Item* item, nw::EquipIndex index, bool remove)
{
    NW_PROFILE_SCOPE_N("nwn1::process_item_properties");
    if (!obj || !item) { return 0; }
    if (!bridge::ensure_nwn1_smalls_initialized()) { return 0; }

    auto& rt = nw::kernel::runtime();
    rt.init_object_propsets(item->handle());

    nw::Vector<nw::smalls::Value> args;
    push_object_arg(args, obj);
    push_object_arg(args, item);
    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(index)));
    args.push_back(nw::smalls::Value::make_bool(remove));

    constexpr uint64_t item_props_gas_limit = 10'000'000;
    auto result = rt.execute_script("core.item", "process_item_properties", args, item_props_gas_limit);
    if (result.ok()) { return result.value.data.ival; }
    LOG_F(ERROR, "[nwn1.bridge] core.item.process_item_properties failed: {}", result.error_message);
    return 0;
}

void refresh_combat_weapon_cache(nw::Creature* obj)
{
    if (!obj) { return; }
    nw::Vector<nw::smalls::Value> args;
    push_object_arg(args, obj);
    bridge::call_nwn1_module_value("nwn1.combat", "refresh_combat_weapon_cache", args);
}

} // namespace nwn1
