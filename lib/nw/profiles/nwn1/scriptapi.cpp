#include "scriptapi.hpp"

#include "constants.hpp"

#include "../../functions.hpp"
#include "../../kernel/Config.hpp"
#include "../../kernel/Rules.hpp"
#include "../../kernel/TwoDACache.hpp"
#include "../../objects/Door.hpp"
#include "../../objects/Placeable.hpp"
#include "../../objects/Player.hpp"
#include "../../rules/Class.hpp"
#include "../../rules/combat.hpp"
#include "../../rules/effects.hpp"
#include "../../scriptapi.hpp"
#include "../../smalls/Array.hpp"
#include "../../smalls/Bytecode.hpp"
#include "../../smalls/runtime.hpp"
#include "../../util/macros.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace nwn1 {

// Legacy compatibility implementation for nwn1::scriptapi.hpp.
// Keep behavior stable while callers move to profile-agnostic APIs; avoid
// adding new long-term dependencies on symbols in this namespace.

static bool resolve_saving_throw(const nw::ObjectBase* obj, nw::Save type, int dc,
    nw::SaveVersus type_vs, const nw::ObjectBase* versus);
static bool resolve_skill_check(const nw::Creature* obj, nw::Skill skill, int dc,
    nw::ObjectBase* versus = nullptr);
static bool is_creature_weapon(const nw::Item* item);
static bool is_shield(nw::BaseItem baseitem);
static bool is_unarmed_weapon(const nw::Item* item);

static nw::DiceRoll resolve_creature_damage(const nw::Creature* attacker, nw::Item* weapon);

namespace {

thread_local bool in_combat_policy_dispatch = false;
thread_local nw::Vector<nw::smalls::Value> policy_args_cache;

/// Pre-cached byte offsets for all AttackData fields.
/// Resolved once when resolve_attack is first compiled; used for zero-copy decode.
struct AttackDataOffsetCache {
    bool valid = false;
    uint32_t attack_type;
    uint32_t attack_result;
    uint32_t attack_roll;
    uint32_t attack_bonus;
    uint32_t armor_class;
    uint32_t nth_attack;
    uint32_t damage_total;
    uint32_t critical_multiplier;
    uint32_t critical_threat;
    uint32_t concealment;
    uint32_t iteration_penalty;
    uint32_t is_ranged;
    uint32_t target_is_creature;
};

struct ResolveAttackCache {
    nw::String module;
    const nw::smalls::BytecodeModule* bytecode_module = nullptr;
    const nw::smalls::CompiledFunction* compiled_function = nullptr;
    bool known_missing = false;
    bool function_resolved = false;
    AttackDataOffsetCache offsets;
};

thread_local ResolveAttackCache s_resolve_attack;

thread_local bool combat_policy_timing_enabled = false;
thread_local bool combat_resolve_timing_enabled = false;

struct CombatPolicyTimingData {
    uint64_t policy_call_ns = 0;
    uint64_t decode_ns = 0;
    uint64_t fallback_ns = 0;
    uint64_t iterations = 0;
};

struct CombatResolveTimingData {
    uint64_t prepare_ns = 0;
    uint64_t policy_call_ns = 0;
    uint64_t decode_ns = 0;
    uint64_t marshal_ns = 0;
    uint64_t total_ns = 0;
    uint64_t iterations = 0;
};

thread_local CombatPolicyTimingData combat_policy_timing;
thread_local CombatResolveTimingData combat_resolve_timing;

nw::MetaMagic metamagic_flag_to_idx_internal(nw::MetaMagicFlag flag)
{
    switch (*flag) {
    case *metamagic_empower:
        return metamagic_idx_empower;
    case *metamagic_extend:
        return metamagic_idx_extend;
    case *metamagic_maximize:
        return metamagic_idx_maximize;
    case *metamagic_quicken:
        return metamagic_idx_quicken;
    case *metamagic_silent:
        return metamagic_idx_silent;
    case *metamagic_still:
        return metamagic_idx_still;
    }
    return nw::MetaMagic::invalid();
}

inline uint64_t now_ns() noexcept
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

nw::StringView configured_combat_module() noexcept
{
    return nw::kernel::config().combat_policy_module();
}

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

        if (type->type_params.empty() || !type->type_params[0].is<nw::smalls::TypeID>()) {
            return false;
        }
        type_id = type->type_params[0].as<nw::smalls::TypeID>();
    }

    return true;
}

