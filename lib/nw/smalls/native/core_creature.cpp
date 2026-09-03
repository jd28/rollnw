#include "../runtime.hpp"

#include "../../kernel/Rules.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../profiles/nwn1/body_part_catalog.hpp"
#include "../../resources/ResourceManager.hpp"
#include "../../rules/creature_body_parts.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nw::smalls {

namespace {

struct ScriptAppearanceInfo {
    int32_t id = -1;
    ScriptString label;
    int32_t string_ref = -1;
    ScriptString base_name;
    nw::Resref model;
    int32_t model_type = -1;
    int32_t model_flags = 0;
    float wing_tail_scale = 1.0f;
    float helmet_scale_m = 1.0f;
    float helmet_scale_f = 1.0f;
    float weapon_scale = -1.0f;
    float personal_space = -1.0f;
    bool has_arms = false;
};

struct ScriptCreatureAccessoryModelInfo {
    int32_t id = -1;
    ScriptString label;
    nw::Resref model;
};

ScriptAppearanceInfo appearance_info(int32_t id)
{
    auto& rt = nw::kernel::runtime();
    const auto* info = nw::kernel::rules().appearances.get(nw::Appearance::make(id));
    if (!info) { return {}; }
    return {
        .id = id,
        .label = ScriptString{rt.alloc_string(info->label)},
        .string_ref = static_cast<int32_t>(info->string_ref),
        .base_name = ScriptString{rt.alloc_string(info->base_name)},
        .model = info->model,
        .model_type = static_cast<int32_t>(info->model_type),
        .model_flags = static_cast<int32_t>(info->model_flags),
        .wing_tail_scale = info->wing_tail_scale,
        .helmet_scale_m = info->helmet_scale_m,
        .helmet_scale_f = info->helmet_scale_f,
        .weapon_scale = info->weapon_scale,
        .personal_space = info->personal_space,
        .has_arms = info->has_arms,
    };
}

bool publish_appearance_info(Value value)
{
    auto& rt = nw::kernel::runtime();
    const TypeID info_type = rt.type_id("core.creature.AppearanceInfo");
    auto* array = rt.get_array_typed(value.data.hptr);
    if (!array || info_type == invalid_type_id
        || array->element_type() != info_type) {
        return false;
    }

    auto& rules = nw::kernel::rules();
    nw::AppearanceArray next{rules.allocator()};
    next.entries.reserve(array->size());
    if (array->size() == 0) {
        rules.appearances = std::move(next);
        return true;
    }

    size_t valid_count = 0;
    for (size_t index = 0; index < array->size(); ++index) {
        Value element;
        if (!array->get_value(index, element, rt)
            || element.type_id != info_type) {
            return false;
        }
        const auto source = detail::value_cast<ScriptAppearanceInfo>(&rt, element);
        const auto label = source.label.view(rt);
        if (source.id == -1) {
            next.entries.emplace_back();
            continue;
        }
        if (source.id != static_cast<int32_t>(index) || label.empty()
            || source.string_ref < -1
            || source.model_type < static_cast<int32_t>(nw::AppearanceModelType::parts)
            || source.model_type > static_cast<int32_t>(nw::AppearanceModelType::limited)
            || source.model_flags < 0
            || source.model_flags > static_cast<int32_t>(nw::appearance_model_flags_all)
            || (source.model_type == static_cast<int32_t>(nw::AppearanceModelType::parts)
                && source.model_flags != static_cast<int32_t>(nw::appearance_model_flags_all))
            || !std::isfinite(source.wing_tail_scale)
            || !std::isfinite(source.helmet_scale_m)
            || !std::isfinite(source.helmet_scale_f)
            || !std::isfinite(source.weapon_scale)
            || !std::isfinite(source.personal_space)
            || source.personal_space < -1.0f) {
            return false;
        }
        next.entries.push_back({
            .label = nw::String{label},
            .string_ref = static_cast<uint32_t>(source.string_ref),
            .base_name = nw::String{source.base_name.view(rt)},
            .model = source.model,
            .model_type = static_cast<nw::AppearanceModelType>(source.model_type),
            .model_flags = static_cast<nw::AppearanceModelFlags>(source.model_flags),
            .wing_tail_scale = source.wing_tail_scale,
            .helmet_scale_m = source.helmet_scale_m,
            .helmet_scale_f = source.helmet_scale_f,
            .weapon_scale = source.weapon_scale,
            .personal_space = source.personal_space,
            .has_arms = source.has_arms,
        });
        ++valid_count;
    }
    if (valid_count == 0) { return false; }
    rules.appearances = std::move(next);
    return true;
}

template <typename Array>
bool publish_accessory_model_info_impl(
    Runtime& rt, Value value, Array& destination)
{
    const TypeID info_type = rt.type_id(
        "core.creature.CreatureAccessoryModelInfo");
    auto* array = rt.get_array_typed(value.data.hptr);
    if (!array || info_type == invalid_type_id
        || array->element_type() != info_type) {
        return false;
    }

    Array next{nw::kernel::rules().allocator()};
    next.entries.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        Value element;
        if (!array->get_value(index, element, rt)
            || element.type_id != info_type) {
            return false;
        }
        const auto source = detail::value_cast<ScriptCreatureAccessoryModelInfo>(
            &rt, element);
        if (source.id == -1) {
            next.entries.emplace_back();
            continue;
        }
        if (source.id != static_cast<int32_t>(index)) {
            return false;
        }
        nw::CreatureAccessoryModelInfo target;
        target.label = source.label.view(rt);
        target.model = source.model;
        next.entries.push_back(std::move(target));
    }
    destination = std::move(next);
    return true;
}

