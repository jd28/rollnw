#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ParsedScriptCache.hpp>

namespace nwk = nw::kernel;

TEST_CASE("parsed script cache", "[kernel]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto s1 = nwk::parsed_scripts().get("nwscript");
    REQUIRE(s1);
    auto s2 = nwk::parsed_scripts().get("nwscript");
    REQUIRE(s1 == s2);
    auto s3 = nwk::parsed_scripts().get("dontexist");
    REQUIRE(!s3);

    nwk::unload_module();
}
