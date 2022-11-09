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
        auto eff = nwk::effects().create(nwn1::effect_haste);
        nwk::effects().destroy(eff);
    }
    auto stats = nwk::effects().stats();
    REQUIRE(stats.free_list_size == 1);
    REQUIRE(stats.pool_size == 1);
}

TEST_CASE("effect system registration", "[kernel]")
{
    auto apply = [](nw::ObjectBase* obj, const nw::Effect*) -> bool {
        if (auto cre = obj->as_creature()) {
            cre->hasted = true;
        }
        return true;
    };

    auto remove = [](nw::ObjectBase* obj, const nw::Effect*) -> bool {
        if (auto cre = obj->as_creature()) {
            cre->hasted = false;
        }
        return true;
    };

    nwk::effects().add(nwn1::effect_haste, apply, remove);
    auto eff = nwk::effects().create(nwn1::effect_haste);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj);
    REQUIRE(nwk::effects().apply(obj, eff));
    REQUIRE(obj->hasted);
    REQUIRE(obj->effects().size() == 1);
    REQUIRE(nwk::effects().remove(obj, eff));
    REQUIRE_FALSE(obj->hasted);
    REQUIRE(obj->effects().size() == 0);
}
