#include "Profile.hpp"

#include "constants.hpp"
#include "rules.hpp"
#include "scriptbridge.hpp"

#include "../../formats/Ini.hpp"
#include "../../formats/StaticTwoDA.hpp"
#include "../../kernel/Kernel.hpp"
#include "../../kernel/Rules.hpp"
#include "../../kernel/Strings.hpp"
#include "../../kernel/TwoDACache.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../resources/ResourceManager.hpp"
#include "../../rules/combat_scheduler.hpp"
#include "../../rules/feats.hpp"
#include "../../rules/system.hpp"
#include "../../smalls/runtime.hpp"
#include "../../util/profile.hpp"

#include <map>
#include <utility>

namespace nwk = nw::kernel;

using namespace std::literals;

namespace nwn1 {

namespace {

void register_twoda_config_converters()
{
    // Register 2da-based converters for smalls load_config! paths before any
    // object callbacks can force scripts to load these modules.
    using CM = nw::smalls::Runtime::TwoDAColumnMapping;
    auto& srt = nw::kernel::runtime();

    // Scan classes.2da for unique SavingThrowTable and AttackBonusTable values
    // and assign stable integer IDs (sorted alphabetically for consistency with datagen).
    nw::Vector<std::pair<nw::String, int32_t>> save_table_enum;
    nw::Vector<std::pair<nw::String, int32_t>> atk_table_enum;
    nw::Vector<std::pair<nw::String, int32_t>> spell_table_enum{
        {"bard", 0},
        {"cleric", 1},
        {"druid", 2},
        {"paladin", 3},
        {"ranger", 4},
        {"wiz_sorc", 5},
    };
    auto spell_progression_grid = [](nw::String column, nw::String field, nw::String limit_column = {}) {
        CM result;
        result.column = std::move(column);
        result.field = std::move(field);
        result.secondary_grid_column_prefix = "SpellLevel";
        result.secondary_grid_column_count = 10;
        result.secondary_grid_limit_column = std::move(limit_column);
        return result;
    };
    {
        auto* cls_tda = nw::kernel::twodas().get("classes");
        if (cls_tda) {
            std::map<std::string, int32_t> save_map, atk_map;
            for (size_t i = 0; i < cls_tda->rows(); ++i) {
                nw::String tbl;
                if (cls_tda->get_to(i, "SavingThrowTable", tbl) && !tbl.empty()) {
                    save_map.emplace(tbl, 0);
                }
                if (cls_tda->get_to(i, "AttackBonusTable", tbl) && !tbl.empty()) {
                    atk_map.emplace(tbl, 0);
                }
            }
            int32_t id = 0;
            for (auto& [name, _] : save_map) {
                save_table_enum.push_back({name, id++});
            }
            id = 0;
            for (auto& [name, _] : atk_map) {
                atk_table_enum.push_back({name, id++});
            }
        }
    }

    srt.register_twoda_converter("nwn1.data.appearance", "appearance", {
                                                                           CM{"STRING_REF", "name"},
                                                                           CM{"RACE", "model"},
                                                                           CM{"MODELTYPE", "model_type", {
                                                                                                             {"P", 0},
                                                                                                             {"S", 1},
                                                                                                             {"F", 2},
                                                                                                             {"L", 3},
                                                                                                             {"FT", 4},
                                                                                                             {"FW", 5},
                                                                                                             {"FWT", 6},
                                                                                                             {"LWT", 7},
                                                                                                             {"SWT", 8},
                                                                                                         }},
                                                                           CM{"SIZECATEGORY", "size"},
                                                                           CM{"MOVERATE", "walkrate", {
                                                                                                          {"PLAYER", 0},
                                                                                                          {"NOMOVE", 1},
                                                                                                          {"VSLOW", 2},
                                                                                                          {"SLOW", 3},
                                                                                                          {"NORM", 4},
                                                                                                          {"FAST", 5},
                                                                                                          {"VFAST", 6},
                                                                                                          {"DEFAULT", 7},
                                                                                                          {"DFAST", 8},
                                                                                                      }},
                                                                           CM{"WING_TAIL_SCALE", "wing_tail_scale", {}, {}, 0, 1.0f},
                                                                           CM{"HELMET_SCALE_M", "helmet_scale_m", {}, {}, 0, 1.0f},
                                                                           CM{"HELMET_SCALE_F", "helmet_scale_f", {}, {}, 0, 1.0f},
                                                                           CM{"WEAPONSCALE", "weapon_scale", {}, {}, 0, -1.0f},
                                                                           CM{"HASARMS", "has_arms", {}, {}, 0, 0.0f, true},
                                                                       });
    srt.register_twoda_converter("nwn1.data.parts_robe", "parts_robe", {
                                                                           CM{"HIDEBELT", "hide_belt"},
                                                                           CM{"HIDEBICEPL", "hide_bicep_left"},
                                                                           CM{"HIDEBICEPR", "hide_bicep_right"},
                                                                           CM{"HIDEFOOTL", "hide_foot_left"},
                                                                           CM{"HIDEFOOTR", "hide_foot_right"},
                                                                           CM{"HIDEFOREL", "hide_forearm_left"},
                                                                           CM{"HIDEFORER", "hide_forearm_right"},
                                                                           CM{"HIDEHANDL", "hide_hand_left"},
                                                                           CM{"HIDEHANDR", "hide_hand_right"},
                                                                           CM{"HIDEHEAD", "hide_head"},
                                                                           CM{"HIDENECK", "hide_neck"},
                                                                           CM{"HIDEPELVIS", "hide_pelvis"},
                                                                           CM{"HIDESHINL", "hide_shin_left"},
                                                                           CM{"HIDESHINR", "hide_shin_right"},
                                                                           CM{"HIDESHOL", "hide_shoulder_left"},
                                                                           CM{"HIDESHOR", "hide_shoulder_right"},
                                                                           CM{"HIDELEGL", "hide_thigh_left"},
                                                                           CM{"HIDELEGR", "hide_thigh_right"},
                                                                           CM{"HIDECHEST", "hide_torso"},
                                                                       });
    srt.register_twoda_converter("nwn1.data.wingmodel", "wingmodel", {
                                                                         CM{"MODEL", "model"},
                                                                     });
    srt.register_twoda_converter("nwn1.data.tailmodel", "tailmodel", {
                                                                         CM{"MODEL", "model"},
                                                                     });
    srt.register_twoda_converter("nwn1.data.creaturesize", "creaturesize", {
                                                                               CM{"ACATTACKMOD", "ac_attack_mod"},
                                                                           });
    srt.register_twoda_converter("nwn1.data.creaturespeed", "creaturespeed", {
                                                                                 CM{"WALKRATE", "walkrate"},
                                                                             });
    srt.register_twoda_converter("nwn1.data.fractionalcr", "fractionalcr", {
                                                                               CM{"Min", "min"},
                                                                               CM{"Denominator", "denominator"},
                                                                           });
    srt.register_twoda_converter("nwn1.data.classes", "classes", {
                                                                     CM{"Name", "name"},
                                                                     CM{"HitDie", "hit_die"},
                                                                     CM{"SavingThrowTable", "saves_table", save_table_enum},
                                                                     CM{"AttackBonusTable", "attack_table", atk_table_enum},
                                                                     CM{"SpellCaster", "spellcaster"},
                                                                     CM{"MemorizesSpells", "memorizes_spells"},
                                                                     CM{"SpellbookRestricted", "spellbook_restricted"},
                                                                     CM{"SpellTableColumn", "spell_table", spell_table_enum},
                                                                     spell_progression_grid("SpellGainTable", "spells_gained", "NumSpellLevels"),
                                                                     spell_progression_grid("SpellKnownTable", "spells_known"),
                                                                     CM{"Arcane", "arcane"},
                                                                     CM{"ArcSpellLvlMod", "arcane_spellgain_mod"},
                                                                     CM{"DivSpellLvlMod", "divine_spellgain_mod"},
                                                                     CM{"SpellcastingAbil", "caster_ability", {
                                                                                                                  {"str", 0},
                                                                                                                  {"dex", 1},
                                                                                                                  {"con", 2},
                                                                                                                  {"int", 3},
                                                                                                                  {"wis", 4},
                                                                                                                  {"cha", 5},
                                                                                                              }},
                                                                 });
    srt.register_twoda_converter("nwn1.data.spells", "spells", {
                                                                   CM{"Innate", "innate_level"},
                                                                   CM{"UserType", "user_type"},
                                                                   CM{"School", "school", {
                                                                                              {"G", 0},
                                                                                              {"A", 1},
                                                                                              {"C", 2},
                                                                                              {"D", 3},
                                                                                              {"E", 4},
                                                                                              {"V", 5},
                                                                                              {"I", 6},
                                                                                              {"N", 7},
                                                                                              {"T", 8},
                                                                                          }},
                                                                   CM{"Bard", "class_levels[0]", {}, {}, -1},
                                                                   CM{"Cleric", "class_levels[1]", {}, {}, -1},
                                                                   CM{"Druid", "class_levels[2]", {}, {}, -1},
                                                                   CM{"Paladin", "class_levels[3]", {}, {}, -1},
                                                                   CM{"Ranger", "class_levels[4]", {}, {}, -1},
                                                                   CM{"Wiz_Sorc", "class_levels[5]", {}, {}, -1},
                                                               });
    srt.register_twoda_converter("nwn1.data.metamagic", "metamagic", {
                                                                         CM{"Name", "name"},
                                                                         CM{"LevelAdjustment", "level_adjustment"},
                                                                         CM{"FeatRequired", "feat", {}, {}, -1},
                                                                     });
    srt.register_twoda_converter("nwn1.data.feats", "feat", {
                                                                CM{"FEAT", "name"},
                                                                CM{"MINLEVELCLASS", "min_level"},
                                                                CM{"MaxLevel", "max_level", {}, {}, -1},
                                                                CM{"MINSTR", "min_str"},
                                                                CM{"MINDEX", "min_dex"},
                                                                CM{"MINCON", "min_con"},
                                                                CM{"MININT", "min_int"},
                                                                CM{"MINWIS", "min_wis"},
                                                                CM{"MINCHA", "min_cha"},
                                                                CM{"PREREQFEAT1", "prereq_feat1", {}, {}, -1},
                                                                CM{"PREREQFEAT2", "prereq_feat2", {}, {}, -1},
                                                                CM{"MAXCR", "max_cr"},
                                                                CM{"CRValue", "cr_value"},
                                                                CM{"USESPERDAY", "uses"},
                                                                CM{"PreReqEpic", "epic"},
                                                                CM{"MASTERFEAT", "master", {}, {}, -1},
                                                                CM{"SUCCESSOR", "successor", {}, {}, -1},
                                                            });
    srt.register_twoda_converter("nwn1.data.skills", "skills", {
                                                                   CM{"Name", "name"},
                                                                   CM{"Untrained", "untrained"},
                                                                   CM{"KeyAbility", "ability", {
                                                                                                   {"str", 0},
                                                                                                   {"dex", 1},
                                                                                                   {"con", 2},
                                                                                                   {"int", 3},
                                                                                                   {"wis", 4},
                                                                                                   {"cha", 5},
                                                                                               }},
                                                                   CM{"ArmorCheckPenalty", "armor_check_penalty"},
                                                               });
    srt.register_twoda_converter("nwn1.data.doortypes", "doortypes", {
                                                                         CM{"Model", "model"},
                                                                     });
    srt.register_twoda_converter("nwn1.data.genericdoors", "genericdoors", {
                                                                               CM{"ModelName", "model"},
                                                                           });
    srt.register_twoda_converter("nwn1.data.placeables", "placeables", {
                                                                           CM{"ModelName", "model"},
                                                                           CM{"LightColor", "light_color", {}, {}, -1},
                                                                           CM{"LightOffsetX", "light_offset_x"},
                                                                           CM{"LightOffsetY", "light_offset_y"},
                                                                           CM{"LightOffsetZ", "light_offset_z"},
                                                                           CM{"Static", "static"},
                                                                       });
    srt.register_twoda_converter("nwn1.data.cloakmodels", "cloakmodel", {
                                                                            CM{"ICON", "icon", {}, {}, -1},
                                                                            CM{"MODEL", "model", {}, {}, -1},
                                                                        });
    srt.register_twoda_converter("nwn1.data.armor", "armor", {
                                                                 CM{"Cost", "cost"},
                                                             });
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

} // namespace

void Profile::load_custom_services()
{
    nw::kernel::runtime().add_module_path(std::filesystem::path("stdlib") / "nwn1");
}

bool Profile::load_rules() const
{
    LOG_F(INFO, "[nwn1] loading rules...");
    register_twoda_config_converters();

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

    nw::StaticTwoDA appearances{nw::kernel::resman().demand({"appearance"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA classes{nw::kernel::resman().demand({"classes"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA feat{nw::kernel::resman().demand({"feat"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA racialtypes{nw::kernel::resman().demand({"racialtypes"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA skills{nw::kernel::resman().demand({"skills"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA spells{nw::kernel::resman().demand({"spells"sv, nw::ResourceType::twoda})};
    nw::StaticTwoDA spellschools{nw::kernel::resman().demand({"spellschools"sv, nw::ResourceType::twoda})};
    nw::String temp_string;

    auto& appearance_array = nw::kernel::rules().appearances;
    if (appearances.is_valid()) {
        appearance_array.entries.reserve(appearances.rows());
        for (size_t i = 0; i < appearances.rows(); ++i) {
            appearance_array.entries.emplace_back(appearances.row(i));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'appearance.2da'");
    }

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

bool Profile::load_resources()
{
    bool include_user = nwk::config().options().include_user;
    bool include_install = nwk::config().options().include_install;
    auto version = nwk::config().version();

    // Overrides
    if (include_user && version == nw::GameVersion::vEE) {
        nwk::resman().add_override_container(nwk::config().user_path(), "development");
    }

    if (include_user) {
        nwk::resman().add_override_container(nwk::config().user_path(), "portraits",
            nw::ResourceType::texture);
    }

    if (include_install) {
        nwk::resman().add_override_container(nwk::config().install_path() / "data", "prt",
            nw::ResourceType::texture);
    }

    // Base
    if (include_user) {
        nwk::resman().add_base_container(nwk::config().user_path(), "override");
    }

    if (include_install) {
        nwk::resman().add_base_container(nwk::config().install_path() / "data", "ovr");
    }

    if (include_user) {
        nwk::resman().add_base_container(nwk::config().user_path(), "ambient",
            nw::ResourceType::sound);
        nwk::resman().add_base_container(nwk::config().user_path(), "music",
            nw::ResourceType::sound);
    }

    if (include_install) {
        nwk::resman().add_base_container(nwk::config().install_path() / "data", "amb",
            nw::ResourceType::sound);
        nwk::resman().add_base_container(nwk::config().install_path() / "data", "mus",
            nw::ResourceType::sound);
    }

    if (include_user) {
        if (std::filesystem::exists(nwk::config().user_path() / "userpatch.ini")) {
            nw::Ini user_patch{nwk::config().user_path() / "userpatch.ini"};
            if (user_patch.valid()) {
                int i = 0;
                nw::String file;
                while (user_patch.get_to(fmt::format("Patch/PatchFile{:03d}", i++), file)) {
                    if (!nwk::resman().add_base_container(nwk::config().user_path() / "patch", file)) {
                        break;
                    }
                }
            }
        }
    }

    if (include_install) {
        if (version == nw::GameVersion::vEE) {
            auto lang = nwk::strings().global_language();
            if (lang != nw::LanguageID::english) {
                auto shortcode = nw::Language::to_string(lang);
                nwk::resman().add_base_container(
                    nwk::config().install_path() / "lang" / shortcode / "data", "nwn_base_loc");
            }

            nwk::resman().add_base_container(
                nwk::config().install_path() / "data", "nwn_retail");

            nwk::resman().add_base_container(
                nwk::config().install_path() / "data", "nwn_base");

        } else {
            nwk::resman().add_base_container(nwk::config().install_path() / "texturepacks", "xp2_tex_tpa");
            nwk::resman().add_base_container(nwk::config().install_path() / "texturepacks", "xp1_tex_tpa");
            nwk::resman().add_base_container(nwk::config().install_path() / "texturepacks", "textures_tpa");
            nwk::resman().add_base_container(nwk::config().install_path() / "texturepacks", "tiles_tpa");

            nwk::resman().add_base_container(nwk::config().install_path(), "xp3patch");
            nwk::resman().add_base_container(nwk::config().install_path(), "xp3");
            nwk::resman().add_base_container(nwk::config().install_path(), "xp2patch");
            nwk::resman().add_base_container(nwk::config().install_path(), "xp2");
            nwk::resman().add_base_container(nwk::config().install_path(), "xp1patch");
            nwk::resman().add_base_container(nwk::config().install_path(), "xp1");
            nwk::resman().add_base_container(nwk::config().install_path(), "patch");
            nwk::resman().add_base_container(nwk::config().install_path(), "chitin");
        }
    }

    return true;
}

} // namespace nwn1
