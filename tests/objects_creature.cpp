#include <gtest/gtest.h>

#include "nwn1_test_builders.hpp"

#include <nw/functions.hpp>
#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Equips.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/combat_scheduler.hpp>
#include <nw/rules/effects.hpp>
#include <nw/rules/feats.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

using namespace std::literals;

namespace {

nw::Vector<nw::EffectType> g_effect_batch_types;
bool g_effect_batch_is_apply = false;
constexpr uint32_t creature_plt_mask = (1u << nw::plt_layer_skin)
    | (1u << nw::plt_layer_hair)
    | (1u << nw::plt_layer_tattoo1)
    | (1u << nw::plt_layer_tattoo2);

void capture_effect_batch(nw::ObjectBase*, const nw::Vector<nw::Effect*>& effects, bool is_apply)
{
    g_effect_batch_types.clear();
    g_effect_batch_types.reserve(effects.size());
    for (const auto* effect : effects) {
        if (effect) {
            g_effect_batch_types.push_back(effect->handle().type);
        }
    }
    g_effect_batch_is_apply = is_apply;
}

bool test_has_effect_applied(const nw::ObjectBase* obj, nw::EffectType type, int subtype = -1)
{
    if (!obj || type == nw::EffectType::invalid()) { return false; }
    for (const auto& handle : obj->effects()) {
        if (handle.type == type && handle.subtype == subtype) {
            return true;
        }
    }
    return false;
}

struct CreatureModelLookup {
    nw::Resref model;
    nw::Resref race;
    std::string error;
    int32_t appearance = -1;
    int32_t model_type = -1;
    int32_t hand_item_reason = 0;
    float wing_tail_scale = 1.0f;
    float helmet_scale_m = 1.0f;
    float helmet_scale_f = 1.0f;
    float hand_item_scale = 1.0f;
    bool hand_item_visible = true;
    bool humanoid = false;
    bool resolved = false;

    bool valid() const noexcept { return resolved; }
    bool has_model() const noexcept { return !model.empty(); }
};

nw::smalls::Value script_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    const nw::smalls::StructDef* def = rt.get_struct_def(value.type_id);
    if (!def) {
        return {};
    }

    uint32_t index = def->field_index(field);
    if (index == UINT32_MAX) {
        return {};
    }

    const auto& fd = def->fields[index];
    return rt.read_value_field_at_offset(value, fd.offset, fd.type_id);
}

int32_t script_int_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value,
    const char* field, int32_t fallback = 0)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    nw::smalls::TypeID type_id = field_value.type_id;
    while (type_id != nw::smalls::invalid_type_id) {
        if (type_id == rt.int_type()) { return field_value.data.ival; }

        const nw::smalls::Type* type = rt.get_type(type_id);
        if (!type
            || (type->type_kind != nw::smalls::TK_newtype
                && type->type_kind != nw::smalls::TK_alias)
            || !type->type_params[0].is<nw::smalls::TypeID>()) {
            break;
        }
        type_id = type->type_params[0].as<nw::smalls::TypeID>();
    }
    return fallback;
}

float script_float_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value,
    const char* field, float fallback = 0.0f)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.float_type() ? field_value.data.fval : fallback;
}

bool script_bool_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value,
    const char* field, bool fallback = false)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.bool_type() ? field_value.data.bval : fallback;
}

std::string script_string_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    if (field_value.type_id != rt.string_type()) {
        return {};
    }
    return std::string{nw::smalls::ScriptString{field_value.data.hptr}.view(rt)};
}

nw::Resref script_resref_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    nw::smalls::TypeID resref_type = rt.type_id("core.types.ResRef", false);
    if (field_value.type_id != resref_type) {
        return {};
    }

    const auto* resref = static_cast<const nw::Resref*>(rt.get_value_data_ptr(field_value));
    return resref ? *resref : nw::Resref{};
}

nw::smalls::IArray* script_array_field(nw::smalls::Runtime& rt,
    const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    if (field_value.type_id == nw::smalls::invalid_type_id) { return nullptr; }

    nw::TypedHandle handle = nw::TypedHandle::from_ull(field_value.data.handle);
    return rt.object_pool().get_unmanaged_array(handle);
}

size_t script_array_size(nw::smalls::Runtime& rt,
    const nw::smalls::Value& value, const char* field)
{
    auto* array = script_array_field(rt, value, field);
    return array ? array->size() : 0;
}

nw::smalls::Value script_array_value(nw::smalls::Runtime& rt,
    const nw::smalls::Value& value, const char* field, size_t index)
{
    auto* array = script_array_field(rt, value, field);
    if (!array || index >= array->size()) { return {}; }

    nw::smalls::Value result;
    if (!array->get_value(index, result, rt)) { return {}; }
    return result;
}

nw::smalls::Value find_creature_propset(nw::smalls::Runtime& rt, const nw::Creature* creature, const char* qname)
{
    if (!creature) { return {}; }

    auto tid = rt.type_id(qname, false);
    if (tid == nw::smalls::invalid_type_id) { return {}; }
    return rt.find_propset_ref(tid, creature->handle());
}

bool set_script_int_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value,
    const char* field, int32_t field_value)
{
    const nw::smalls::StructDef* def = rt.get_struct_def(value.type_id);
    if (!def) { return false; }

    uint32_t index = def->field_index(field);
    if (index == UINT32_MAX) { return false; }

    const auto& fd = def->fields[index];
    return rt.write_value_field_at_offset(value, fd.offset, rt.int_type(),
        nw::smalls::Value::make_int(field_value));
}

bool seed_creature_appearance_propset(nw::Creature* creature,
    nw::Appearance appearance,
    int32_t wings = 0,
    int32_t tail = 0,
    int32_t torso = 0)
{
    if (!creature) { return false; }

    auto& rt = nw::kernel::runtime();
    rt.init_object_propsets(creature->handle());
    auto tid = rt.type_id("core.creature.CreatureAppearance", false);
    if (tid == nw::smalls::invalid_type_id) { return false; }

    auto ref = rt.get_or_create_propset_ref(tid, creature->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return false; }

    return set_script_int_field(rt, ref, "appearance", *appearance)
        && set_script_int_field(rt, ref, "wings", wings)
        && set_script_int_field(rt, ref, "tail", tail)
        && set_script_int_field(rt, ref, "color_hair", 11)
        && set_script_int_field(rt, ref, "color_skin", 12)
        && set_script_int_field(rt, ref, "color_tattoo1", 13)
        && set_script_int_field(rt, ref, "color_tattoo2", 14)
        && set_script_int_field(rt, ref, "body_part_torso", torso);
}

float imported_creature_cr(nw::Creature* creature)
{
    auto& rt = nw::kernel::runtime();
    auto stats = find_creature_propset(rt, creature, "core.creature.CreatureStats");
    if (stats.type_id == nw::smalls::invalid_type_id) {
        ADD_FAILURE() << "missing CreatureStats propset";
        return 0.0f;
    }
    return script_float_field(rt, stats, "cr");
}

bool recompute_creature_available_spell_slots(nw::Creature* creature)
{
    if (!creature) { return false; }

    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    auto value = nw::smalls::Value::make_object(creature->handle());
    value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(value);

    auto result = rt.execute_script("nwn1.creature", "recompute_all_available_spell_slots", args);
    if (!result.ok()) {
        ADD_FAILURE() << result.error_message;
        return false;
    }
    return true;
}

int32_t creature_base_attack_bonus_from_script(nw::Creature* creature)
{
    if (!creature) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    return nwn1::bridge::call_nwn1_module_int("nwn1.combat", "compute_base_attack_bonus", args).value_or(0);
}

float creature_challenge_rating_from_script(nw::Creature* creature)
{
    if (!creature) { return 0.0f; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    return nwn1::bridge::call_nwn1_module_float("nwn1.creature", "calculate_challenge_rating", args).value_or(0.0f);
}

int32_t creature_ability_modifier_from_script(const nw::Creature* creature, nw::Ability ability, bool base = false)
{
    if (!creature || ability == nw::Ability::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*ability));
    args.push_back(nw::smalls::Value::make_bool(base));
    return nwn1::bridge::call_nwn1_module_int("nwn1.creature", "get_ability_modifier", args).value_or(0);
}

int32_t creature_ability_score_from_script(const nw::Creature* creature, nw::Ability ability, bool base = false)
{
    if (!creature || ability == nw::Ability::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*ability));
    args.push_back(nw::smalls::Value::make_bool(base));
    return nwn1::bridge::call_nwn1_module_int("nwn1.creature", "get_ability_score", args).value_or(0);
}

int32_t object_hitpoints_from_script(const char* function, const nw::ObjectBase* object)
{
    if (!object) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(object->handle()));
    return nwn1::bridge::call_nwn1_module_int("nwn1.hitpoints", function, args).value_or(0);
}

nw::smalls::Value nullable_object_arg(const nw::ObjectBase* object)
{
    if (object) {
        return nwn1::bridge::make_object_arg(object->handle());
    }

    auto value = nw::smalls::Value::make_object(nw::ObjectHandle{});
    value.type_id = nw::kernel::runtime().object_type();
    return value;
}

int32_t creature_saving_throw_from_script(const nw::ObjectBase* object, nw::Save save,
    nw::SaveVersus save_vs = nw::SaveVersus::invalid(), const nw::ObjectBase* versus = nullptr,
    bool base = false)
{
    if (!object || save == nw::Save::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(object->handle()));
    args.push_back(nw::smalls::Value::make_int(*save));
    args.push_back(nw::smalls::Value::make_int(*save_vs));
    args.push_back(nullable_object_arg(versus));
    args.push_back(nw::smalls::Value::make_bool(base));
    return nwn1::bridge::call_nwn1_module_int("nwn1.saving_throws", "get_saving_throw", args).value_or(0);
}

int32_t creature_skill_rank_from_script(const nw::Creature* creature, nw::Skill skill,
    const nw::ObjectBase* versus = nullptr, bool base = false)
{
    if (!creature || skill == nw::Skill::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*skill));
    if (base) {
        return nwn1::bridge::call_nwn1_module_int("nwn1.creature", "get_skill_rank", args).value_or(0);
    }

    args.push_back(nullable_object_arg(versus));
    return nwn1::bridge::call_nwn1_module_int("nwn1.creature", "get_skill_rank_full", args).value_or(0);
}

bool knows_feat_from_script(const nw::Creature* creature, nw::Feat feat)
{
    if (!creature || feat == nw::Feat::invalid()) { return false; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*feat));
    return nwn1::bridge::call_nwn1_module_bool("core.creature", "has_feat", args).value_or(false);
}

