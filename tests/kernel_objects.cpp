#include <catch2/catch_all.hpp>

#include <nw/components/Creature.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/log.hpp>
#include <nwn1/Profile.hpp>

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("objects manager", "[kernel]")
{
    auto ent = nw::kernel::objects().load<nw::Creature>("nw_chicken"sv);

    REQUIRE(ent);
    REQUIRE(ent->common.resref == "nw_chicken");
    REQUIRE(ent->stats.get_ability_score(nwn1::ability_dexterity) == 7);
    REQUIRE(ent->scripts.on_attacked == "nw_c2_default5");
    REQUIRE(ent->appearance.id == 31);
    REQUIRE(ent->gender == 1);

    auto ent2 = nw::kernel::objects().get<nw::Creature>(ent->handle());
    REQUIRE(ent == ent2);

    auto handle = ent->handle();
    nw::kernel::objects().destroy(handle);
    REQUIRE_FALSE(nw::kernel::objects().valid(handle));

    auto ent3 = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/scratch/pl_agent_001.utc.json"));

    REQUIRE(ent3);
    REQUIRE(ent3->common.resref == "pl_agent_001");
    REQUIRE(ent3->stats.get_ability_score(nwn1::ability_dexterity) == 13);
    REQUIRE(ent3->scripts.on_attacked == "mon_ai_5attacked");
    REQUIRE(ent3->appearance.id == 6);
    REQUIRE(ent3->appearance.body_parts.shin_left == 1);
    REQUIRE(ent3->soundset == 171);
    REQUIRE(std::get<nw::Resref>(ent3->equipment.equips[1]) == "dk_agent_thread2");
    REQUIRE(ent3->combat_info.ac_natural == 0);
    REQUIRE(ent3->combat_info.special_abilities.size() == 1);
    REQUIRE(ent3->combat_info.special_abilities[0].spell == 120);

    auto handle2 = ent3->handle();

    REQUIRE(handle.id == handle2.id);
}