bool publish_accessory_model_info(int32_t accessory, Value value)
{
    auto& rt = nw::kernel::runtime();
    if (accessory == 0) {
        return publish_accessory_model_info_impl(
            rt, value, nw::kernel::rules().wingmodels);
    }
    if (accessory == 1) {
        return publish_accessory_model_info_impl(
            rt, value, nw::kernel::rules().tailmodels);
    }
    return false;
}

bool publish_body_part_catalog(Value value)
{
    auto& rt = nw::kernel::runtime();
    auto* array = rt.get_array_typed(value.data.hptr);
    if (!array || array->element_type() != rt.int_type()) {
        return false;
    }

    nw::Vector<int32_t> fallbacks;
    fallbacks.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        Value element;
        if (!array->get_value(index, element, rt)
            || element.type_id != rt.int_type()) {
            return false;
        }
        fallbacks.push_back(element.data.ival);
    }

    nw::String diagnostic;
    if (!nwn1::build_body_part_catalog(nw::kernel::rules().appearances,
            fallbacks, nw::kernel::resman(),
            nw::kernel::rules().creature_body_parts, diagnostic)) {
        LOG_F(ERROR, "rules: failed to publish creature body-part catalog: {}",
            diagnostic);
        return false;
    }
    return true;
}

const nw::CreatureAccessoryModelInfo* creature_accessory_info(
    int32_t accessory, int32_t id)
{
    if (id < 0) { return nullptr; }
    if (accessory == 0) {
        return nw::kernel::rules().wingmodels.get(nw::WingModel::make(id));
    }
    if (accessory == 1) {
        return nw::kernel::rules().tailmodels.get(nw::TailModel::make(id));
    }
    return nullptr;
}

// Raw resource fact access. Accessory kinds are the persisted two-element
// CreatureAppearance order (wings, tail); profile SmallS owns the policy that
// determines whether a fact is legal for a particular creature.
nw::Resref accessory_model(int32_t accessory, int32_t id)
{
    const auto* info = creature_accessory_info(accessory, id);
    return info ? info->model : nw::Resref{};
}

bool accessory_exists(int32_t accessory, int32_t id)
{
    return creature_accessory_info(accessory, id) != nullptr;
}

int32_t body_part_assembly(
    int32_t appearance,
    int32_t gender,
    int32_t phenotype,
    int32_t fallback_phenotype)
{
    return nwn1::body_part_assembly_id(
        appearance, gender, phenotype, fallback_phenotype);
}