std::pair<nw::Feat, int32_t> feat_successor_from_script(const nw::Creature* creature, nw::Feat feat)
{
    nw::Feat highest = nw::Feat::invalid();
    int32_t count = 0;

    if (!creature || feat == nw::Feat::invalid()) {
        return {highest, count};
    }

    nw::Vector<nw::smalls::Value> count_args;
    const int32_t feat_count = nwn1::bridge::call_nwn1_module_int("nwn1.feats", "count", count_args).value_or(0);
    if (feat_count <= 0) {
        return {highest, count};
    }

    nw::Vector<nw::smalls::Value> successor_args;
    successor_args.push_back(nw::smalls::Value::make_int(*feat));

    while (count < feat_count) {
        if (!knows_feat_from_script(creature, feat)) {
            break;
        }
        highest = feat;
        ++count;

        successor_args[0] = nw::smalls::Value::make_int(*feat);
        const int32_t successor = nwn1::bridge::call_nwn1_module_int(
            "nwn1.feats", "successor", successor_args)
                                      .value_or(*nw::Feat::invalid());
        if (successor < 0) {
            break;
        }
        feat = nw::Feat::make(successor);
    }

    return {highest, count};
}

nw::Feat highest_feat_in_range_from_script(const nw::Creature* creature, nw::Feat start, nw::Feat end)
{
    if (!creature) { return nw::Feat::invalid(); }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*start));
    args.push_back(nw::smalls::Value::make_int(*end));
    return nw::Feat::make(nwn1::bridge::call_nwn1_module_int(
        "core.creature", "highest_feat_in_range", args)
                              .value_or(*nw::Feat::invalid()));
}

int32_t count_feats_in_range_from_script(const nw::Creature* creature, nw::Feat start, nw::Feat end)
{
    if (!creature) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*start));
    args.push_back(nw::smalls::Value::make_int(*end));
    return nwn1::bridge::call_nwn1_module_int("core.creature", "count_feats_in_range", args).value_or(0);
}

void push_creature_class_args(nw::Vector<nw::smalls::Value>& args,
    const nw::Creature* creature, nw::Class class_)
{
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*class_));
}

void push_creature_class_spell_args(nw::Vector<nw::smalls::Value>& args,
    const nw::Creature* creature, nw::Class class_, nw::Spell spell)
{
    push_creature_class_args(args, creature, class_);
    args.push_back(nw::smalls::Value::make_int(*spell));
}

int32_t creature_class_int_from_script(const char* function, const nw::Creature* creature, nw::Class class_)
{
    if (!creature || class_ == nw::Class::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    push_creature_class_args(args, creature, class_);
    return nwn1::bridge::call_nwn1_module_int("nwn1.creature", function, args).value_or(0);
}

int32_t creature_class_spell_level_int_from_script(const char* function, const nw::Creature* creature,
    nw::Class class_, int32_t spell_level)
{
    if (!creature || class_ == nw::Class::invalid() || spell_level < 0) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    push_creature_class_args(args, creature, class_);
    args.push_back(nw::smalls::Value::make_int(spell_level));
    return nwn1::bridge::call_nwn1_module_int("nwn1.creature", function, args).value_or(0);
}

bool creature_class_spell_bool_from_script(const char* function, const nw::Creature* creature,
    nw::Class class_, nw::Spell spell)
{
    if (!creature || class_ == nw::Class::invalid() || spell == nw::Spell::invalid()) { return false; }

    nw::Vector<nw::smalls::Value> args;
    push_creature_class_spell_args(args, creature, class_, spell);
    return nwn1::bridge::call_nwn1_module_bool("nwn1.creature", function, args).value_or(false);
}

bool creature_add_memorized_spell_from_script(const nw::Creature* creature,
    nw::Class class_, nw::Spell spell, nw::MetaMagicCode meta = nw::metamagic_none)
{
    if (!creature || class_ == nw::Class::invalid() || spell == nw::Spell::invalid()) { return false; }

    nw::Vector<nw::smalls::Value> args;
    push_creature_class_spell_args(args, creature, class_, spell);
    args.push_back(nw::smalls::Value::make_int(*meta));
    return nwn1::bridge::call_nwn1_module_bool("nwn1.creature", "add_memorized_spell", args).value_or(false);
}

void creature_class_spell_void_from_script(const char* function, const nw::Creature* creature,
    nw::Class class_, nw::Spell spell)
{
    if (!creature || class_ == nw::Class::invalid() || spell == nw::Spell::invalid()) { return; }

    nw::Vector<nw::smalls::Value> args;
    push_creature_class_spell_args(args, creature, class_, spell);
    nwn1::bridge::call_nwn1_module_void("nwn1.creature", function, args);
}

int32_t creature_available_spell_uses_from_script(const nw::Creature* creature, nw::Class class_,
    nw::Spell spell, int32_t min_spell_level, nw::MetaMagicCode meta)
{
    if (!creature || class_ == nw::Class::invalid() || spell == nw::Spell::invalid() || min_spell_level < 0) {
        return 0;
    }

    nw::Vector<nw::smalls::Value> args;
    push_creature_class_spell_args(args, creature, class_, spell);
    args.push_back(nw::smalls::Value::make_int(min_spell_level));
    args.push_back(nw::smalls::Value::make_int(*meta));
    return nwn1::bridge::call_nwn1_module_int("nwn1.creature", "get_available_spell_uses", args).value_or(0);
}

int32_t creature_get_caster_level_from_script(const nw::Creature* creature, nw::Class class_)
{
    return creature_class_int_from_script("get_caster_level", creature, class_);
}

int32_t creature_get_spell_dc_from_script(const nw::Creature* creature, nw::Class class_, nw::Spell spell)
{
    if (!creature || class_ == nw::Class::invalid() || spell == nw::Spell::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    push_creature_class_spell_args(args, creature, class_, spell);
    return nwn1::bridge::call_nwn1_module_int("nwn1.creature", "get_spell_dc", args).value_or(0);
}

int32_t creature_compute_total_spell_slots_from_script(const nw::Creature* creature,
    nw::Class class_, int32_t spell_level)
{
    return creature_class_spell_level_int_from_script("compute_total_spell_slots", creature, class_, spell_level);
}

int32_t creature_get_available_spell_slots_from_script(const nw::Creature* creature,
    nw::Class class_, int32_t spell_level)
{
    return creature_class_spell_level_int_from_script("get_available_spell_slots", creature, class_, spell_level);
}

bool creature_add_known_spell_from_script(const nw::Creature* creature, nw::Class class_, nw::Spell spell)
{
    return creature_class_spell_bool_from_script("add_known_spell", creature, class_, spell);
}

bool creature_get_knows_spell_from_script(const nw::Creature* creature, nw::Class class_, nw::Spell spell)
{
    return creature_class_spell_bool_from_script("get_knows_spell", creature, class_, spell);
}

void creature_remove_known_spell_from_script(const nw::Creature* creature, nw::Class class_, nw::Spell spell)
{
    creature_class_spell_void_from_script("remove_known_spell", creature, class_, spell);
}

int32_t creature_alignment_flags_from_script(nw::Creature* creature)
{
    auto& rt = nw::kernel::runtime();
    auto stats = find_creature_propset(rt, creature, "core.creature.CreatureStats");
    if (stats.type_id == nw::smalls::invalid_type_id) {
        ADD_FAILURE() << "missing CreatureStats propset";
        return 0;
    }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(script_int_field(rt, stats, "good_evil")));
    args.push_back(nw::smalls::Value::make_int(script_int_field(rt, stats, "lawful_chaotic")));

    auto result = rt.execute_script("core.combat", "alignment_flags_from_values", args);
    if (!result.ok()) {
        ADD_FAILURE() << result.error_message;
        return 0;
    }
    if (result.value.type_id != rt.int_type()) {
        ADD_FAILURE() << "alignment_flags_from_values returned non-int";
        return 0;
    }
    return result.value.data.ival;
}

bool add_creature_propset_feat(nw::Creature* creature, nw::Feat feat)
{
    if (!creature) { return false; }

    auto& rt = nw::kernel::runtime();
    nw::smalls::Value stats = find_creature_propset(rt, creature, "core.creature.CreatureStats");
    if (stats.type_id == nw::smalls::invalid_type_id) { return false; }

    const nw::smalls::StructDef* def = rt.get_struct_def(stats.type_id);
    if (!def) { return false; }

    uint32_t field_index = def->field_index("feats");
    if (field_index == UINT32_MAX) { return false; }

    const nw::smalls::FieldDef& field = def->fields[field_index];
    if (!field.is_unmanaged_array) { return false; }

    nw::smalls::Value arr_value = rt.read_value_field_at_offset(stats, field.offset, field.type_id);
    nw::TypedHandle handle = nw::TypedHandle::from_ull(arr_value.data.handle);
    nw::smalls::IArray* arr = rt.object_pool().get_unmanaged_array(handle);
    if (!arr) { return false; }

    nw::Vector<int32_t> values;
    values.reserve(arr->size() + 1);
    for (size_t i = 0; i < arr->size(); ++i) {
        nw::smalls::Value value;
        if (arr->get_value(i, value, rt) && value.type_id == rt.int_type()) {
            values.push_back(value.data.ival);
        }
    }

    int32_t feat_id = *feat;
    auto it = std::lower_bound(values.begin(), values.end(), feat_id);
    if (it != values.end() && *it == feat_id) { return false; }
    values.insert(it, feat_id);

    arr->clear();
    arr->reserve(values.size());
    for (int32_t value : values) {
        arr->append_value(nw::smalls::Value::make_int(value), rt);
    }
    return true;
}

int32_t script_fixed_int_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value,
    const char* field, size_t index, int32_t fallback = 0)
{
    const nw::smalls::StructDef* def = rt.get_struct_def(value.type_id);
    if (!def) { return fallback; }

    uint32_t field_index = def->field_index(field);
    if (field_index == UINT32_MAX) { return fallback; }

    auto fixed_value = rt.read_value_field_at_offset(
        value, def->fields[field_index].offset + uint32_t(index) * 4u, rt.int_type());
    return fixed_value.type_id == rt.int_type() ? fixed_value.data.ival : fallback;
}

int32_t creature_propset_level_by_class(nw::smalls::Runtime& rt, const nw::smalls::Value& levels,
    nw::Class class_id)
{
    if (class_id == nw::Class::invalid()) { return 0; }

    for (size_t i = 0; i < 8; ++i) {
        const int32_t slot_class = script_fixed_int_field(rt, levels, "classes", i, -1);
        if (slot_class == *class_id) {
            return script_fixed_int_field(rt, levels, "class_levels", i);
        }
        if (slot_class == -1) { break; }
    }
    return 0;
}

CreatureModelLookup resolve_creature_model_from_appearance(nw::Appearance appearance)
{
    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(*appearance));

    auto executed = rt.execute_script("nwn1.creature", "resolve_creature_model", args);
    CreatureModelLookup result;
    if (!executed.ok()) {
        result.error = executed.error_message;
        return result;
    }

    result.model = script_resref_field(rt, executed.value, "model");
    result.race = script_resref_field(rt, executed.value, "race");
    result.error = script_string_field(rt, executed.value, "error");
    result.appearance = script_int_field(rt, executed.value, "appearance", -1);
    result.model_type = script_int_field(rt, executed.value, "model_type", -1);
    result.hand_item_reason = script_int_field(rt, executed.value, "hand_item_reason");
    result.wing_tail_scale = script_float_field(rt, executed.value, "wing_tail_scale", 1.0f);
    result.helmet_scale_m = script_float_field(rt, executed.value, "helmet_scale_m", 1.0f);
    result.helmet_scale_f = script_float_field(rt, executed.value, "helmet_scale_f", 1.0f);
    result.hand_item_scale = script_float_field(rt, executed.value, "hand_item_scale", 1.0f);
    result.hand_item_visible = script_bool_field(rt, executed.value, "hand_item_visible", true);
    result.humanoid = script_bool_field(rt, executed.value, "humanoid");
    result.resolved = script_bool_field(rt, executed.value, "valid");
    return result;
}

} // namespace

