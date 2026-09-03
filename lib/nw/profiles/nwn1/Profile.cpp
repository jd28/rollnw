#include "Profile.hpp"

#include "constants.hpp"
#include "rules.hpp"
#include "scriptbridge.hpp"

#include "../../formats/StaticTwoDA.hpp"
#include "../../kernel/Kernel.hpp"
#include "../../kernel/Rules.hpp"
#include "../../kernel/TwoDACache.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../resources/ResourceManager.hpp"
#include "../../rules/combat_scheduler.hpp"
#include "../../rules/feats.hpp"
#include "../../rules/system.hpp"
#include "../../smalls/Smalls.hpp"
#include "../../smalls/data_spec.hpp"
#include "../../smalls/runtime.hpp"
#include "../../util/profile.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace nwk = nw::kernel;

using namespace std::literals;

namespace nwn1 {

namespace {

void register_structured_data_specs()
{
    namespace fs = std::filesystem;
    const auto& package_directory = nw::kernel::runtime().select_package_directory(
        *nw::kernel::config().profile());
    const auto spec_directory = package_directory / "data_specs";
    std::error_code error;
    if (!fs::is_directory(spec_directory, error) || error) {
        throw std::runtime_error(fmt::format(
            "rules: package data-spec directory '{}' was not found",
            spec_directory.string()));
    }

    nw::Vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(spec_directory, error)) {
        if (error) { break; }
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            paths.push_back(entry.path());
        }
    }
    if (error) {
        throw std::runtime_error(fmt::format(
            "rules: failed to enumerate package data specs '{}': {}",
            spec_directory.string(), error.message()));
    }
    std::ranges::sort(paths);

    for (const auto& path : paths) {
        nw::Vector<nw::smalls::DataSpec> specs;
        nw::Vector<nw::smalls::DataDiagnostic> diagnostics;
        if (!nw::smalls::parse_data_specs(
                std::span<const fs::path>{&path, 1}, specs, diagnostics)) {
            const auto detail = diagnostics.empty()
                ? nw::String{"unknown parse error"}
                : fmt::format("{}: {} (target '{}')",
                      diagnostics.front().source.string(),
                      diagnostics.front().message,
                      diagnostics.front().target);
            LOG_F(ERROR,
                "rules: failed to parse structured data spec '{}'; disabling only that domain: {}",
                path.string(), detail);
            continue;
        }

        if (specs.size() != 1) {
            LOG_F(ERROR,
                "rules: data spec '{}' produced {} domains; disabling that file",
                path.string(), specs.size());
            continue;
        }
        nw::String diagnostic;
        if (!nw::kernel::runtime().register_data_spec(
                std::move(specs.front()), diagnostic)) {
            LOG_F(ERROR,
                "rules: failed to register data spec '{}'; disabling only that domain: {}",
                path.string(), diagnostic);
        }
    }
}

void update_placeable_visual(nw::smalls::Runtime& rt, nw::ObjectBase* obj)
{
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, obj->handle()));
    auto result = rt.execute_script("nwn1.placeables", "update_visual", args);
    if (!result.ok()) {
        LOG_F(WARNING, "nwn1: failed to update placeable visual: {}", result.error_message);
    }
}

void update_door_visual(nw::smalls::Runtime& rt, nw::ObjectBase* obj)
{
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, obj->handle()));
    auto result = rt.execute_script("nwn1.doors", "update_visual", args);
    if (!result.ok()) {
        LOG_F(WARNING, "nwn1: failed to update door visual: {}", result.error_message);
    }
}

void update_creature_visual_equipment(nw::smalls::Runtime& rt, nw::ObjectBase* obj)
{
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, obj->handle()));
    auto result = rt.execute_script("nwn1.item", "update_creature_visual_equipment", args);
    if (!result.ok()) {
        LOG_F(WARNING, "nwn1: failed to update creature equipment visual: {}", result.error_message);
    }
}