int32_t body_part_count(int32_t assembly)
{
    const auto count = nw::kernel::rules().creature_body_parts.parts(assembly).size();
    return count <= static_cast<size_t>(std::numeric_limits<int32_t>::max())
        ? static_cast<int32_t>(count)
        : 0;
}

const nw::CreatureBodyPartInfo* body_part_at(int32_t assembly, int32_t index)
{
    const auto parts = nw::kernel::rules().creature_body_parts.parts(assembly);
    return index >= 0 && static_cast<size_t>(index) < parts.size()
        ? &parts[static_cast<size_t>(index)]
        : nullptr;
}

int32_t body_part_id_at(int32_t assembly, int32_t index)
{
    const auto* part = body_part_at(assembly, index);
    return part ? part->part_id : -1;
}

int32_t body_part_mirror(int32_t assembly, int32_t part_id)
{
    const auto* part = nw::kernel::rules().creature_body_parts.part(
        assembly, part_id);
    return part ? part->mirror_part_id : -1;
}

int32_t body_part_flags(int32_t assembly, int32_t part_id)
{
    const auto* part = nw::kernel::rules().creature_body_parts.part(
        assembly, part_id);
    return part ? static_cast<int32_t>(part->flags) : 0;
}

ScriptString body_part_label(int32_t assembly, int32_t part_id)
{
    auto& rt = nw::kernel::runtime();
    const auto* part = nw::kernel::rules().creature_body_parts.part(
        assembly, part_id);
    return ScriptString{rt.alloc_string(
        part ? nw::StringView{part->label} : nw::StringView{})};
}

nw::Resref body_part_anchor(int32_t assembly, int32_t part_id)
{
    const auto* part = nw::kernel::rules().creature_body_parts.part(
        assembly, part_id);
    return part ? part->anchor : nw::Resref{};
}

int32_t body_part_option_count(int32_t assembly, int32_t part_id)
{
    const auto count = nw::kernel::rules().creature_body_parts.options(
                                                                  assembly, part_id)
                           .size();
    return count <= static_cast<size_t>(std::numeric_limits<int32_t>::max())
        ? static_cast<int32_t>(count)
        : 0;
}

const nw::CreatureBodyPartOption* body_part_option_at(
    int32_t assembly, int32_t part_id, int32_t index)
{
    const auto options = nw::kernel::rules().creature_body_parts.options(
        assembly, part_id);
    return index >= 0 && static_cast<size_t>(index) < options.size()
        ? &options[static_cast<size_t>(index)]
        : nullptr;
}

int32_t body_part_option_id_at(
    int32_t assembly, int32_t part_id, int32_t index)
{
    const auto* option = body_part_option_at(assembly, part_id, index);
    return option ? option->option_id : -1;
}

int32_t body_part_option_flags_at(
    int32_t assembly, int32_t part_id, int32_t index)
{
    const auto* option = body_part_option_at(assembly, part_id, index);
    return option ? static_cast<int32_t>(option->flags) : 0;
}

bool body_part_option_exists(
    int32_t assembly, int32_t part_id, int32_t option_id)
{
    return nw::kernel::rules().creature_body_parts.option(
               assembly, part_id, option_id)
        != nullptr;
}

nw::Resref body_part_model(
    int32_t assembly, int32_t part_id, int32_t option_id, bool prefer_mirror)
{
    const auto& catalog = nw::kernel::rules().creature_body_parts;
    if (prefer_mirror) {
        const auto* part = catalog.part(assembly, part_id);
        if (part && part->mirror_part_id >= 0) {
            const auto* mirrored = catalog.option(
                assembly, part->mirror_part_id, option_id);
            if (mirrored && !mirrored->model.empty()) { return mirrored->model; }
        }
    }
    const auto* option = catalog.option(assembly, part_id, option_id);
    return option ? option->model : nw::Resref{};
}

nw::Creature* as_creature(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_creature();
}