TEST(Creature, GffDeserialize)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto name = nw::Creature::get_name_from_file(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_EQ(name, "Chicken");

    auto obj1 = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(obj1);
    EXPECT_TRUE(nwk::objects().valid(obj1->handle()));

    EXPECT_EQ(obj1->resref, "nw_chicken");
    auto& rt = nw::kernel::runtime();
    auto descriptor1 = find_creature_propset(rt, obj1, "core.creature.CreatureDescriptor");
    ASSERT_NE(descriptor1.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_resref_field(rt, descriptor1, "on_attacked"), nw::Resref{"nw_c2_default5"});
    auto appearance1 = find_creature_propset(rt, obj1, "core.creature.CreatureAppearance");
    ASSERT_NE(appearance1.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_int_field(rt, appearance1, "appearance"), 31);
    auto stats1 = find_creature_propset(rt, obj1, "core.creature.CreatureStats");
    ASSERT_NE(stats1.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_fixed_int_field(rt, stats1, "abilities", *nwn1::ability_dexterity), 7);
    EXPECT_EQ(script_int_field(rt, stats1, "gender"), 1);
    EXPECT_EQ(creature_alignment_flags_from_script(obj1),
        static_cast<int32_t>(nw::align_true_neutral));

    auto obj2 = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(obj2);

    EXPECT_EQ(obj2->resref, "pl_agent_001");
    auto descriptor2 = find_creature_propset(rt, obj2, "core.creature.CreatureDescriptor");
    ASSERT_NE(descriptor2.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_resref_field(rt, descriptor2, "on_attacked"), nw::Resref{"mon_ai_5attacked"});
    auto appearance2 = find_creature_propset(rt, obj2, "core.creature.CreatureAppearance");
    ASSERT_NE(appearance2.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_int_field(rt, appearance2, "appearance"), 6);
    EXPECT_EQ(script_int_field(rt, appearance2, "body_part_shin_left"), 1);
    EXPECT_EQ(script_int_field(rt, descriptor2, "soundset"), 171);
    EXPECT_TRUE(nw::get_equipped_item(obj2, nw::EquipIndex::chest));
    auto stats2 = find_creature_propset(rt, obj2, "core.creature.CreatureStats");
    ASSERT_NE(stats2.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_fixed_int_field(rt, stats2, "abilities", *nwn1::ability_dexterity), 13);
    EXPECT_EQ(script_int_field(rt, stats2, "ac_natural_bonus"), 0);
    ASSERT_EQ(script_array_size(rt, stats2, "special_abilities"), 1);
    auto special_ability = script_array_value(rt, stats2, "special_abilities", 0);
    EXPECT_EQ(script_int_field(rt, special_ability, "spell"), 120);
    EXPECT_EQ(creature_alignment_flags_from_script(obj2),
        static_cast<int32_t>(nw::align_neutral_good));
}

TEST(Creature, GffDeserializeImportsPropsets)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    nw::Gff gff("test_data/user/development/pl_agent_001.utc");
    ASSERT_TRUE(gff.valid());
    auto gff_top = gff.toplevel();

    nw::Resref script_attacked;
    uint16_t soundset = 0;
    uint8_t dexterity = 0;
    int16_t hp_current = 0;
    uint16_t faction_id = 0;
    int32_t walkrate = 0;
    ASSERT_TRUE(gff_top.get_to("ScriptAttacked", script_attacked));
    ASSERT_TRUE(gff_top.get_to("SoundSetFile", soundset));
    ASSERT_TRUE(gff_top.get_to("Dex", dexterity));
    ASSERT_TRUE(gff_top.get_to("CurrentHitPoints", hp_current));
    ASSERT_TRUE(gff_top.get_to("FactionID", faction_id));
    ASSERT_TRUE(gff_top.get_to("WalkRate", walkrate));

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    ASSERT_TRUE(nw::deserialize(creature, gff_top, nw::SerializationProfile::blueprint));

    auto& rt = nw::kernel::runtime();
    auto descriptor = find_creature_propset(rt, creature, "core.creature.CreatureDescriptor");
    ASSERT_NE(descriptor.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_resref_field(rt, descriptor, "on_attacked"), script_attacked);
    EXPECT_EQ(script_int_field(rt, descriptor, "soundset"), soundset);

    auto stats = find_creature_propset(rt, creature, "core.creature.CreatureStats");
    ASSERT_NE(stats.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_fixed_int_field(rt, stats, "abilities", *nwn1::ability_dexterity), dexterity);

    auto health = find_creature_propset(rt, creature, "core.creature.CreatureHealth");
    ASSERT_NE(health.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_int_field(rt, health, "hp_current"), hp_current);
    EXPECT_EQ(script_int_field(rt, health, "faction_id"), faction_id);

    auto levels = find_creature_propset(rt, creature, "core.creature.CreatureLevels");
    ASSERT_NE(levels.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_int_field(rt, levels, "walkrate"), walkrate);

    nw::kernel::objects().destroy(creature->handle());
}

TEST(Creature, InstantiatePreservesImportedPropsetsWithoutLegacyCombatMirrors)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    nw::Gff gff("test_data/user/development/pl_agent_001.utc");
    ASSERT_TRUE(gff.valid());
    auto gff_top = gff.toplevel();

    uint8_t dexterity = 0;
    ASSERT_TRUE(gff_top.get_to("Dex", dexterity));

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    ASSERT_TRUE(nw::deserialize(creature, gff_top, nw::SerializationProfile::blueprint));

    auto& rt = nw::kernel::runtime();
    auto stats = find_creature_propset(rt, creature, "core.creature.CreatureStats");
    ASSERT_NE(stats.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_fixed_int_field(rt, stats, "abilities", *nwn1::ability_dexterity), dexterity);

    ASSERT_TRUE(creature->instantiate());

    stats = find_creature_propset(rt, creature, "core.creature.CreatureStats");
    ASSERT_NE(stats.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_fixed_int_field(rt, stats, "abilities", *nwn1::ability_dexterity), dexterity);

    auto combat = find_creature_propset(rt, creature, "core.creature.CreatureCombat");
    ASSERT_NE(combat.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_int_field(rt, combat, "attack_current"), 0);
    EXPECT_EQ(script_int_field(rt, combat, "attacks_onhand"), 0);
    EXPECT_EQ(script_int_field(rt, combat, "attacks_offhand"), 0);
    EXPECT_EQ(script_int_field(rt, combat, "attacks_extra"), 0);
    EXPECT_EQ(script_int_field(rt, combat, "combat_mode"), 0);
    EXPECT_EQ(script_int_field(rt, combat, "ac_armor_base"), 0);
    EXPECT_EQ(script_int_field(rt, combat, "ac_shield_base"), 0);
    EXPECT_EQ(script_int_field(rt, combat, "size_ab_modifier"), 0);
    EXPECT_EQ(script_int_field(rt, combat, "size_ac_modifier"), 0);
    EXPECT_EQ(script_float_field(rt, combat, "target_distance_sq"), 0.0f);
    EXPECT_EQ(script_int_field(rt, combat, "target_state"), 0);
    EXPECT_EQ(script_int_field(rt, combat, "hasted"), 0);
    EXPECT_EQ(script_int_field(rt, combat, "size"), 0);

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_object(creature->handle()));
    auto size_result = rt.execute_script("nwn1.creature", "creature_size", args);
    ASSERT_TRUE(size_result.ok()) << size_result.error_message;
    ASSERT_TRUE(size_result.value.type_id == rt.int_type());
    auto appearance_propset = find_creature_propset(rt, creature, "core.creature.CreatureAppearance");
    ASSERT_NE(appearance_propset.type_id, nw::smalls::invalid_type_id);
    const auto* appearance = nw::kernel::rules().appearances.get(
        nw::Appearance::make(script_int_field(rt, appearance_propset, "appearance")));
    ASSERT_NE(appearance, nullptr);
    EXPECT_EQ(size_result.value.data.ival, appearance->size);

    auto* creature_sizes = nw::kernel::twodas().get("creaturesize");
    ASSERT_NE(creature_sizes, nullptr);
    int32_t expected_size_modifier = 0;
    ASSERT_TRUE(creature_sizes->get_to(static_cast<size_t>(size_result.value.data.ival),
        "ACATTACKMOD", expected_size_modifier));

    auto attack_modifier = rt.execute_script("nwn1.creature_size", "attack_modifier", args);
    ASSERT_TRUE(attack_modifier.ok()) << attack_modifier.error_message;
    ASSERT_TRUE(attack_modifier.value.type_id == rt.int_type());
    EXPECT_EQ(attack_modifier.value.data.ival, expected_size_modifier);

    auto armor_modifier = rt.execute_script("nwn1.creature_size", "armor_modifier", args);
    ASSERT_TRUE(armor_modifier.ok()) << armor_modifier.error_message;
    ASSERT_TRUE(armor_modifier.value.type_id == rt.int_type());
    EXPECT_EQ(armor_modifier.value.data.ival, expected_size_modifier);

    auto health = find_creature_propset(rt, creature, "core.creature.CreatureHealth");
    ASSERT_NE(health.type_id, nw::smalls::invalid_type_id);
    const auto* vitals = nw::kernel::objects().components().find_vitals(creature->handle());
    ASSERT_NE(vitals, nullptr);
    EXPECT_EQ(vitals->hp_current, script_int_field(rt, health, "hp_current"));
    EXPECT_EQ(vitals->hp_max, script_int_field(rt, health, "hp_max"));

    nw::kernel::objects().destroy(creature->handle());
}

TEST(Creature, CreatureStatsFeatsImportAndInsert)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    auto& rt = nw::kernel::runtime();
    auto stats = find_creature_propset(rt, ent, "core.creature.CreatureStats");
    ASSERT_NE(stats.type_id, nw::smalls::invalid_type_id);
    ASSERT_EQ(script_array_size(rt, stats, "feats"), 37);

    auto rando_feat_value = script_array_value(rt, stats, "feats", 20);
    ASSERT_EQ(rando_feat_value.type_id, rt.int_type());
    auto rando_feat = nw::Feat::make(static_cast<uint32_t>(rando_feat_value.data.ival));
    EXPECT_TRUE(knows_feat_from_script(ent, rando_feat));
    EXPECT_FALSE(add_creature_propset_feat(ent, rando_feat));

    EXPECT_FALSE(knows_feat_from_script(ent, nwn1::feat_epic_toughness_1));
    EXPECT_TRUE(add_creature_propset_feat(ent, nwn1::feat_epic_toughness_1));
    EXPECT_TRUE(knows_feat_from_script(ent, nwn1::feat_epic_toughness_1));
}

