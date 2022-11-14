#include "nwn1/functions/funcs_effects.hpp"
#include <catch2/catch_all.hpp>

#include <nw/components/Creature.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/log.hpp>
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
    REQUIRE(nwn1::apply_effect(obj, eff));
    REQUIRE(obj->hasted);
    REQUIRE(obj->effects().size() == 1);
    REQUIRE(nwn1::remove_effect(obj, eff));
    REQUIRE_FALSE(obj->hasted);
    REQUIRE(obj->effects().size() == 0);
}