void update_creature_visual_body(nw::smalls::Runtime& rt, nw::ObjectBase* obj)
{
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, obj->handle()));
    auto result = rt.execute_script("nwn1.creature", "update_visual", args);
    if (!result.ok()) {
        LOG_F(WARNING, "nwn1: failed to update creature body visual: {}", result.error_message);
    }
}

void initialize_creature_health(nw::smalls::Runtime& rt, nw::ObjectBase* obj)
{
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, obj->handle()));
    auto result = rt.execute_script("nwn1.hitpoints", "initialize_creature_health", args);
    if (!result.ok()) {
        LOG_F(WARNING, "nwn1: failed to initialize creature health: {}", result.error_message);
    }
}

void recompute_creature_available_spell_slots(nw::smalls::Runtime& rt, nw::ObjectBase* obj)
{
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, obj->handle()));
    auto result = rt.execute_script("nwn1.creature", "recompute_all_available_spell_slots", args);
    if (!result.ok()) {
        LOG_F(WARNING, "nwn1: failed to recompute creature available spell slots: {}", result.error_message);
    }
}

void load_baseitem_definitions()
{
    auto& rt = nw::kernel::runtime();
    auto result = rt.execute_script("nwn1.baseitems", "init");
    if (!result.ok()) {
        LOG_F(ERROR,
            "rules: failed to load base-item definitions; continuing without that config domain: {}",
            result.error_message);
        return;
    }
    if (result.value.type_id != rt.bool_type() || !result.value.data.bval) {
        LOG_F(ERROR,
            "rules: invalid base-item definitions; continuing without that config domain");
    }
}

void load_data_definitions(nw::StringView module, nw::StringView label)
{
    auto& rt = nw::kernel::runtime();
    auto result = rt.execute_script(module, "init");
    if (!result.ok()) {
        LOG_F(ERROR,
            "rules: failed to load {} definitions; continuing without that config domain: {}",
            label, result.error_message);
        return;
    }
    if (result.value.type_id != rt.bool_type() || !result.value.data.bval) {
        LOG_F(ERROR,
            "rules: invalid {} definitions; continuing without that config domain",
            label);
    }
}

} // namespace