TEST(Creature, ResolveModelUsesSmallsAppearanceConfig)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto bodak = resolve_creature_model_from_appearance(nwn1::appearance_type_bodak);
    ASSERT_TRUE(bodak.valid()) << bodak.error;
    EXPECT_FALSE(bodak.humanoid);
    ASSERT_TRUE(bodak.has_model()) << bodak.error;
    EXPECT_EQ(bodak.model, nw::Resref{"c_bodak"});
    EXPECT_EQ(bodak.race, nw::Resref{"c_bodak"});
    EXPECT_TRUE(bodak.hand_item_visible);
    EXPECT_FLOAT_EQ(bodak.hand_item_scale, 1.0f);
    EXPECT_FLOAT_EQ(bodak.wing_tail_scale, 1.0f);

    auto human = resolve_creature_model_from_appearance(nwn1::appearance_type_human);
    ASSERT_TRUE(human.valid()) << human.error;
    EXPECT_TRUE(human.humanoid);
    EXPECT_FALSE(human.has_model());
    EXPECT_EQ(human.race, nw::Resref{"h"});
    EXPECT_TRUE(human.hand_item_visible);
    EXPECT_FLOAT_EQ(human.hand_item_scale, 1.0f);
}

TEST(Creature, UpdateVisualUsesNonHumanoidSmallsModelRow)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto* creature = nwk::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    ASSERT_TRUE(seed_creature_appearance_propset(creature, nwn1::appearance_type_bodak));

    auto& rt = nwk::runtime();
    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);
    args.push_back(nw::smalls::Value::make_int(*nwn1::appearance_type_bodak));

    auto updated = rt.execute_script("nwn1.creature", "update_visual_by_appearance", args);
    ASSERT_TRUE(updated.ok()) << updated.error_message;
    ASSERT_EQ(updated.value.type_id, rt.bool_type());
    ASSERT_TRUE(updated.value.data.bval);

    const auto* visual = nwk::objects().components().find_visual(creature->handle());
    ASSERT_NE(visual, nullptr);
    EXPECT_EQ(visual->appearance, *nwn1::appearance_type_bodak);
    EXPECT_EQ(visual->base_plt_color_mask, creature_plt_mask);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_hair], 11);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_skin], 12);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_tattoo1], 13);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_tattoo2], 14);
    ASSERT_EQ(visual->models.size(), 1);
    EXPECT_EQ(visual->models[0].model, nw::Resref{"c_bodak"});
    EXPECT_EQ(visual->models[0].kind, nw::ObjectVisualModel{}.kind);
    EXPECT_EQ(visual->models[0].slot, nw::ObjectVisualModel{}.slot);

    nwk::objects().destroy(creature->handle());
}

TEST(Creature, UpdateVisualUsesSmallsAttachmentRows)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto* wingmodel = nw::kernel::twodas().get("wingmodel");
    std::string wing_model;
    if (!wingmodel || !wingmodel->get_to(1u, "MODEL", wing_model) || wing_model.empty()) {
        GTEST_SKIP() << "wingmodel row 1 unavailable";
    }

    auto* creature = nwk::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    ASSERT_TRUE(seed_creature_appearance_propset(creature, nwn1::appearance_type_bodak, 1));

    auto& rt = nwk::runtime();
    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);
    args.push_back(nw::smalls::Value::make_int(*nwn1::appearance_type_bodak));

    auto updated = rt.execute_script("nwn1.creature", "update_visual_by_appearance", args);
    ASSERT_TRUE(updated.ok()) << updated.error_message;
    ASSERT_EQ(updated.value.type_id, rt.bool_type());
    ASSERT_TRUE(updated.value.data.bval);

    const auto* visual = nwk::objects().components().find_visual(creature->handle());
    ASSERT_NE(visual, nullptr);

    const nw::ObjectVisualModel* wing = nullptr;
    for (const auto& row : visual->models) {
        if (row.kind == nw::ObjectVisualModelKind::creature_attachment
            && row.part == nw::ObjectVisualCreatureAttachmentPart::wing) {
            wing = &row;
            break;
        }
    }

    ASSERT_NE(wing, nullptr);
    EXPECT_EQ(wing->model, nw::Resref{wing_model});
    EXPECT_EQ(wing->attach_to, nw::Resref{"wings"});
    EXPECT_EQ(wing->attach_from, nw::Resref{"wings"});
    EXPECT_EQ(wing->slot, -1);
    EXPECT_EQ(wing->source_part, 1);
    EXPECT_EQ(wing->model_part, 1);

    nwk::objects().destroy(creature->handle());
}

TEST(Creature, UpdateVisualUsesHumanoidSmallsBodyRows)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);
    ASSERT_TRUE(nwk::resman().contains({"pmh0_chest001"sv, nw::ResourceType::mdl}));

    auto* creature = nwk::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    ASSERT_TRUE(seed_creature_appearance_propset(creature, nwn1::appearance_type_human, 0, 0, 1));

    auto& rt = nwk::runtime();
    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);
    args.push_back(nw::smalls::Value::make_int(*nwn1::appearance_type_human));

    auto updated = rt.execute_script("nwn1.creature", "update_visual_by_appearance", args);
    ASSERT_TRUE(updated.ok()) << updated.error_message;
    ASSERT_EQ(updated.value.type_id, rt.bool_type());
    ASSERT_TRUE(updated.value.data.bval);

    const auto* visual = nwk::objects().components().find_visual(creature->handle());
    ASSERT_NE(visual, nullptr);
    EXPECT_EQ(visual->appearance, *nwn1::appearance_type_human);
    EXPECT_EQ(visual->base_plt_color_mask, creature_plt_mask);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_hair], 11);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_skin], 12);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_tattoo1], 13);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_tattoo2], 14);

    const nw::ObjectVisualModel* torso = nullptr;
    for (const auto& row : visual->models) {
        if (row.model == nw::Resref{"pmh0_chest001"}) {
            torso = &row;
            break;
        }
    }
    ASSERT_NE(torso, nullptr);
    EXPECT_EQ(torso->kind, nw::ObjectVisualModelKind::creature_model_part);
    EXPECT_EQ(torso->slot, -1);
    EXPECT_EQ(torso->part, -1);
    EXPECT_EQ(torso->source_part, 1);
    EXPECT_EQ(torso->model_part, 1);
    EXPECT_EQ(torso->attach_to, nw::Resref{"torso_g"});

    nwk::objects().destroy(creature->handle());
}

TEST(Creature, InstantiatePopulatesHumanoidSmallsBodyRows)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto* creature = nwk::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    ASSERT_TRUE(seed_creature_appearance_propset(creature, nwn1::appearance_type_human, 0, 0, 1));

    ASSERT_TRUE(creature->instantiate());

    const auto* visual = nwk::objects().components().find_visual(creature->handle());
    ASSERT_NE(visual, nullptr);
    EXPECT_EQ(visual->appearance, *nwn1::appearance_type_human);

    bool found_torso = false;
    for (const auto& row : visual->models) {
        if (row.kind == nw::ObjectVisualModelKind::creature_model_part
            && row.model == nw::Resref{"pmh0_chest001"}) {
            found_torso = true;
            break;
        }
    }
    EXPECT_TRUE(found_torso);

    nwk::objects().destroy(creature->handle());
}

TEST(Creature, FeatSearch)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    EXPECT_TRUE(knows_feat_from_script(ent, nwn1::feat_epic_toughness_2));
    EXPECT_FALSE(knows_feat_from_script(ent, nwn1::feat_epic_toughness_1));

    auto [feat, nth] = feat_successor_from_script(ent, nwn1::feat_epic_toughness_2);
    EXPECT_TRUE(nth);
    EXPECT_EQ(feat, nwn1::feat_epic_toughness_4);

    auto [feat2, nth2] = feat_successor_from_script(ent, nwn1::feat_epic_great_wisdom_1);
    EXPECT_EQ(nth2, 0);

    auto feat3 = highest_feat_in_range_from_script(ent, nwn1::feat_epic_toughness_1, nwn1::feat_epic_toughness_10);
    EXPECT_EQ(feat3, nwn1::feat_epic_toughness_4);

    auto n = count_feats_in_range_from_script(ent, nwn1::feat_epic_toughness_1, nwn1::feat_epic_toughness_10);
    EXPECT_EQ(n, 3); // char doesn't have et1.
}

TEST(Creature, Ability)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(obj);
    EXPECT_TRUE(obj->instantiate());

    EXPECT_EQ(creature_ability_score_from_script(obj, nwn1::ability_strength), 40);
    EXPECT_EQ(creature_ability_modifier_from_script(obj, nwn1::ability_strength), 15);

    EXPECT_EQ(creature_ability_score_from_script(obj, nwn1::ability_dexterity), 17);
    EXPECT_EQ(creature_ability_modifier_from_script(obj, nwn1::ability_dexterity), 3);

    EXPECT_EQ(creature_ability_score_from_script(obj, nwn1::ability_strength, false), 40);
    EXPECT_TRUE(add_creature_propset_feat(obj, nwn1::feat_epic_great_strength_1));
    EXPECT_EQ(creature_ability_score_from_script(obj, nwn1::ability_strength, false), 41);
    EXPECT_TRUE(add_creature_propset_feat(obj, nwn1::feat_epic_great_strength_2));
    EXPECT_EQ(creature_ability_score_from_script(obj, nwn1::ability_strength, false), 42);

    auto eff = nwn1::effect_ability_modifier(nwn1::ability_strength, 5);
    EXPECT_TRUE(nwk::effects().apply_to(obj, eff));
    EXPECT_TRUE(obj->effects().size() > 0);
    EXPECT_EQ(creature_ability_score_from_script(obj, nwn1::ability_strength, false), 47);

    auto eff2 = nwn1::effect_ability_modifier(nwn1::ability_strength, -3);
    EXPECT_TRUE(nwk::effects().apply_to(obj, eff2));
    EXPECT_EQ(creature_ability_score_from_script(obj, nwn1::ability_strength, false), 44);

    // Belt of Cloud Giant Strength
    auto item = nwk::objects().load<nw::Item>("x2_it_mbelt001"sv);
    EXPECT_TRUE(item);
    EXPECT_TRUE(nwn1::equip_item(obj, item, nw::EquipIndex::belt));
    EXPECT_TRUE(obj->effects().size() > 1);
    EXPECT_EQ(creature_ability_score_from_script(obj, nwn1::ability_strength, false), 52);
    EXPECT_TRUE(nwn1::unequip_item(obj, nw::EquipIndex::belt));
    EXPECT_EQ(creature_ability_score_from_script(obj, nwn1::ability_strength, false), 44);

    // Class stat gain
    auto obj2 = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/sorcrdd.utc");
    EXPECT_TRUE(obj2);
    EXPECT_TRUE(obj2->instantiate());

    auto& rt = nw::kernel::runtime();
    auto obj2_stats = find_creature_propset(rt, obj2, "core.creature.CreatureStats");
    ASSERT_NE(obj2_stats.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_fixed_int_field(rt, obj2_stats, "abilities", *nwn1::ability_strength), 15);
    EXPECT_EQ(creature_ability_score_from_script(obj2, nwn1::ability_strength), 23);

    EXPECT_EQ(script_fixed_int_field(rt, obj2_stats, "abilities", *nwn1::ability_constitution), 14);
    EXPECT_EQ(creature_ability_score_from_script(obj2, nwn1::ability_constitution), 14); // This is an elf!
}

