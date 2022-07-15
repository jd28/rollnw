#include <catch2/catch.hpp>

#include <nw/components/Creature.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/rules/Feat.hpp>
#include <nw/rules/system.hpp>
#include <nwn1/Profile.hpp>
#include <nwn1/rules.hpp>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST_CASE("modifier", "[rules]")
{
    auto ent = nw::kernel::objects().load(fs::path("../tests/test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());
    auto stats = ent.get_mut<nw::LevelStats>();
    stats->entries[0].id = nwn1::class_type_pale_master;

    auto is_pm = nw::Requirement{{nwn1::qual::class_level(nwn1::class_type_pale_master, 1)}};

    auto pm_ac = [](flecs::entity ent) -> nw::ModifierResult {
        auto stat = ent.get<nw::LevelStats>();
        if (!stat) {
            return 0;
        }
        auto pm_level = stat->level_by_class(nwn1::class_type_pale_master);
        return pm_level > 0 ? ((pm_level / 4) + 1) * 2 : 0;
    };

    auto mod2 = nwn1::mod::armor_class(nwn1::ac_natural, pm_ac, is_pm, {},
        "dnd-3.0-palemaster-ac", nw::ModifierSource::class_);

    auto res = nwk::rules().calculate<int>(ent, mod2);
    REQUIRE(res == 6);
}

TEST_CASE("modifier kernel", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());
    auto stats = ent.get_mut<nw::LevelStats>();
    stats->entries[0].id = nwn1::class_type_pale_master;

    auto res = nwk::rules().calculate<int>(ent, nwn1::mod_type_armor_class, nwn1::ac_natural);
    REQUIRE(res == 6);

    auto pm_ac_nerf = [](flecs::entity ent) -> nw::ModifierResult {
        auto stat = ent.get<nw::LevelStats>();
        if (!stat) {
            return 0;
        }
        auto pm_level = stat->level_by_class(nwn1::class_type_pale_master);
        return ((pm_level / 4) + 1);
    };

    REQUIRE(nwk::rules().replace("dnd-3.0-palemaster-ac", pm_ac_nerf));
    res = nwk::rules().calculate<int>(ent, nwn1::mod_type_armor_class, nwn1::ac_natural);
    REQUIRE(res == 3);

    REQUIRE(nwk::rules().remove("dnd-3.0-palemaster-*"));
    res = nwk::rules().calculate<int>(ent, nwn1::mod_type_armor_class, nwn1::ac_natural);
    REQUIRE(res == 0);

    nwk::unload_module();
}

TEST_CASE("modifier kernel 2", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());
    auto stats = ent.get_mut<nw::CreatureStats>();
    stats->add_feat(nwn1::feat_epic_toughness_1);
    stats->add_feat(nwn1::feat_epic_toughness_2);
    stats->add_feat(nwn1::feat_epic_toughness_3);

    auto [highest, nth] = nwn1::has_feat_successor(ent, nwn1::feat_epic_toughness_1);
    REQUIRE(highest == nwn1::feat_epic_toughness_4);
    REQUIRE(nth == 4);

    auto res = nwk::rules().calculate<int>(ent, nwn1::mod_type_hitpoints);
    REQUIRE(res == 80);

    nwk::unload_module();
}