std::optional<nw::smalls::Value> dispatch_resolve_attack(const nw::Vector<nw::smalls::Value>& args)
{
    auto& rt = nw::kernel::runtime();
    auto& cache = s_resolve_attack;
    auto module = configured_combat_module();

    auto* script = rt.get_module(module);
    if (!script) {
        LOG_F(ERROR, "[nwn1] combat policy module '{}' failed to load", module);
        return std::nullopt;
    }

    if (cache.module != module) {
        cache = {};
        cache.module = nw::String(module);
    }

    struct PolicyCallGuard {
        PolicyCallGuard() noexcept { in_combat_policy_dispatch = true; }
        ~PolicyCallGuard() noexcept { in_combat_policy_dispatch = false; }
    } guard;

    auto* bytecode_module = rt.get_or_compile_module(script);
    if (!bytecode_module) {
        LOG_F(ERROR, "[nwn1] combat policy module '{}' failed to compile", module);
        return std::nullopt;
    }

    if (cache.bytecode_module != bytecode_module) {
        cache.bytecode_module = bytecode_module;
        cache.compiled_function = nullptr;
        cache.known_missing = false;
        cache.function_resolved = false;
        cache.offsets = {};
    }

    if (!cache.function_resolved) {
        cache.compiled_function = bytecode_module->get_function("resolve_attack");
        cache.function_resolved = true;
        cache.known_missing = cache.compiled_function == nullptr;
        if (cache.known_missing) {
            LOG_F(ERROR, "[nwn1] combat policy '{}.resolve_attack' not found", module);
        }

        if (cache.compiled_function) {
            const auto* ret_type = rt.get_type(cache.compiled_function->return_type);
            if (ret_type && ret_type->type_kind == nw::smalls::TK_struct
                && rt.type_name(cache.compiled_function->return_type) == "core.combat.AttackData") {
                auto sid = ret_type->type_params[0].as<nw::smalls::StructID>();
                const auto* def = rt.type_table_.get(sid);
                if (def) {
                    auto& c = cache.offsets;
                    auto resolve_field = [&](nw::StringView name) -> uint32_t {
                        uint32_t idx = def->field_index(name);
                        return (idx != UINT32_MAX) ? def->fields[idx].offset : UINT32_MAX;
                    };
                    c.attack_type = resolve_field("attack_type");
                    c.attack_result = resolve_field("attack_result");
                    c.attack_roll = resolve_field("attack_roll");
                    c.attack_bonus = resolve_field("attack_bonus");
                    c.armor_class = resolve_field("armor_class");
                    c.nth_attack = resolve_field("nth_attack");
                    c.damage_total = resolve_field("damage_total");
                    c.critical_multiplier = resolve_field("critical_multiplier");
                    c.critical_threat = resolve_field("critical_threat");
                    c.concealment = resolve_field("concealment");
                    c.iteration_penalty = resolve_field("iteration_penalty");
                    c.is_ranged = resolve_field("is_ranged");
                    c.target_is_creature = resolve_field("target_is_creature");
                    c.valid = (c.attack_type != UINT32_MAX);
                }
            }
        }
    }

    if (cache.known_missing) {
        return std::nullopt;
    }

    auto result = rt.execute_compiled(bytecode_module, cache.compiled_function, args);
    if (!result.ok()) {
        LOG_F(ERROR, "[nwn1] combat policy '{}.resolve_attack' failed: {}", module, result.error_message);
        return std::nullopt;
    }

    return result.value;
}

nw::smalls::Value make_object_arg(nw::ObjectHandle handle)
{
    auto& rt = nw::kernel::runtime();
    auto value = nw::smalls::Value::make_object(handle);
    value.type_id = rt.object_subtype_for_tag(handle.type);
    return value;
}

const nw::smalls::StructDef* get_attack_data_def(nw::smalls::Runtime& rt, const nw::smalls::Value& value)
{
    const auto* type = rt.get_type(value.type_id);
    if (!type || type->type_kind != nw::smalls::TK_struct || !type->type_params[0].is<nw::smalls::StructID>()) {
        return nullptr;
    }

    if (rt.type_name(value.type_id) != "core.combat.AttackData") {
        return nullptr;
    }

    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    return rt.type_table_.get(struct_id);
}

bool read_attack_data_int_field(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    const nw::smalls::StructDef* def, nw::StringView field_name, int32_t& out)
{
    if (!def) {
        return false;
    }

    uint32_t field_index = def->field_index(field_name);
    if (field_index == UINT32_MAX) {
        return false;
    }

    const auto& field = def->fields[field_index];
    auto value = rt.read_value_field_at_offset(data, field.offset, field.type_id);
    if (!value.type_id.value) {
        return false;
    }

    if (!is_int_compatible_type(rt, value.type_id)) {
        return false;
    }

    out = value.data.ival;
    return true;
}