TEST(Creature, Skills)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(obj);

    EXPECT_EQ(creature_skill_rank_from_script(obj, nwn1::skill_discipline, nullptr, true), 40);
    EXPECT_TRUE(add_creature_propset_feat(obj, nwn1::feat_skill_focus_discipline));
    EXPECT_EQ(creature_skill_rank_from_script(obj, nwn1::skill_discipline), 61);
    EXPECT_TRUE(add_creature_propset_feat(obj, nwn1::feat_epic_skill_focus_discipline));
    EXPECT_EQ(creature_skill_rank_from_script(obj, nwn1::skill_discipline), 71);

    auto eff = nwn1::effect_skill_modifier(nwn1::skill_discipline, 5);
    EXPECT_TRUE(nwk::effects().apply_to(obj, eff));
    EXPECT_EQ(creature_skill_rank_from_script(obj, nwn1::skill_discipline), 76);

    auto eff2 = nwn1::effect_ability_modifier(nwn1::ability_strength, 5);
    EXPECT_TRUE(nwk::effects().apply_to(obj, eff2));
    EXPECT_EQ(creature_skill_rank_from_script(obj, nwn1::skill_discipline), 78);
}

TEST(Creature, Attack)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(obj);
    EXPECT_TRUE(test_has_effect_applied(obj, nwn1::effect_type_damage_increase, *nwn1::damage_type_base_weapon));

    auto obj2 = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(obj2);

    for (size_t i = 0; i < 100; ++i) {
        nw::AttackData data1;
        EXPECT_TRUE(nw::combat::resolve_attack(obj, obj2, &data1));
        EXPECT_EQ(data1.type, nwn1::attack_type_unarmed);
        if (nw::is_attack_type_hit(data1.result)) {
            EXPECT_GT(data1.damage_total, 0);
        } else {
            EXPECT_EQ(data1.damage_total, 0);
        }
    }

    auto obj3 = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    EXPECT_TRUE(obj3);

    for (size_t i = 0; i < 100; ++i) {
        nw::AttackData data1;
        EXPECT_TRUE(nw::combat::resolve_attack(obj3, obj, &data1));
        EXPECT_EQ(data1.type, nwn1::attack_type_onhand);
        if (nw::is_attack_type_hit(data1.result)) {
            EXPECT_GT(data1.damage_total, 0);
        } else {
            EXPECT_EQ(data1.damage_total, 0);
        }
    }

    auto obj4 = nwk::objects().load_file<nw::Creature>("test_data/user/development/rangerdexranged.utc");
    EXPECT_TRUE(obj4);

    auto vs4 = nwk::objects().load<nw::Creature>("nw_drggreen003"sv);
    EXPECT_TRUE(vs4);
    for (size_t i = 0; i < 100; ++i) {
        nw::AttackData data1;
        EXPECT_TRUE(nw::combat::resolve_attack(obj4, vs4, &data1));
        EXPECT_EQ(data1.type, nwn1::attack_type_onhand);
        if (nw::is_attack_type_hit(data1.result)) {
            EXPECT_GT(data1.damage_total, 0);
        } else {
            EXPECT_EQ(data1.damage_total, 0);
        }
    }
}

TEST(Creature, AttackSchedulingUsesEventTicks)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto attacker = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    auto target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_TRUE(attacker);
    ASSERT_TRUE(target);

    auto& events = nwk::events();
    events.process_until(std::numeric_limits<uint64_t>::max());
    events.set_current_tick(0);

    auto delay = nw::combat::resolve_attack_cooldown_ticks(attacker, 60);
    EXPECT_GE(delay, 1u);

    EXPECT_TRUE(nw::combat::schedule_attack(attacker, target, 2));
    EXPECT_EQ(events.process(), 0);

    events.advance(1);
    EXPECT_EQ(events.process(), 0);

    events.advance(1);
    EXPECT_EQ(events.process(), 1);
    EXPECT_EQ(events.process(), 0);
}

TEST(Creature, CombatSchedulerCanResolveAttackWithoutAttackData)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto attacker = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    auto target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_TRUE(attacker);
    ASSERT_TRUE(target);

    EXPECT_TRUE(nw::combat::resolve_attack(attacker, target));
    EXPECT_TRUE(nw::combat::resolve_attack(attacker, target));
}

TEST(Creature, AutoAttackCanRetargetAndStop)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto attacker = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    auto target1 = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    auto target2 = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ASSERT_TRUE(attacker);
    ASSERT_TRUE(target1);
    ASSERT_TRUE(target2);

    auto& events = nwk::events();
    events.process_until(std::numeric_limits<uint64_t>::max());
    events.set_current_tick(0);

    EXPECT_TRUE(nw::combat::start_auto_attack(attacker, target1, 1, 60));
    events.advance(1);
    EXPECT_EQ(events.process(), 1);
    EXPECT_EQ(events.process(), 0);

    EXPECT_TRUE(nw::combat::start_auto_attack(attacker, target2, 1, 60));
    events.advance(1);
    EXPECT_EQ(events.process(), 1);
    EXPECT_EQ(events.process(), 0);

    EXPECT_TRUE(nw::combat::stop_auto_attack(attacker));
    EXPECT_FALSE(nw::combat::stop_auto_attack(attacker));
}

TEST(Creature, BaseAttackBonus)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(obj);

    EXPECT_EQ(27, creature_base_attack_bonus_from_script(obj));

    EXPECT_FALSE(knows_feat_from_script(obj, nwn1::feat_weapon_focus_unarmed));
    EXPECT_FALSE(knows_feat_from_script(obj, nwn1::feat_epic_weapon_focus_unarmed));
    EXPECT_TRUE(add_creature_propset_feat(obj, nwn1::feat_weapon_focus_unarmed));
    EXPECT_TRUE(add_creature_propset_feat(obj, nwn1::feat_epic_weapon_focus_unarmed));

    auto eff1 = nwn1::effect_attack_modifier(nwn1::attack_type_any, 2);
    EXPECT_TRUE(nwk::effects().apply_to(obj, eff1));
    auto eff2 = nwn1::effect_attack_modifier(nwn1::attack_type_unarmed, 3);
    EXPECT_TRUE(nwk::effects().apply_to(obj, eff2));
}

TEST(Creature, CombatPolicyModuleResolveAttackFullPayloadIntegration)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto attacker = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(attacker);

    auto target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(target);

    auto& rt = nw::kernel::runtime();
    auto* script = rt.load_module_from_source("test.custom_combat_policy_attack_full_payload", R"(
        import core.combat as C;
        from core.combat import { AttackData, DamageResult };
        from core.types import { Damage, DamageRoll };
        from nwn1.constants import { attack_type_offhand };

        fn resolve_attack(_attacker: Creature, _target: object): AttackData {
            var base: DamageResult = {
                damage_type = Damage(-1),
                amount = 0,
                unblocked = 0,
                immunity = 0,
                reduction = 0,
                reduction_remaining = 0,
                resist = 0,
                resist_remaining = 0,
            };
            var damages: array!(DamageResult);
            var rolls: array!(DamageRoll);
            return AttackData {
                attack_type = attack_type_offhand as int,
                attack_result = 2,
                attack_roll = 19,
                attack_bonus = 42,
                armor_class = 13,
                nth_attack = 1,
                damage_total = 17,
                critical_multiplier = 3,
                critical_threat = 4,
                concealment = 11,
                iteration_penalty = 5,
                is_ranged = true,
                target_is_creature = true,
                base_damage = base,
                damages = damages,
                rolls = rolls,
            };
        }
    )");
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nwk::config().set_combat_policy_module("test.custom_combat_policy_attack_full_payload");
    nw::AttackData attack;
    ASSERT_TRUE(nw::combat::resolve_attack(attacker, target, &attack));
    EXPECT_EQ(attack.type, nwn1::attack_type_offhand);
    EXPECT_EQ(attack.result, nw::AttackResult::hit_by_roll);
    EXPECT_EQ(attack.attack_roll, 19);
    EXPECT_EQ(attack.attack_bonus, 42);
    EXPECT_EQ(attack.armor_class, 13);
    EXPECT_EQ(attack.nth_attack, 1);
    EXPECT_EQ(attack.damage_total, 17);
    EXPECT_EQ(attack.multiplier, 3);
    EXPECT_EQ(attack.threat_range, 4);
    EXPECT_EQ(attack.concealment, 11);
    EXPECT_EQ(attack.iteration_penalty, 5);
    EXPECT_TRUE(attack.is_ranged_attack);
    EXPECT_TRUE(attack.target_is_creature);
    nwk::config().set_combat_policy_module("core.combat");
}