bool Profile::load_rules() const
{
    LOG_F(INFO, "[nwn1] loading rules...");
    register_structured_data_specs();

    // == Load Rules ==========================================================

    load_qualifier_matcher();

    nw::kernel::objects().set_instantiate_callback([](nw::ObjectBase* obj) {
        NW_PROFILE_SCOPE_N("nwn1::instantiate_callback");
        auto& rt = nw::kernel::runtime();
        {
            NW_PROFILE_SCOPE_N("nwn1::init_object_propsets");
            rt.init_object_propsets(obj->handle());
        }
        switch (obj->handle().type) {
        default:
            break;
        case nw::ObjectType::placeable: {
            NW_PROFILE_SCOPE_N("nwn1::update_placeable_visual");
            update_placeable_visual(rt, obj);
        } break;
        case nw::ObjectType::door: {
            NW_PROFILE_SCOPE_N("nwn1::update_door_visual");
            update_door_visual(rt, obj);
        } break;
        }

        if (obj->handle().type == nw::ObjectType::creature) {
            NW_PROFILE_SCOPE_N("nwn1::creature_post_instantiate");
            auto* cre = obj->as_creature();
            initialize_creature_health(rt, obj);
            recompute_creature_available_spell_slots(rt, obj);
            refresh_combat_weapon_cache(cre);
            update_creature_visual_body(rt, obj);
            update_creature_visual_equipment(rt, obj);
        }
    });

    nw::kernel::objects().set_destroy_callback([](nw::ObjectBase* obj) {
        nw::kernel::runtime().free_object_propsets(obj->handle());
    });

    // == Load 2das ===========================================================

    nw::StaticTwoDA classes{nw::kernel::resman().demand({"classes"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA feat{nw::kernel::resman().demand({"feat"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA racialtypes{nw::kernel::resman().demand({"racialtypes"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA skills{nw::kernel::resman().demand({"skills"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA spells{nw::kernel::resman().demand({"spells"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA spellschools{nw::kernel::resman().demand({"spellschools"sv, nw::ResourceType::twoda})};
    nw::String temp_string;

    load_data_definitions("nwn1.appearances", "appearance");
    load_data_definitions("nwn1.placeables", "placeable");
    load_data_definitions("nwn1.door_types", "door-type");
    load_data_definitions("nwn1.generic_doors", "generic-door");
    load_data_definitions("nwn1.wing_models", "wing-model");
    load_data_definitions("nwn1.tail_models", "tail-model");
    load_baseitem_definitions();
    load_data_definitions("nwn1.phenotypes", "phenotype");

    // Class policy is generated as Smalls config. Keep the load-time check here
    // because the profile still requires classes.2da for nwn1.data.classes.
    if (!classes.is_valid()) {
        throw std::runtime_error("rules: failed to load 'classes.2da'");
    }

    // Feats
    auto& feat_array = nw::kernel::rules().feats;
    if (feat.is_valid()) {
        feat_array.entries.reserve(feat.rows());
        for (size_t i = 0; i < feat.rows(); ++i) {
            const auto& info = feat_array.entries.emplace_back(feat.row(i));
            if (info.constant) {
                feat_array.constant_to_index.emplace(info.constant, nw::Feat::make(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid feat ({}) with invalid constant", i);
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'feat.2da'");
    }

    // Races
    auto& race_array = nw::kernel::rules().races;
    if (racialtypes.is_valid()) {
        race_array.entries.reserve(racialtypes.rows());
        for (size_t i = 0; i < racialtypes.rows(); ++i) {
            const auto& info = race_array.entries.emplace_back(racialtypes.row(i));
            if (info.constant) {
                race_array.constant_to_index.emplace(info.constant, nw::Race::make(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid race ({}) with invalid constant", i);
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'racialtypes.2da'");
    }

    // Skills
    auto& skill_array = nw::kernel::rules().skills;
    if (skills.is_valid()) {
        skill_array.entries.reserve(skills.rows());
        for (size_t i = 0; i < skills.rows(); ++i) {
            const auto& info = skill_array.entries.emplace_back(skills.row(i));
            if (info.constant) {
                skill_array.constant_to_index.emplace(info.constant, nw::Skill::make(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid skill ({}) with invalid constant", i);
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'skills.2da'");
    }

    // Spell Schools
    auto& spell_school_array = nw::kernel::rules().spellschools;
    if (spellschools.is_valid()) {
        spell_school_array.entries.reserve(spellschools.rows());
        for (size_t i = 0; i < spellschools.rows(); ++i) {
            spell_school_array.entries.emplace_back(spellschools.row(i));
        }
    }

    // Spells
    auto& spell_array = nw::kernel::rules().spells;
    if (spells.is_valid()) {
        spell_array.entries.reserve(spells.rows());
        for (size_t i = 0; i < spells.rows(); ++i) {
            spell_array.entries.emplace_back(spells.row(i));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'spells.2da'");
    }

    // == Postprocess 2das ====================================================

    // Skill
    for (size_t i = 0; i < skills.rows(); ++i) {
        auto& info = skill_array.entries[i];
        if (skills.get_to(i, "KeyAbility", temp_string)) {
            if (nw::string::icmp("str", temp_string)) {
                info.ability = ability_strength;
            } else if (nw::string::icmp("dex", temp_string)) {
                info.ability = ability_dexterity;
            } else if (nw::string::icmp("con", temp_string)) {
                info.ability = ability_constitution;
            } else if (nw::string::icmp("int", temp_string)) {
                info.ability = ability_intelligence;
            } else if (nw::string::icmp("wis", temp_string)) {
                info.ability = ability_wisdom;
            } else if (nw::string::icmp("cha", temp_string)) {
                info.ability = ability_charisma;
            }
        }
    }

    return true;
}

} // namespace nwn1
