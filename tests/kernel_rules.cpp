#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/util/game_install.hpp>

TEST_CASE("rules manager", "[kernel]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);
    REQUIRE(nw::kernel::rules().skill_count() > 0);

    auto& sk = nw::kernel::rules().skill(0);
    REQUIRE(sk);
    REQUIRE(sk.name == 269);
    REQUIRE(sk.constant.name == "SKILL_ANIMAL_EMPATHY");

    nw::kernel::unload_module();
}