bool read_attack_data_bool_field(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    const nw::smalls::StructDef* def, nw::StringView field_name, bool& out)
{
    if (!def) {
        return false;
    }

    uint32_t field_index = def->field_index(field_name);
    if (field_index == UINT32_MAX) {
        return false;
    }

    const auto& field = def->fields[field_index];
    auto value = rt.read_value_field_at_offset(data, field.offset, field.type_id);
    if (!value.type_id.value) {
        return false;
    }

    if (value.type_id == rt.bool_type()) {
        out = value.data.bval;
        return true;
    }

    if (!is_int_compatible_type(rt, value.type_id)) {
        return false;
    }

    out = value.data.ival != 0;
    return true;
}

void read_attack_data_effects_field(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    const nw::smalls::StructDef* def, nw::StringView field_name,
    absl::InlinedVector<nw::Effect*, 8>& out)
{
    out.clear();
    if (!def) {
        return;
    }

    uint32_t field_index = def->field_index(field_name);
    if (field_index == UINT32_MAX) {
        return;
    }

    const auto& field = def->fields[field_index];
    auto value = rt.read_value_field_at_offset(data, field.offset, field.type_id);
    if (!value.type_id.value) {
        return;
    }

    auto* values = nw::smalls::detail::value_cast<nw::smalls::IArray*>(&rt, value);
    if (!values) {
        return;
    }

    out.reserve(values->size());
    nw::smalls::Value elem;
    for (size_t i = 0; i < values->size(); ++i) {
        if (!values->get_value(i, elem, rt)) {
            continue;
        }

        auto handle = nw::smalls::detail::value_cast<nw::TypedHandle>(&rt, elem);
        if (!handle.is_valid()) {
            continue;
        }

        if (auto* effect = nw::kernel::effects().get(handle)) {
            out.push_back(effect);
            nw::kernel::runtime().set_handle_ownership(handle, nw::smalls::OwnershipMode::ENGINE_OWNED);
        }
    }
}

} // namespace

void set_combat_policy_timing_enabled(bool enabled) noexcept
{
    combat_policy_timing_enabled = enabled;
}

void reset_combat_policy_timing() noexcept
{
    combat_policy_timing = {};
}

nwn1::CombatPolicyTimingSnapshot combat_policy_timing_snapshot() noexcept
{
    return {
        .policy_call_ns = combat_policy_timing.policy_call_ns,
        .decode_ns = combat_policy_timing.decode_ns,
        .fallback_ns = combat_policy_timing.fallback_ns,
        .iterations = combat_policy_timing.iterations,
    };
}

bool is_combat_policy_timing_enabled() noexcept
{
    return combat_policy_timing_enabled;
}

void record_combat_policy_timing(uint64_t policy_ns, uint64_t decode_ns, uint64_t fallback_ns) noexcept
{
    combat_policy_timing.policy_call_ns += policy_ns;
    combat_policy_timing.decode_ns += decode_ns;
    combat_policy_timing.fallback_ns += fallback_ns;
    ++combat_policy_timing.iterations;
}

void set_combat_resolve_timing_enabled(bool enabled) noexcept
{
    combat_resolve_timing_enabled = enabled;
}

void reset_combat_resolve_timing() noexcept
{
    combat_resolve_timing = {};
}

nwn1::CombatResolveTimingSnapshot combat_resolve_timing_snapshot() noexcept
{
    return {
        .prepare_ns = combat_resolve_timing.prepare_ns,
        .policy_call_ns = combat_resolve_timing.policy_call_ns,
        .decode_ns = combat_resolve_timing.decode_ns,
        .marshal_ns = combat_resolve_timing.marshal_ns,
        .total_ns = combat_resolve_timing.total_ns,
        .iterations = combat_resolve_timing.iterations,
    };
}

bool is_combat_resolve_timing_enabled() noexcept
{
    return combat_resolve_timing_enabled;
}

void record_combat_resolve_timing(uint64_t prepare_ns, uint64_t policy_ns, uint64_t decode_ns,
    uint64_t marshal_ns, uint64_t total_ns) noexcept
{
    combat_resolve_timing.prepare_ns += prepare_ns;
    combat_resolve_timing.policy_call_ns += policy_ns;
    combat_resolve_timing.decode_ns += decode_ns;
    combat_resolve_timing.marshal_ns += marshal_ns;
    combat_resolve_timing.total_ns += total_ns;
    ++combat_resolve_timing.iterations;
}

// ============================================================================
// == Object ==================================================================
// ============================================================================