TEST(Creature, CombatPolicyModuleDamageMitigationEffectConsumption)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(target);

    auto versus = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(versus);

    auto& rt = nw::kernel::runtime();
    auto* script = rt.load_module_from_source("test.custom_combat_policy_damage_mitigation_effect_consumption", R"(
        import core.combat as C;
        import core.effects as Eff;
        import core.object as Obj;
        import nwn1.combat as Combat;
        from nwn1.constants import { damage_type_fire };

        const effect_type_damage_reduction = 12;
        const effect_type_damage_resistance = 2;

        fn add_damage_reduction(target: Creature, amount: int, bypass: int, remaining: int): int {
            var eff = Eff.create();
            if (!Eff.is_valid(eff)) {
                return 0;
            }

            Eff.set_type(eff, effect_type_damage_reduction);
            Eff.set_int(eff, 0, amount);
            Eff.set_int(eff, 1, bypass);
            Eff.set_int(eff, 2, remaining);
            return Obj.apply_effect(target, eff) ? 1 : 0;
        }

        fn add_damage_resistance(target: Creature, amount: int, remaining: int): int {
            var eff = Eff.create();
            if (!Eff.is_valid(eff)) {
                return 0;
            }

            Eff.set_type(eff, effect_type_damage_resistance);
            Eff.set_subtype(eff, damage_type_fire as int);
            Eff.set_int(eff, 0, amount);
            Eff.set_int(eff, 1, remaining);
            return Obj.apply_effect(target, eff) ? 1 : 0;
        }

        fn apply_damage_reduction_once(target: Creature, versus: Creature, incoming: int): int {
            return Combat.apply_damage_reduction(target, 10, versus, incoming);
        }

        fn apply_damage_resistance_once(target: Creature, versus: Creature, incoming: int): int {
            return Combat.apply_damage_resistance(target, damage_type_fire, versus, incoming);
        }

        fn damage_reduction_remaining(target: Creature): int {
            var eff = C.effect_best_damage_reduction(target, 10);
            if (!Eff.is_valid(eff)) {
                return -1;
            }
            return Eff.get_int(eff, 2);
        }

        fn damage_resistance_remaining(target: Creature): int {
            var eff = C.effect_best_damage_resistance(target, damage_type_fire);
            if (!Eff.is_valid(eff)) {
                return -1;
            }
            return Eff.get_int(eff, 1);
        }
    )");
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    auto make_obj_arg = [&rt](const nw::ObjectBase* obj) {
        auto value = nw::smalls::Value::make_object(obj->handle());
        value.type_id = rt.object_subtype_for_tag(obj->handle().type);
        return value;
    };

    auto exec_int = [&](const char* fn, nw::Vector<nw::smalls::Value> args) {
        auto result = rt.execute_script(script, fn, args);
        EXPECT_TRUE(result.ok()) << result.error_message;
        if (!result.ok()) { return 0; }
        EXPECT_EQ(result.value.type_id, rt.int_type());
        return result.value.data.ival;
    };

    auto target_arg = make_obj_arg(target);
    auto versus_arg = make_obj_arg(versus);

    EXPECT_EQ(exec_int("add_damage_reduction", {target_arg, nw::smalls::Value::make_int(6), nw::smalls::Value::make_int(20), nw::smalls::Value::make_int(5)}), 1);
    EXPECT_EQ(exec_int("apply_damage_reduction_once", {target_arg, versus_arg, nw::smalls::Value::make_int(4)}), 4);
    EXPECT_EQ(exec_int("damage_reduction_remaining", {target_arg}), 1);
    EXPECT_EQ(exec_int("apply_damage_reduction_once", {target_arg, versus_arg, nw::smalls::Value::make_int(4)}), 1);
    EXPECT_EQ(exec_int("damage_reduction_remaining", {target_arg}), -1);

    EXPECT_EQ(exec_int("add_damage_resistance", {target_arg, nw::smalls::Value::make_int(7), nw::smalls::Value::make_int(6)}), 1);
    EXPECT_EQ(exec_int("apply_damage_resistance_once", {target_arg, versus_arg, nw::smalls::Value::make_int(4)}), 4);
    EXPECT_EQ(exec_int("damage_resistance_remaining", {target_arg}), 2);
    EXPECT_EQ(exec_int("apply_damage_resistance_once", {target_arg, versus_arg, nw::smalls::Value::make_int(4)}), 2);
    EXPECT_EQ(exec_int("damage_resistance_remaining", {target_arg}), -1);
}

TEST(Creature, ChallengRating)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    EXPECT_TRUE(obj);
    EXPECT_NEAR(imported_creature_cr(obj), creature_challenge_rating_from_script(obj), 0.1);

    // auto obj2 = nwk::objects().load<nw::Creature>("nw_chicken"sv);
    // EXPECT_TRUE(obj2);
    // EXPECT_NEAR(imported_creature_cr(obj2), creature_challenge_rating_from_script(obj2), 0.01);

    auto obj3 = nwk::objects().load<nw::Creature>("x3_gzhorb"sv);
    EXPECT_TRUE(obj3);
    EXPECT_NEAR(imported_creature_cr(obj3), creature_challenge_rating_from_script(obj3), 0.01);
}

// TEST(Creature, CriticalHitMultiplier)
// {
//     auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
//     EXPECT_TRUE(mod);

//     auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
//     EXPECT_TRUE(obj);

//     int multiplier = nwn1::resolve_critical_multiplier(obj, nwn1::attack_type_onhand);
//     EXPECT_EQ(multiplier, 3);

//     auto obj2 = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
//     EXPECT_TRUE(obj2);
//     int multiplier2 = nwn1::resolve_critical_multiplier(obj2, nwn1::attack_type_onhand);
//     EXPECT_EQ(multiplier2, 2);
// }

// TEST(Creature, DamageWeaponFlags)
// {
//     auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
//     EXPECT_TRUE(mod);

//     auto obj1 = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
//     EXPECT_TRUE(obj1);
//     auto weapon1 = nw::get_equipped_item(obj1, nw::EquipIndex::righthand);
//     EXPECT_TRUE(weapon1);
//     EXPECT_TRUE(nwn1::resolve_weapon_damage_flags(weapon1).test(nwn1::damage_type_slashing));

//     auto obj2 = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
//     EXPECT_TRUE(obj2);
//     auto weapon2 = nw::get_equipped_item(obj2, nw::EquipIndex::arms);
//     EXPECT_TRUE(weapon2);
//     EXPECT_TRUE(nwn1::resolve_weapon_damage_flags(weapon2).test(nwn1::damage_type_bludgeoning));
// }

// TEST(Creature, DamageImmunity)
// {
//     auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
//     EXPECT_TRUE(mod);

//     auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
//     EXPECT_TRUE(obj);

//     auto eff = nwn1::effect_damage_immunity(nwn1::damage_type_bludgeoning, 10);
//     EXPECT_TRUE(nwk::effects().apply_to(obj, eff));
//     EXPECT_EQ(nwn1::resolve_damage_immunity(obj, nwn1::damage_type_bludgeoning), 10);

//     auto eff2 = nwn1::effect_damage_immunity(nwn1::damage_type_bludgeoning, -3);
//     EXPECT_TRUE(nwk::effects().apply_to(obj, eff2));
//     EXPECT_EQ(nwn1::resolve_damage_immunity(obj, nwn1::damage_type_bludgeoning), 7);

//     // Fake RDD.
//     auto obj2 = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
//     EXPECT_TRUE(obj2);
//     obj2->levels.entries[0].id = nwn1::class_type_dragon_disciple;
//     obj2->levels.entries[0].level = 20;
//     EXPECT_EQ(nwn1::resolve_damage_immunity(obj2, nwn1::damage_type_fire), 100);
// }

// TEST(Creature, DamageReduction)
// {
//     auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
//     EXPECT_TRUE(mod);

//     auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
//     EXPECT_TRUE(obj);

//     auto eff = nwn1::effect_damage_reduction(30, 2);
//     EXPECT_TRUE(nwk::effects().apply_to(obj, eff));
//     EXPECT_EQ(nwn1::resolve_damage_reduction(obj, 1).first, 30);
//     EXPECT_EQ(nwn1::resolve_damage_reduction(obj, 3).first, 0);

//     auto eff2 = nwn1::effect_damage_reduction(20, 4, 100);
//     EXPECT_TRUE(nwk::effects().apply_to(obj, eff2));
//     auto eff3 = nwn1::effect_damage_reduction(20, 4, 50);
//     EXPECT_TRUE(nwk::effects().apply_to(obj, eff3));
//     EXPECT_EQ(nwn1::resolve_damage_reduction(obj, 1).first, 30);
//     EXPECT_EQ(nwn1::resolve_damage_reduction(obj, 3).first, 20);
//     EXPECT_EQ(nwn1::resolve_damage_reduction(obj, 4).first, 0);

//     // Fake DD.
//     auto obj2 = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
//     EXPECT_TRUE(obj2);
//     obj2->levels.entries[0].id = nwn1::class_type_dwarven_defender;
//     obj2->levels.entries[0].level = 20;
//     EXPECT_EQ(nwn1::resolve_damage_reduction(obj2, 1).first, 12);

//     // Fake Barb.
//     auto obj3 = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
//     EXPECT_TRUE(obj3);
//     obj3->levels.entries[0].id = nwn1::class_type_barbarian;
//     obj3->levels.entries[0].level = 20;
//     EXPECT_EQ(nwn1::resolve_damage_reduction(obj3, 1).first, 4);

//     add_creature_propset_feat(obj3, nwn1::feat_epic_damage_reduction_6);
//     EXPECT_EQ(nwn1::resolve_damage_reduction(obj3, 1).first, 10);
// }

// TEST(Creature, DamageResistance)
// {
//     auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
//     EXPECT_TRUE(mod);

//     auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
//     EXPECT_TRUE(obj);

//     auto eff = nwn1::effect_damage_resistance(nwn1::damage_type_acid, 10);
//     EXPECT_TRUE(nwk::effects().apply_to(obj, eff));
//     EXPECT_EQ(nwn1::resolve_damage_resistance(obj, nwn1::damage_type_acid).first, 10);
//     EXPECT_EQ(nwn1::resolve_damage_resistance(obj, nwn1::damage_type_fire).first, 0);

//     auto obj3 = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
//     EXPECT_TRUE(obj3);
//     add_creature_propset_feat(obj3, nwn1::feat_epic_energy_resistance_sonic_3);
//     EXPECT_EQ(nwn1::resolve_damage_resistance(obj3, nwn1::damage_type_sonic).first, 30);
// }

TEST(Creature, Hitpoints)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(obj);

    EXPECT_EQ(object_hitpoints_from_script("get_current_hitpoints", obj), 4);
    EXPECT_EQ(object_hitpoints_from_script("get_max_hitpoints", obj), 4);
    const auto* vitals = nw::kernel::objects().components().find_vitals(obj->handle());
    ASSERT_NE(vitals, nullptr);
    EXPECT_EQ(vitals->hp_current, 4);
    EXPECT_EQ(vitals->hp_max, 4);

    auto eff = nwn1::effect_hitpoints_temporary(10);
    EXPECT_TRUE(nwk::effects().apply_to(obj, eff));
    EXPECT_EQ(object_hitpoints_from_script("get_max_hitpoints", obj), 14);
    vitals = nw::kernel::objects().components().find_vitals(obj->handle());
    ASSERT_NE(vitals, nullptr);
    EXPECT_EQ(vitals->hp_current, 14);
    EXPECT_EQ(vitals->hp_max, 14);

    const bool removed = nwk::effects().remove_from(obj, eff);
    if (removed) { nwk::effects().destroy(eff); }
    EXPECT_TRUE(removed);
    EXPECT_EQ(object_hitpoints_from_script("get_max_hitpoints", obj), 4);
    vitals = nw::kernel::objects().components().find_vitals(obj->handle());
    ASSERT_NE(vitals, nullptr);
    EXPECT_EQ(vitals->hp_current, 4);
    EXPECT_EQ(vitals->hp_max, 4);

    EXPECT_TRUE(add_creature_propset_feat(obj, nwn1::feat_toughness));
    EXPECT_EQ(object_hitpoints_from_script("get_max_hitpoints", obj), 5);
    EXPECT_TRUE(add_creature_propset_feat(obj, nwn1::feat_epic_toughness_5));
    EXPECT_EQ(object_hitpoints_from_script("get_max_hitpoints", obj), 105);
}

