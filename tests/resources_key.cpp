#include <catch2/catch.hpp>

#include "nw/log.hpp"
#include "nw/resources/Key.hpp"
#include "nw/util/platform.hpp"

#include <filesystem>
#include <regex>

namespace fs = std::filesystem;
using namespace std::literals;

TEST_CASE("Load Key", "[resources]")
{
    auto paths = nw::get_nwn_paths();
    if (fs::exists(paths.install)) {
        nw::Key k{paths.install / "data/nwn_base.key"};
        REQUIRE(k.size() > 0);
        REQUIRE(k.all().size() > 0);

    nw::ByteArray ba = k.demand({"nwscript"sv, nw::ResourceType::nss});
    REQUIRE(ba.size());
    REQUIRE(k.extract(std::regex("nwscript\\.nss"), "tmp/") == 1);
    }
}