float calculate_challenge_rating(const nw::Creature* obj)
{
    int hd = obj->levels.level();
    if (hd <= 0) { return 0.0f; }
    float temp = 0;

    // HD * 0.15
    float base_cr = hd * 0.15f;
    LOG_F(INFO, "base_cr: {}", base_cr);

    // (Natural AC bonus) * 0.1
    float natural_ac_cr = obj->combat_info.ac_natural_bonus * 0.1;
    LOG_F(INFO, "natural_ac_cr: {}", natural_ac_cr);

    // [ (Inventory Value) / (HD * 20000 + 100000) ] * 0.2 * HD
    // Equipped non-creature items only
    float inventory_value = 0.0f;
    for (size_t i = 0; i < size_t(nw::EquipIndex::creature_left); ++i) {
        if (obj->equipment.equips[i].is<nw::Item*>()) {
            if (!obj->equipment.equips[i].as<nw::Item*>()) { continue; }
            inventory_value += calculate_item_value(obj->equipment.equips[i].as<nw::Item*>());
        }
    }

    float inventory_cr = (inventory_value / (hd * 20000 + 100000)) * 0.2 * hd;
    LOG_F(INFO, "inventory_cr: {}", inventory_cr);

    // [ (Total HP) / (Average HP) ] * 0.2 * HD * (Walk Rate) / (Standard Walk Rate)
    float average_hitpoints = 0.0f;
    float total_hitpoints = 0.0f;

    auto walkrate_2da = nw::kernel::twodas().get("creaturespeed");
    float walkrate, player_walkrate;

    if (!walkrate_2da->get_to(obj->walkrate, "WALKRATE", walkrate)
        || !walkrate_2da->get_to(0, "WALKRATE", player_walkrate)) {
        return 0.0f;
    }

    for (const auto& cls : obj->levels.entries) {
        if (cls.id == nw::Class::invalid()) { break; }
        auto cls_info = nw::kernel::rules().classes.get(cls.id);
        if (!cls_info) { break; }
        average_hitpoints += cls.level * (float(cls_info->hitdie + 1) / 2);
        total_hitpoints += cls.level * cls_info->hitdie;
    }
    LOG_F(INFO, "base: {}, walrate: {}, player walk rate: {}", obj->walkrate, walkrate, player_walkrate);
    float hitpoint_cr = (float(obj->hp) / average_hitpoints) * 0.2 * hd * (walkrate / player_walkrate);
    LOG_F(INFO, "hp: {}, average_hitpoints {},  hitpoint_cr: {}", obj->hp, average_hitpoints, hitpoint_cr);

    // [ (Total of all Ability Scores) / (HD + 50) ] * 0.1 * HD
    temp = 0; // Ability totals
    for (auto abil : obj->stats.abilities_) {
        temp += abil;
    }
    float ability_cr = (temp / (hd + 50)) * 0.1 * hd;
    LOG_F(INFO, "ability_cr: {}", ability_cr);

    // [ (Total Special Ability Levels) / { (HD * (HD + 1) ) + (HD * 5 ) } ] * 0.15 * HD
    temp = 0; // Total Levels
    for (const auto& specabil : obj->combat_info.special_abilities) {
        auto sp = nw::kernel::rules().spells.get(specabil.spell);
        if (!sp) { continue; }
        temp += sp->innate_level > 0 ? sp->innate_level : 0.5f;
    }
    float specabil_cr = (float(temp) / (hd * (hd + 1) + (hd * 5))) * 0.15 * hd;
    LOG_F(INFO, "specabil_cr: {}", specabil_cr);

    // [ (Total Spell Levels) / { (HD * (HD + 1) ) } ] * 0.15 * HD
    float total_spell_levels = 0.0f;
    for (const auto& cls : obj->levels.entries) {
        for (const auto& spell_level : cls.spells.known_) {
            for (const auto& spell : spell_level) {
                auto sp = nw::kernel::rules().spells.get(spell);
                if (!sp) { continue; }
                total_spell_levels += sp->innate_level > 0 ? sp->innate_level : 0.5f;
            }
        }
        for (const auto& spell_level : cls.spells.memorized_) {
            for (const auto& spell : spell_level) {
                auto sp = nw::kernel::rules().spells.get(spell.spell);
                if (!sp) { continue; }
                total_spell_levels += sp->innate_level > 0 ? sp->innate_level : 0.5f;
            }
        }
    }
    float spells_cr = (total_spell_levels / (hd * (hd + 1))) * 0.15 * hd;
    LOG_F(INFO, "spells_cr: {}", spells_cr);

    // [ ((Bonus Saves) + (Base Saves)) / (Base Saves) ] * 0.15 * HD
    int bonus_saves = obj->stats.save_bonus.fort + obj->stats.save_bonus.reflex + obj->stats.save_bonus.will;
    int base_saves = nwn1::saving_throw(obj, saving_throw_fort, nw::SaveVersus::invalid(), nullptr, true)
        + nwn1::saving_throw(obj, saving_throw_reflex, nw::SaveVersus::invalid(), nullptr, true)
        + nwn1::saving_throw(obj, saving_throw_will, nw::SaveVersus::invalid(), nullptr, true);
    base_saves -= bonus_saves;
    float saves_cr = 0.15 * hd;
    if (base_saves > 0) {
        saves_cr *= ((bonus_saves + base_saves) / float(base_saves));
    }
    LOG_F(INFO, "saves_cr: {}", saves_cr);

    // [ (Total Feat CR Values) / (HD * 0.5 + 7) ] * 0.1 * HD
    float total_feat_values = 0.0f;
    for (auto feat : obj->stats.feats_) {
        auto feat_info = nw::kernel::rules().feats.get(feat);
        if (!feat_info) { continue; }
        total_feat_values += feat_info->cr_value;
    }
    float feat_cr = (total_feat_values / (hd * 0.5 + 7)) * 0.1 * hd;
    LOG_F(INFO, "feat_cr: {}", feat_cr);

    float additive_cr = base_cr
        + natural_ac_cr
        + inventory_cr
        + hitpoint_cr
        + ability_cr
        + specabil_cr
        + spells_cr
        + saves_cr
        + feat_cr;

    LOG_F(INFO, "additive_cr before race: {}", additive_cr);

    auto race_info = nw::kernel::rules().races.get(obj->race);
    if (!race_info) { return 0.0f; }
    additive_cr *= race_info->cr_modifier;

    LOG_F(INFO, "additive_cr after race: {}", additive_cr);

    if (additive_cr < 1.5) {
        if (additive_cr > 0.75f) {
            additive_cr -= 0.35;
        } else {
            additive_cr -= 0.25;
        }
    }

    LOG_F(INFO, "additive_cr after adjustment: {}", additive_cr);

    additive_cr += obj->cr_adjust;

    if (additive_cr > 0.75) {
        return round(additive_cr);
    } else {
        int denom = 1;
        float min;
        auto fractionalcr_2da = nw::kernel::twodas().get("fractionalcr");
        ENSURE_OR_RETURN_ZERO(fractionalcr_2da, "[nwn1] calculate_challenge_rating unable to load fractionalcr.2da");

        for (size_t i = 0; i < fractionalcr_2da->rows(); ++i) {
            LOG_F(INFO, "additive cr: {}, row: {}", additive_cr, i);
            if (fractionalcr_2da->get_to(i, "Min", min) && min < additive_cr) {
                if (fractionalcr_2da->get_to(i, "Denominator", denom) && denom != 0) {
                    return 1.0f / denom;
                }
            }
        }
    }

    return 0.0f;
}