TEST(Creature, SavingThrows)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    EXPECT_TRUE(obj);
    EXPECT_EQ(creature_saving_throw_from_script(obj, nwn1::saving_throw_fort), 20);
    EXPECT_EQ(creature_saving_throw_from_script(obj, nwn1::saving_throw_reflex), 21);
    EXPECT_EQ(creature_saving_throw_from_script(obj, nwn1::saving_throw_will), 13);

    auto eff = nwn1::effect_save_modifier(nwn1::saving_throw_fort, 5);
    EXPECT_TRUE(eff);
    EXPECT_TRUE(nwk::effects().apply_to(obj, eff));
    EXPECT_EQ(creature_saving_throw_from_script(obj, nwn1::saving_throw_fort), 25);

    auto eff2 = nwn1::effect_save_modifier(nwn1::saving_throw_fort, -2);
    EXPECT_TRUE(eff2);
    EXPECT_TRUE(nwk::effects().apply_to(obj, eff2));
    EXPECT_EQ(creature_saving_throw_from_script(obj, nwn1::saving_throw_fort), 23);
}

TEST(Creature, JsonSerialization)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto fixture_name = nw::Creature::get_name_from_file(
        fs::path("test_data/user/development/pl_agent_001.utc.json"));
    EXPECT_EQ(fixture_name, "Agent");

    auto fixture_ent = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc.json");
    EXPECT_TRUE(fixture_ent);

    auto ent = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);
    EXPECT_TRUE(ent->save("tmp/pl_agent_001.utc.json", "json"));

    auto name = nw::Creature::get_name_from_file(fs::path("tmp/pl_agent_001.utc.json"));
    EXPECT_EQ(name, "Agent");

    auto ent2 = nw::kernel::objects().load_file<nw::Creature>("tmp/pl_agent_001.utc.json");
    EXPECT_TRUE(ent2);

    auto& rt = nw::kernel::runtime();
    auto stats = find_creature_propset(rt, ent2, "core.creature.CreatureStats");
    ASSERT_NE(stats.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_fixed_int_field(rt, stats, "abilities", *nwn1::ability_dexterity), 13);

    auto descriptor = find_creature_propset(rt, ent2, "core.creature.CreatureDescriptor");
    ASSERT_NE(descriptor.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_resref_field(rt, descriptor, "on_attacked"), nw::Resref{"mon_ai_5attacked"});
    EXPECT_EQ(script_int_field(rt, descriptor, "soundset"), 171);

    auto appearance = find_creature_propset(rt, ent2, "core.creature.CreatureAppearance");
    ASSERT_NE(appearance.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_int_field(rt, appearance, "appearance"), 6);
    EXPECT_EQ(script_int_field(rt, appearance, "body_part_shin_left"), 1);
    EXPECT_TRUE(nw::get_equipped_item(ent2, nw::EquipIndex::chest));
    EXPECT_EQ(script_int_field(rt, stats, "ac_natural_bonus"), 0);
    ASSERT_EQ(script_array_size(rt, stats, "special_abilities"), 1);
    auto special_ability = script_array_value(rt, stats, "special_abilities", 0);
    ASSERT_NE(special_ability.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(script_int_field(rt, special_ability, "spell"), *nw::Spell::make(120));
    EXPECT_EQ(creature_alignment_flags_from_script(ent2),
        static_cast<int32_t>(nw::align_neutral_good));

    auto drorry = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    EXPECT_TRUE(drorry);
    EXPECT_TRUE(drorry->save("tmp/drorry.utc.json", "json"));

    auto drorry_fixture = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/drorry.utc.json");
    EXPECT_TRUE(drorry_fixture);
}

TEST(Creature, JsonRoundTrip)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::serialize(ent, j, nw::SerializationProfile::blueprint);
    EXPECT_FALSE(j.contains("core.creature.CreatureCombat"));
    EXPECT_FALSE(j.contains("core.creature.CreatureCombatCache"));

    auto ent2 = nw::kernel::objects().make<nw::Creature>();
    EXPECT_TRUE(ent2);
    nw::deserialize(ent2, j, nw::SerializationProfile::blueprint);

    nlohmann::json j2;
    nw::serialize(ent2, j2, nw::SerializationProfile::blueprint);

    EXPECT_EQ(j, j2);
}

TEST(Creature, GffRoundTrip)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    nw::Gff g("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent->save("tmp/pl_agent_001_2.utc", "gff"));

    // Note: the below will typically always fail because the toolset,
    // doesn't sort feats when it writes out the gff.
    // nw::Gff g2("tmp/pl_agent_001_2.utc");
    // EXPECT_TRUE(g2.valid());
    // EXPECT_TRUE(nw::gff_to_gffjson(g) == nw::gff_to_gffjson(g2));

    // With updated entries we can no longer round trip.

    // EXPECT_EQ(oa.header.struct_offset, g.head_->struct_offset);
    // EXPECT_EQ(oa.header.struct_count, g.head_->struct_count);
    // EXPECT_EQ(oa.header.field_offset, g.head_->field_offset);
    // EXPECT_EQ(oa.header.field_count, g.head_->field_count);
    // EXPECT_EQ(oa.header.label_offset, g.head_->label_offset);
    // EXPECT_EQ(oa.header.label_count, g.head_->label_count);
    // EXPECT_EQ(oa.header.field_data_offset, g.head_->field_data_offset);
    // EXPECT_EQ(oa.header.field_data_count, g.head_->field_data_count);
    // EXPECT_EQ(oa.header.field_idx_offset, g.head_->field_idx_offset);
    // EXPECT_EQ(oa.header.field_idx_count, g.head_->field_idx_count);
    // EXPECT_EQ(oa.header.list_idx_offset, g.head_->list_idx_offset);
    // EXPECT_EQ(oa.header.list_idx_count, g.head_->list_idx_count);
}

TEST(Creature, Equips)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/test_creature.utc");
    EXPECT_TRUE(obj);
    EXPECT_TRUE(obj->instantiate());

    auto item = nwk::objects().load_file<nw::Item>("test_data/user/development/cloth028.uti");
    EXPECT_TRUE(item);

    EXPECT_FALSE(nw::can_equip_item(obj, item, nw::EquipIndex::invalid));
    EXPECT_FALSE(nw::equip_item_in_slot(obj, item, nw::EquipIndex::invalid));
    EXPECT_EQ(nw::get_equipped_item(obj, nw::EquipIndex::invalid), nullptr);
    EXPECT_EQ(nw::unequip_item_in_slot(obj, nw::EquipIndex::invalid), nullptr);

    EXPECT_TRUE(nwn1::equip_item(obj, item, nw::EquipIndex::chest));
    EXPECT_TRUE(nw::get_equipped_item(obj, nw::EquipIndex::chest));

    auto item1 = nwn1::unequip_item(obj, nw::EquipIndex::chest);
    EXPECT_TRUE(item1);
    EXPECT_FALSE(nw::get_equipped_item(obj, nw::EquipIndex::chest));

    EXPECT_FALSE(nwn1::equip_item(obj, item, nw::EquipIndex::head));

    auto item2 = nwn1::unequip_item(obj, nw::EquipIndex::head);
    EXPECT_FALSE(item2);

    auto boots_of_speed = nwk::objects().load<nw::Item>("nw_it_mboots005"sv);
    EXPECT_TRUE(boots_of_speed);
    EXPECT_TRUE(nwn1::equip_item(obj, boots_of_speed, nw::EquipIndex::boots));
    // Note: haste status is tracked in propset, not synced back to C++ object
    auto boots_of_speed2 = nwn1::unequip_item(obj, nw::EquipIndex::boots);
    EXPECT_TRUE(boots_of_speed2);
}

TEST(Creature, EffectsApplyRemove)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/test_creature.utc");
    EXPECT_TRUE(obj);

    auto eff = nwn1::effect_haste();
    EXPECT_TRUE(obj->effects().add(eff));
    EXPECT_TRUE(test_has_effect_applied(obj, nwn1::effect_type_haste));
    EXPECT_TRUE(obj->effects().size());
    EXPECT_TRUE(obj->effects().remove(eff));
    EXPECT_FALSE(test_has_effect_applied(obj, nwn1::effect_type_haste));
    EXPECT_EQ(obj->effects().size(), 0);
    EXPECT_FALSE(obj->effects().remove(nullptr));
}

TEST(Creature, EffectsBatchCallback)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/test_creature.utc");
    EXPECT_TRUE(obj);

    auto& effects = nwk::effects();
    g_effect_batch_types.clear();
    g_effect_batch_is_apply = false;
    effects.set_event_batch_callback(capture_effect_batch);

    auto* haste = nwn1::effect_haste();
    auto* temp_hp = nwn1::effect_hitpoints_temporary(5);
    ASSERT_TRUE(haste);
    ASSERT_TRUE(temp_hp);

    nw::Vector<nw::Effect*> to_apply{haste, temp_hp};
    nw::Vector<nw::Effect*> apply_failed;
    auto applied = effects.apply_to(obj, to_apply, &apply_failed);
    EXPECT_EQ(applied, 2);
    EXPECT_TRUE(apply_failed.empty());

    ASSERT_EQ(g_effect_batch_types.size(), 2);
    EXPECT_EQ(g_effect_batch_types[0], nwn1::effect_type_haste);
    EXPECT_EQ(g_effect_batch_types[1], nwn1::effect_type_temporary_hitpoints);
    EXPECT_TRUE(g_effect_batch_is_apply);

    g_effect_batch_types.clear();
    nw::Vector<nw::Effect*> to_remove{haste, temp_hp};
    nw::Vector<nw::Effect*> remove_failed;
    auto removed = effects.remove_from(obj, to_remove, true, &remove_failed);
    EXPECT_EQ(removed, 2);
    EXPECT_TRUE(remove_failed.empty());

    ASSERT_EQ(g_effect_batch_types.size(), 2);
    EXPECT_FALSE(g_effect_batch_is_apply);

    effects.set_event_batch_callback({});
}

