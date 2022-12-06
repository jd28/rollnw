#include <catch2/catch_all.hpp>

#include <nw/functions.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/log.hpp>
#include <nw/objects/Creature.hpp>
#include <nwn1/Profile.hpp>

#include <filesystem>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST_CASE("effect system pool", "[kernel]")
{
    for (size_t i = 0; i < 100; ++i) {
        auto eff = nwk::effects().create(nwn1::effect_type_haste);
        nwk::effects().destroy(eff);
    }
    auto stats = nwk::effects().stats();
    REQUIRE(stats.free_list_size == 1);
    REQUIRE(stats.pool_size == 1);
}

TEST_CASE("effect system registration", "[kernel]")
{
    auto eff = nwk::effects().create(nwn1::effect_type_haste);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj);
    REQUIRE(nw::apply_effect(obj, eff));
    REQUIRE(obj->hasted);
    REQUIRE(obj->effects().size() == 1);
    REQUIRE(nw::remove_effect(obj, eff));
    REQUIRE_FALSE(obj->hasted);
    REQUIRE(obj->effects().size() == 0);
}

TEST_CASE("rules system: item property cost/param tables", "[kernel]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    REQUIRE(nwk::effects().ip_cost_table(4));
    REQUIRE(nwk::effects().ip_param_table(3));
    REQUIRE(nwk::effects().ip_definition(nwn1::ip_ability_bonus)->name == 649);

    nw::kernel::unload_module();
}