// == Object: Saves ============================================================
// =============================================================================

static bool resolve_saving_throw(const nw::ObjectBase* obj, nw::Save type, int dc,
    nw::SaveVersus type_vs, const nw::ObjectBase* versus)
{
    if (!obj) { return false; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(bridge::make_object_arg(obj->handle()));
    args.push_back(nw::smalls::Value::make_int(*type));
    args.push_back(nw::smalls::Value::make_int(dc));
    args.push_back(nw::smalls::Value::make_int(*type_vs));
    args.push_back(bridge::make_nullable_object_arg(versus));
    if (auto result = bridge::call_nwn1_module_bool("nwn1.saving_throws", "resolve_saving_throw", args)) {
        return *result;
    }

    return false;
}

// ============================================================================
// == Creature ================================================================
// ============================================================================

// == Creature: Abilities =====================================================
// ============================================================================

int get_ability_modifier(const nw::Creature* obj, nw::Ability ability, bool base)
{
    if (!obj || ability == nw::Ability::invalid()) { return 0; }
    return (get_ability_score(obj, ability, base) - 10) / 2;
}

// == Creature: Casting =======================================================
// ===========================================================================

bool add_known_spell(nw::Creature* obj, nw::Class class_, nw::Spell spell)
{
    ENSURE_OR_RETURN_FALSE(obj, "[nwn1] add_known_spell called with invalid object");

    auto cls = nw::kernel::rules().classes.get(class_);
    ENSURE_OR_RETURN_FALSE(cls, "[nwn1] add_known_spell called with invalid class '{}'", *class_);
    ENSURE_OR_RETURN_FALSE(cls->spellcaster, "[nwn1] add_known_spell called with non-spellcaster class '{}'", *class_);

    int class_level = obj->levels.level_by_class(class_);

    if (!obj || spell == nw::Spell::invalid() || class_level == 0) {
        return false;
    }

    auto spell_level = nw::kernel::rules().classes.get_spell_level(class_, spell);
    if (spell_level == -1) { return false; }
    auto sb = obj->levels.spells(class_);
    if (!sb) { return false; }

    if (!cls->memorizes_spells) {
        int can_know = cls->spells_known[(class_level - 1) * nw::kernel::rules().maximum_spell_levels() + spell_level];
        int do_know = sb->get_known_spell_count(spell_level);
        if (do_know >= can_know) { return false; }
    }

    return sb->add_known_spell(spell_level, spell);
}

bool add_memorized_spell(nw::Creature* obj, nw::Class class_, nw::Spell spell, nw::MetaMagicFlag meta)
{
    ENSURE_OR_RETURN_FALSE(obj, "[nwn1] add_memorized_spell called with invalid object");

    auto cls = nw::kernel::rules().classes.get(class_);
    ENSURE_OR_RETURN_FALSE(cls, "[nwn1] add_memorized_spell called with invalid class '{}'", *class_);
    ENSURE_OR_RETURN_FALSE(cls->spellcaster, "[nwn1] add_memorized_spell called with non-spellcaster class '{}'", *class_);
    ENSURE_OR_RETURN_FALSE(cls->memorizes_spells, "[nwn1] add_memorized_spell called with spellcaster class '{}' that does not memorize spells", *class_);

    if (!obj) { return false; }
    if (meta == nw::metamagic_any) { meta = nw::metamagic_none; }

    auto spell_level = nw::kernel::rules().classes.get_spell_level(class_, spell);

    if (meta != nw::metamagic_none) {
        auto meta_info = nw::kernel::rules().metamagic.get(metamagic_flag_to_idx_internal(meta));
        spell_level += meta_info->level_adjustment;
    }

    auto sb = obj->levels.spells(class_);
    if (!sb || sb->available_slots(spell_level) == 0) { return false; }
    sb->add_memorized_spell(spell_level, {spell, meta, nw::SpellFlags::readied});
    return true;
}

int compute_total_spell_slots(const nw::Creature* obj, nw::Class class_, int spell_level)
{
    ENSURE_OR_RETURN_FALSE(obj, "[nwn1] compute_total_spell_slots called with invalid object");

    auto cls = nw::kernel::rules().classes.get(class_);
    ENSURE_OR_RETURN_ZERO(cls, "[nwn1] compute_total_spell_slots called with invalid class '{}'", *class_);
    ENSURE_OR_RETURN_ZERO(cls->spellcaster, "[nwn1] compute_total_spell_slots called with non-spellcaster class '{}'", *class_);

    auto cls_level = get_caster_level(obj, class_);

    if (!obj || spell_level < 0 || spell_level >= nw::kernel::rules().maximum_spell_levels()
        || cls_level <= 0) {
        return 0;
    }

    auto sb = obj->levels.spells(class_);
    ENSURE_OR_RETURN_ZERO(sb, "[nwn1] compute_total_spell_slots failed to find spellbook for class '{}'", *class_);

    int result = 0;
    result = cls->spells_gained[((cls_level - 1) * nw::kernel::rules().maximum_spell_levels()) + spell_level];
    if (result > 0) {
        if (spell_level <= get_ability_modifier(obj, cls->caster_ability)) {
            ++result;
        }

        auto it = nw::find_first_effect_of(obj->effects().begin(), obj->effects().end(),
            effect_type_bonus_spell_of_level, *class_);

        while (it != obj->effects().end()) {
            if (it->type == effect_type_bonus_spell_of_level && it->subtype == *class_
                && it->effect->get_int(0) == spell_level) {
                ++result;
            } else {
                break;
            }
            ++it;
        }
    }
    return result;
}

int compute_total_spells_knowable(const nw::Creature* obj, nw::Class class_, int spell_level)
{
    ENSURE_OR_RETURN_ZERO(obj, "[nwn1] compute_total_spells_knowable called with invalid object");

    auto cls = nw::kernel::rules().classes.get(class_);
    ENSURE_OR_RETURN_ZERO(cls, "[nwn1] compute_total_spell_slots called with invalid class '{}'", *class_);
    ENSURE_OR_RETURN_ZERO(cls->spellcaster, "[nwn1] compute_total_spell_slots called with non-spellcaster class '{}'", *class_);

    auto cls_level = get_caster_level(obj, class_);
    return cls->spells_known[((cls_level - 1) * nw::kernel::rules().maximum_spell_levels()) + spell_level];
}

int get_available_spell_slots(const nw::Creature* obj, nw::Class class_, int spell_level)
{
    ENSURE_OR_RETURN_ZERO(obj, "[nwn1] get_available_spell_slots called with invalid object");

    auto cls = nw::kernel::rules().classes.get(class_);
    ENSURE_OR_RETURN_ZERO(cls, "[nwn1] get_available_spell_slots called with invalid class '{}'", *class_);
    ENSURE_OR_RETURN_ZERO(cls->spellcaster, "[nwn1] get_available_spell_slots called with non-spellcaster class '{}'", *class_);

    // For classes that don't memorize spells, we don't need to consult the spellbook.
    if (!cls->memorizes_spells) {
        return compute_total_spell_slots(obj, class_, spell_level);
    }

    auto sb = obj->levels.spells(class_);
    if (!sb) { return 0; }
    return sb->available_slots(spell_level);
}

int get_available_spell_uses(const nw::Creature* obj, nw::Class class_, nw::Spell spell, int min_spell_level, nw::MetaMagicFlag meta)
{
    ENSURE_OR_RETURN_ZERO(obj, "[nwn1] get_available_spell_uses called with invalid object");

    auto cls = nw::kernel::rules().classes.get(class_);
    ENSURE_OR_RETURN_ZERO(cls, "[nwn1] get_available_spell_uses called with invalid class '{}'", *class_);
    ENSURE_OR_RETURN_ZERO(cls->spellcaster, "[nwn1] get_available_spell_uses called with non-spellcaster class '{}'", *class_);

    int result = 0;
    auto sb = obj->levels.spells(class_);

    if (!obj || !sb || spell == nw::Spell::invalid() || min_spell_level < 0) {
        return result;
    }

    for (size_t i = min_spell_level; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
        for (const auto& entry : sb->memorized_[i]) {
            if (entry.spell == spell && (meta == nw::metamagic_any || entry.meta == meta)) {
                ++result;
            }
        }
    }

    return result;
}

int get_caster_level(const nw::Creature* obj, nw::Class class_)
{
    ENSURE_OR_RETURN_ZERO(obj, "[nwn1] get_caster_level called with invalid object");

    auto main_class_info = nw::kernel::rules().classes.get(class_);
    ENSURE_OR_RETURN_ZERO(main_class_info, "[nwn1] get_caster_level called with invalid class '{}'", *class_);
    ENSURE_OR_RETURN_ZERO(main_class_info->spellcaster, "[nwn1] get_caster_level called with non-spellcaster class '{}'", *class_);

    int result = obj->levels.level_by_class(class_);
    if (result == 0) { return 0; }

    for (const auto& cls : obj->levels.entries) {
        if (cls.id == class_) { continue; }

        auto class_info = nw::kernel::rules().classes.get(cls.id);
        if (!class_info) { break; }

        if (main_class_info->arcane && class_info->arcane_spellgain_mod > 0) {
            result += cls.level / class_info->arcane_spellgain_mod;
        } else if (!main_class_info->arcane && class_info->divine_spellgain_mod > 0) {
            result += cls.level / class_info->divine_spellgain_mod;
        }
    }

    return result;
}

bool get_knows_spell(const nw::Creature* obj, nw::Class class_, nw::Spell spell)
{
    ENSURE_OR_RETURN_FALSE(obj, "[nwn1] get_knows_spell called with invalid object");

    auto cls = nw::kernel::rules().classes.get(class_);
    ENSURE_OR_RETURN_FALSE(cls, "[nwn1] get_knows_spell called with invalid class '{}'", *class_);
    ENSURE_OR_RETURN_FALSE(cls->spellcaster, "[nwn1] get_knows_spell called with non-spellcaster class '{}'", *class_);

    auto spell_level = nw::kernel::rules().classes.get_spell_level(class_, spell);

    // If spellbook isn't restricted then caster knows all class spells.
    if (!cls->spellbook_restricted) { return spell_level != -1; }

    auto sb = obj->levels.spells(class_);
    ENSURE_OR_RETURN_FALSE(sb, "[nwn1] get_knows_spell failed to find spellbook for class '{}'", *class_);
    return sb->knows_spell(spell, spell_level);
}

void recompute_all_availabe_spell_slots(nw::Creature* obj)
{
    ENSURE_OR_RETURN(obj, "[nwn1] recompute_all_availabe_spell_slots called with invalid object");

    for (auto& cls : obj->levels.entries) {
        auto cls_info = nw::kernel::rules().classes.get(cls.id);
        if (!cls_info) { break; }
        if (!cls_info->spellcaster || !cls_info->memorizes_spells) { continue; }
        for (int i = 0; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
            cls.spells.set_available_slots(i, compute_total_spell_slots(obj, cls.id, i));
        }
    }
}

void remove_known_spell(nw::Creature* obj, nw::Class class_, nw::Spell spell)
{
    ENSURE_OR_RETURN(obj, "[nwn1] remove_known_spell called with invalid object");

    if (class_ == nw::Class::invalid() || spell == nw::Spell::invalid()) {
        return;
    }

    auto spell_level = nw::kernel::rules().classes.get_spell_level(class_, spell);
    if (spell_level == -1) { return; }
    auto sb = obj->levels.spells(class_);
    if (!sb) { return; }
    sb->remove_known_spell(spell_level, spell);
    for (int i = spell_level; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
        // Metamagic
        sb->clear_memorized_spell(i, spell);
    }
}

// == Creature: Classes =======================================================
// ============================================================================

// == Creature: Combat ========================================================
// ============================================================================

int base_attack_bonus(const nw::Creature* obj)
{
    if (!obj) { return 0; }

    size_t result = 0;
    const auto& classes = nw::kernel::rules().classes;

    size_t levels = obj->levels.level();
    size_t epic = 0;
    if (levels >= 20) {
        // Not sure what account the game takes of the attack bonus tables when epic.
        // pretty sure it's none.
        epic = (levels - 20) / 2;
        levels = 20;
    }

    std::array<int, nw::LevelStats::max_classes> class_levels{};

    if (obj->pc) {
        for (size_t i = 0; i < levels; ++i) {
            ++class_levels[obj->levels.position(obj->history.entries[i].class_)];
        }

        for (size_t i = 0; i < nw::LevelStats::max_classes; ++i) {
            if (class_levels[i] == 0) { break; }
            auto cl = obj->levels.entries[i].id;
            result += classes.get_base_attack_bonus(cl, class_levels[i]);
        }
    } else {
        for (const auto& cl : obj->levels.entries) {
            if (levels == 0 || cl.id == nw::Class::invalid()) { break; }
            auto count = std::min(levels, size_t(cl.level));
            result += classes.get_base_attack_bonus(cl.id, count);
            levels -= count;
        }
    }
    return int(result + epic);
}

// // == Items ===================================================================
// // ============================================================================

int calculate_item_value(const nw::Item* item)
{
    ENSURE_OR_RETURN_ZERO(item, "[nwn1] calculate_item_value called with invalid object");
    float result = 0.0f;
    auto bi_info = nw::kernel::rules().baseitems.get(item->baseitem);
    if (!bi_info) { return result; }

    float base_value = 0.0f;

    if (item->baseitem == base_item_armor) {
        auto armor_2da = nw::kernel::twodas().get("armor");
        if (!armor_2da || !armor_2da->get_to(item->armor_id, "Cost", base_value)) {
            return 0;
        }
    } else {
        base_value = bi_info->base_cost;
    }

    float positive = 0.0f;
    float negative = 0.0f;

    std::vector<float> spell_values;

    for (const auto& ip : item->properties) {
        auto ipdef = nw::kernel::effects().ip_definition(nw::ItemPropertyType::make(ip.type));
        float property_value = ipdef->cost;

        float subtype_value = 0.0f;
        if (ipdef->subtype) {
            if (ip.subtype < ipdef->subtype->rows()) {
                ipdef->subtype->get_to(ip.subtype, "Cost", subtype_value, false);
            }
        }

        if (property_value == 0.0f) {
            property_value = subtype_value;
        }

        float cost_value = 0.0f;
        if (ipdef->cost_table) {
            if (ip.cost_value < ipdef->cost_table->rows()) {
                ipdef->cost_table->get_to(ip.cost_value, "Cost", cost_value);
            }
        }

        if (cost_value != 0.0f) {
            property_value *= cost_value;
        }

        if (ip.type == *ip_cast_spell) {
            spell_values.push_back(property_value);
        } else {
            if (property_value > 0.0f) {
                positive += property_value;
            } else {
                negative += property_value;
            }
        }
    }

    float total_spell_value = 0.0f;
    if (spell_values.size()) {
        std::sort(std::begin(spell_values), std::end(spell_values), std::greater<float>{});

        for (float value : spell_values) {
            total_spell_value += value * 0.5;
        }

        if (spell_values.size() >= 1) {
            total_spell_value += spell_values[0] * 0.5f;
        }

        if (spell_values.size() >= 2) {
            total_spell_value += spell_values[1] * 0.25f;
        }
    }

    // [BaseCost + 1000*(Multiplier^2 - NegMultiplier^2) + SpellCosts]*MaxStack*BaseMult + AdditionalCost
    result = base_value;
    result += 1000 * ((positive * positive) - (negative * negative)) + total_spell_value;
    result *= bi_info->stack_limit;
    result *= bi_info->cost_multiplier;
    result += item->additional_cost;
    return int(std::round(result));
}

} // namespace nwn1