TEST(Creature, Casting)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj1 = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/spell_test_2.utc");
    EXPECT_TRUE(obj1);
    EXPECT_EQ(creature_get_caster_level_from_script(obj1, nwn1::class_type_sorcerer), 20);
    EXPECT_EQ(creature_get_caster_level_from_script(obj1, nwn1::class_type_assassin), 0);

    EXPECT_EQ(creature_compute_total_spell_slots_from_script(obj1, nwn1::class_type_sorcerer, 7), 7);

    // Sorcerer - Known Spells
    EXPECT_FALSE(creature_add_known_spell_from_script(obj1, nwn1::class_type_sorcerer, nwn1::spell_delayed_blast_fireball));
    creature_remove_known_spell_from_script(obj1, nwn1::class_type_sorcerer, nwn1::spell_delayed_blast_fireball);
    EXPECT_TRUE(creature_get_knows_spell_from_script(obj1, nwn1::class_type_sorcerer, nwn1::spell_mordenkainens_sword));
    creature_remove_known_spell_from_script(obj1, nwn1::class_type_sorcerer, nwn1::spell_mordenkainens_sword);
    EXPECT_TRUE(creature_add_known_spell_from_script(obj1, nwn1::class_type_sorcerer, nwn1::spell_delayed_blast_fireball));
    EXPECT_TRUE(creature_get_knows_spell_from_script(obj1, nwn1::class_type_sorcerer, nwn1::spell_delayed_blast_fireball));
    creature_remove_known_spell_from_script(obj1, nwn1::class_type_sorcerer, nwn1::spell_delayed_blast_fireball);
    EXPECT_FALSE(creature_get_knows_spell_from_script(obj1, nwn1::class_type_sorcerer, nwn1::spell_delayed_blast_fireball));

    // Sorcerer - No Memorized Spells
    EXPECT_FALSE(creature_add_memorized_spell_from_script(obj1, nwn1::class_type_sorcerer, nwn1::spell_delayed_blast_fireball));

    auto obj2 = nwk::objects().load_file<nw::Creature>("test_data/user/development/wizard_pm.utc");
    EXPECT_TRUE(obj2);
    auto obj2_levels = find_creature_propset(nw::kernel::runtime(), obj2, "core.creature.CreatureLevels");
    ASSERT_NE(obj2_levels.type_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(creature_propset_level_by_class(nw::kernel::runtime(), obj2_levels, nwn1::class_type_wizard), 15);
    EXPECT_EQ(creature_propset_level_by_class(nw::kernel::runtime(), obj2_levels, nwn1::class_type_palemaster), 6);
    EXPECT_EQ(creature_get_caster_level_from_script(obj2, nwn1::class_type_wizard), 18);

    EXPECT_EQ(creature_ability_modifier_from_script(obj2, nwn1::ability_intelligence), 5);
    EXPECT_EQ(creature_get_spell_dc_from_script(obj2, nwn1::class_type_wizard, nwn1::spell_fireball), 18);
    EXPECT_TRUE(add_creature_propset_feat(obj2, nwn1::feat_spell_focus_evocation));
    EXPECT_EQ(creature_get_spell_dc_from_script(obj2, nwn1::class_type_wizard, nwn1::spell_fireball), 20);
    EXPECT_TRUE(add_creature_propset_feat(obj2, nwn1::feat_greater_spell_focus_evocation));
    EXPECT_EQ(creature_get_spell_dc_from_script(obj2, nwn1::class_type_wizard, nwn1::spell_fireball), 22);
    EXPECT_TRUE(add_creature_propset_feat(obj2, nwn1::feat_epic_spell_focus_evocation));
    EXPECT_EQ(creature_get_spell_dc_from_script(obj2, nwn1::class_type_wizard, nwn1::spell_fireball), 24);

    EXPECT_TRUE(creature_add_known_spell_from_script(obj2, nwn1::class_type_wizard, nwn1::spell_delayed_blast_fireball));
    EXPECT_FALSE(creature_add_known_spell_from_script(obj2, nwn1::class_type_wizard, nwn1::spell_delayed_blast_fireball));

    EXPECT_EQ(creature_compute_total_spell_slots_from_script(obj2, nwn1::class_type_wizard, 0), 5);
    EXPECT_EQ(creature_compute_total_spell_slots_from_script(obj2, nwn1::class_type_wizard, 8), 3);
    EXPECT_EQ(creature_compute_total_spell_slots_from_script(obj2, nwn1::class_type_wizard, 9), 2);

    EXPECT_EQ(creature_get_available_spell_slots_from_script(obj2, nwn1::class_type_wizard, 9), 2);

    auto eff2 = nwn1::effect_bonus_spell_slot(nwn1::class_type_wizard, 9);
    EXPECT_TRUE(eff2);
    nwk::effects().apply_to(obj2, eff2);
    EXPECT_EQ(creature_compute_total_spell_slots_from_script(obj2, nwn1::class_type_wizard, 9), 3);
    EXPECT_TRUE(recompute_creature_available_spell_slots(obj2));
    EXPECT_EQ(creature_get_available_spell_slots_from_script(obj2, nwn1::class_type_wizard, 9), 3);
}

TEST(Creature, AbilityLoadout)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto& components = nwk::objects().components();

    auto obj1 = nwk::objects().load_file<nw::Creature>("test_data/user/development/wizard_pm.utc");
    EXPECT_TRUE(obj1);
    EXPECT_EQ(components.count_slotted_ability(obj1->handle(), *nwn1::class_type_wizard,
                  *nwn1::spell_light, -1),
        1);

    auto obj2 = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/sorcrdd.utc");
    EXPECT_TRUE(obj2);
    EXPECT_TRUE(obj2->save("tmp/sorcrdd.utc.json"));
    EXPECT_EQ(creature_get_caster_level_from_script(obj2, nwn1::class_type_sorcerer), 20);
    EXPECT_TRUE(components.find_ability_loadout(obj2->handle()));
    EXPECT_TRUE(creature_get_knows_spell_from_script(obj2, nwn1::class_type_sorcerer, nwn1::spell_light));
    EXPECT_EQ(creature_compute_total_spell_slots_from_script(obj2, nwn1::class_type_sorcerer, 0), 7);

    auto obj3 = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/spell_test_1.utc");
    EXPECT_TRUE(obj3);
    EXPECT_TRUE(obj3->save("tmp/spell_test_1.utc.json"));
    EXPECT_TRUE(components.find_ability_loadout(obj3->handle()));
    EXPECT_TRUE(creature_get_knows_spell_from_script(obj3, nwn1::class_type_bard, nwn1::spell_bulls_strength));
    EXPECT_EQ(components.unslotted_ability_count(obj3->handle(), *nwn1::class_type_bard), 1);

    auto obj4 = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/spell_test_2.utc");
    EXPECT_TRUE(obj4);
    EXPECT_TRUE(obj4->save("tmp/spell_test_2.utc.json"));
    {
        std::ifstream saved_json{"tmp/spell_test_2.utc.json"};
        ASSERT_TRUE(saved_json.good());
        nlohmann::json saved;
        saved_json >> saved;
        ASSERT_TRUE(saved.contains("components"));
        ASSERT_TRUE(saved["components"].contains("ability_loadout"));
        EXPECT_FALSE(saved["components"]["ability_loadout"].empty());
        ASSERT_TRUE(saved["components"]["ability_loadout"].front().contains("source"));
        ASSERT_TRUE(saved["components"]["ability_loadout"].front().contains("tier"));
        ASSERT_TRUE(saved["components"]["ability_loadout"].front().contains("slot"));
        ASSERT_TRUE(saved["components"]["ability_loadout"].front().contains("ability"));
        EXPECT_FALSE(saved.contains("levels"));
        ASSERT_TRUE(saved.contains("core.creature.CreatureLevels"));
        EXPECT_FALSE(saved["core.creature.CreatureLevels"].contains("spellbook"));
    }
    EXPECT_TRUE(components.find_ability_loadout(obj4->handle()));
    EXPECT_EQ(components.count_slotted_ability(obj4->handle(), *nwn1::class_type_cleric,
                  *nwn1::spell_hammer_of_the_gods, -1),
        3);
    EXPECT_EQ(creature_available_spell_uses_from_script(obj4, nwn1::class_type_cleric,
                  nwn1::spell_hammer_of_the_gods, 4, nw::metamagic_any),
        3);
    EXPECT_EQ(creature_available_spell_uses_from_script(obj4, nwn1::class_type_cleric,
                  nwn1::spell_hammer_of_the_gods, 5, nw::metamagic_any),
        0);
    EXPECT_EQ(components.slotted_ability_count(obj4->handle(), *nwn1::class_type_cleric), 66);
    EXPECT_EQ(components.find_slotted_ability_slot(obj4->handle(), *nwn1::class_type_cleric, 4,
                  *nwn1::spell_hammer_of_the_gods, *nw::metamagic_none),
        4);
    components.clear_slotted_ability(obj4->handle(), *nwn1::class_type_cleric, 4, 4);
    EXPECT_EQ(components.first_empty_ability_slot(obj4->handle(), *nwn1::class_type_cleric, 4), 4);
    EXPECT_EQ(components.count_slotted_ability(obj4->handle(), *nwn1::class_type_cleric,
                  *nwn1::spell_hammer_of_the_gods, -1),
        2);
    EXPECT_EQ(components.find_slotted_ability_slot(obj4->handle(), *nwn1::class_type_cleric, 4,
                  *nwn1::spell_hammer_of_the_gods, *nw::metamagic_none),
        5);
}

TEST(Creature, SpecialAbilities)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto& rt = nwk::runtime();
    rt.add_module_path(fs::path("stdlib/core"));
    rt.add_module_path(fs::path("stdlib/nwn1"));

    constexpr std::string_view src = R"(
        from core.creature import { SpecialAbility };
        from core.types import { Spell };
        import nwn1.creature as NCre;

        fn has_not(obj: Creature, spell: int): bool {
            return !NCre.has_special_ability(obj, Spell(spell));
        }
        fn set_with_level(obj: Creature, spell: int, level: int) {
            NCre.set_special_ability(obj, SpecialAbility { spell = Spell(spell), uses = 1, level = level });
        }
        fn set_no_level(obj: Creature, spell: int) {
            NCre.set_special_ability(obj, SpecialAbility { spell = Spell(spell), uses = 1, level = 0 });
        }
        fn get_level(obj: Creature, spell: int): int {
            return NCre.get_special_ability(obj, Spell(spell)).level;
        }
        fn get_uses(obj: Creature, spell: int): int {
            return NCre.get_special_ability(obj, Spell(spell)).uses;
        }
        fn remove_one(obj: Creature, spell: int) {
            NCre.remove_special_ability(obj, Spell(spell));
        }
    )";

    auto* script = rt.load_module_from_source("test.special_abilities", src);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    auto* obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/wizard_pm.utc");
    ASSERT_NE(obj, nullptr);

    constexpr int spell_id = 367; // spell_horrid_wilting

    auto obj_arg = nw::smalls::Value::make_object(obj->handle());
    obj_arg.type_id = rt.object_subtype_for_tag(obj->handle().type);
    auto spell_arg = nw::smalls::Value::make_int(spell_id);

    auto exec_bool = [&](std::string_view fn, nw::Vector<nw::smalls::Value> args) {
        auto result = rt.execute_script(script, fn, args);
        EXPECT_TRUE(result.ok()) << result.error_message;
        return result.value.data.bval;
    };
    auto exec_int = [&](std::string_view fn, nw::Vector<nw::smalls::Value> args) {
        auto result = rt.execute_script(script, fn, args);
        EXPECT_TRUE(result.ok()) << result.error_message;
        return result.value.data.ival;
    };
    auto exec_void = [&](std::string_view fn, nw::Vector<nw::smalls::Value> args) {
        auto result = rt.execute_script(script, fn, args);
        EXPECT_TRUE(result.ok()) << result.error_message;
    };

    EXPECT_TRUE(exec_bool("has_not", {obj_arg, spell_arg}));
    exec_void("set_with_level", {obj_arg, spell_arg, nw::smalls::Value::make_int(10)});
    EXPECT_EQ(exec_int("get_level", {obj_arg, spell_arg}), 10);
    EXPECT_EQ(exec_int("get_uses", {obj_arg, spell_arg}), 1);
    exec_void("set_no_level", {obj_arg, spell_arg});
    EXPECT_EQ(exec_int("get_uses", {obj_arg, spell_arg}), 1);
    exec_void("remove_one", {obj_arg, spell_arg});
    EXPECT_EQ(exec_int("get_uses", {obj_arg, spell_arg}), 0);
}