const nw::ObjectAbilityLoadoutEntry* ability_loadout_entry(nw::ObjectHandle obj, int32_t index)
{
    if (index < 0) { return nullptr; }
    auto* row = nw::kernel::objects().components().find_ability_loadout(obj);
    if (!row || static_cast<size_t>(index) >= row->entries.size()) { return nullptr; }
    return &row->entries[static_cast<size_t>(index)];
}

} // namespace

void register_core_creature(Runtime& rt)
{
    if (rt.get_native_module("core.creature")) {
        return;
    }

    rt.module("core.creature")
        .native_struct<ScriptAppearanceInfo>("AppearanceInfo")
        .field("id", &ScriptAppearanceInfo::id)
        .field("label", &ScriptAppearanceInfo::label)
        .field("string_ref", &ScriptAppearanceInfo::string_ref)
        .field("base_name", &ScriptAppearanceInfo::base_name)
        .field("model", &ScriptAppearanceInfo::model)
        .field("model_type", &ScriptAppearanceInfo::model_type)
        .field("model_flags", &ScriptAppearanceInfo::model_flags)
        .field("wing_tail_scale", &ScriptAppearanceInfo::wing_tail_scale)
        .field("helmet_scale_m", &ScriptAppearanceInfo::helmet_scale_m)
        .field("helmet_scale_f", &ScriptAppearanceInfo::helmet_scale_f)
        .field("weapon_scale", &ScriptAppearanceInfo::weapon_scale)
        .field("personal_space", &ScriptAppearanceInfo::personal_space)
        .field("has_arms", &ScriptAppearanceInfo::has_arms)
        .end_struct()
        .native_struct<ScriptCreatureAccessoryModelInfo>("CreatureAccessoryModelInfo")
        .field("id", &ScriptCreatureAccessoryModelInfo::id)
        .field("label", &ScriptCreatureAccessoryModelInfo::label)
        .field("model", &ScriptCreatureAccessoryModelInfo::model)
        .end_struct()
        .function("appearance_info", &appearance_info)
        .function("publish_appearance_info", &publish_appearance_info)
        .function("publish_accessory_model_info", &publish_accessory_model_info)
        .function("publish_body_part_catalog", &publish_body_part_catalog)
        .function("publish_skill_count", +[](int32_t count) -> bool { return nw::kernel::rules().publish_skill_count(count); })
        .function("accessory_model", &accessory_model)
        .function("accessory_exists", &accessory_exists)
        .function("body_part_assembly", &body_part_assembly)
        .function("body_part_count", &body_part_count)
        .function("body_part_id_at", &body_part_id_at)
        .function("body_part_mirror", &body_part_mirror)
        .function("body_part_flags", &body_part_flags)
        .function("body_part_label", &body_part_label)
        .function("body_part_anchor", &body_part_anchor)
        .function("body_part_option_count", &body_part_option_count)
        .function("body_part_option_id_at", &body_part_option_id_at)
        .function("body_part_option_flags_at", &body_part_option_flags_at)
        .function("body_part_option_exists", &body_part_option_exists)
        .function("body_part_model", &body_part_model)
        .function("set_vitals", +[](nw::ObjectHandle obj, int32_t hp_current, int32_t hp_max) -> bool {
            if (!as_creature(obj)) { return false; }
            return nw::kernel::objects().components().set_vitals(obj, hp_current, hp_max); })
        .function("set_movement_rate", +[](nw::ObjectHandle obj, float value) -> bool {
            if (!as_creature(obj)) { return false; }
            return nw::kernel::objects().components().set_movement_rate(obj, value); })
        .function("get_vitals_hp_current", +[](nw::ObjectHandle obj) -> int32_t {
            auto* row = nw::kernel::objects().components().find_vitals(obj);
            return row ? row->hp_current : 0; })
        .function("get_vitals_hp_max", +[](nw::ObjectHandle obj) -> int32_t {
            auto* row = nw::kernel::objects().components().find_vitals(obj);
            return row ? row->hp_max : 0; })
        .function("get_ability_loadout_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* row = nw::kernel::objects().components().find_ability_loadout(obj);
            if (!row || row->entries.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
                return 0;
            }
            return static_cast<int32_t>(row->entries.size()); })
        .function("get_ability_loadout_ability", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* entry = ability_loadout_entry(obj, index);
            return entry ? entry->ability : -1; })
        .function("get_ability_loadout_source", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* entry = ability_loadout_entry(obj, index);
            return entry ? entry->source : -1; })
        .function("get_ability_loadout_tier", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* entry = ability_loadout_entry(obj, index);
            return entry ? entry->tier : -1; })
        .function("get_ability_loadout_slot", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* entry = ability_loadout_entry(obj, index);
            return entry ? entry->slot : -1; })
        .function("get_ability_loadout_modifier", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* entry = ability_loadout_entry(obj, index);
            return entry ? entry->modifier : -1; })
        .function("add_unslotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t ability) -> bool {
            if (!as_creature(obj)) { return false; }
            return nw::kernel::objects().components().add_unslotted_ability(obj, source, tier, ability); })
        .function("remove_unslotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t ability) -> bool {
            if (!as_creature(obj)) { return false; }
            nw::kernel::objects().components().remove_unslotted_ability(obj, source, tier, ability);
            return true; })
        .function("set_slotted_ability_count", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t count) -> bool {
            if (!as_creature(obj) || count < 0) { return false; }
            return nw::kernel::objects().components().set_slotted_ability_count(
                obj, source, tier, static_cast<size_t>(count)); })
        .function("available_ability_slots", +[](nw::ObjectHandle obj, int32_t source, int32_t tier) -> int32_t {
            if (!as_creature(obj)) { return 0; }
            return nw::kernel::objects().components().available_ability_slots(obj, source, tier); })
        .function("add_slotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t ability, int32_t modifier, int32_t flags) -> bool {
            if (!as_creature(obj) || flags < 0) { return false; }
            return nw::kernel::objects().components().add_slotted_ability(
                obj, source, tier, ability, modifier, static_cast<uint32_t>(flags)); })
        .function("set_slotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t slot, int32_t ability, int32_t modifier, int32_t flags) -> bool {
            if (!as_creature(obj) || flags < 0) { return false; }
            return nw::kernel::objects().components().set_slotted_ability(
                obj, source, tier, slot, ability, modifier, static_cast<uint32_t>(flags)); })
        .function("clear_slotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t slot, int32_t ability, int32_t modifier, int32_t flags) -> bool {
            if (!as_creature(obj) || flags < 0) { return false; }
            auto& components = nw::kernel::objects().components();
            const auto* loadout = components.find_ability_loadout(obj);
            if (!loadout) { return false; }
            const auto entry = std::find_if(loadout->entries.begin(), loadout->entries.end(),
                [=](const auto& candidate) {
                    return candidate.source == source && candidate.tier == tier
                        && candidate.slot == slot && candidate.ability == ability
                        && candidate.modifier == modifier
                        && candidate.flags == static_cast<uint32_t>(flags);
                });
            if (entry == loadout->entries.end()) { return false; }
            components.clear_slotted_ability(obj, source, tier, slot);
            return true; })
        .function("remove_slotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t ability, int32_t modifier) -> bool {
            if (!as_creature(obj)) { return false; }
            auto& components = nw::kernel::objects().components();
            const int32_t slot = components.find_slotted_ability_slot(
                obj, source, tier, ability, modifier);
            if (slot < 0) { return false; }
            components.clear_slotted_ability(obj, source, tier, slot);
            return true; })
        .function("clear_slotted_ability_from_tier", +[](nw::ObjectHandle obj, int32_t source, int32_t min_tier, int32_t ability) -> bool {
            if (!as_creature(obj)) { return false; }
            nw::kernel::objects().components().clear_slotted_ability_from_tier(obj, source, min_tier, ability);
            return true; })
        // The end.
        .finalize();
}

} // namespace nw::smalls
