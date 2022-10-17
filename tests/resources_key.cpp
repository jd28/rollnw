#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/resources/Key.hpp>

#include <filesystem>
#include <regex>

namespace fs = std::filesystem;
using namespace std::literals;

TEST_CASE("Load Key", "[resources]")
{
    auto install_path = nw::kernel::config().options().install;
    if (fs::exists(install_path / "data/nwn_base.key")) {
        nw::Key k{install_path / "data/nwn_base.key"};
        REQUIRE(k.size() > 0);
        REQUIRE(k.all().size() > 0);

        nw::ByteArray ba = k.demand({"nwscript"sv, nw::ResourceType::nss});
        REQUIRE(ba.size());
        REQUIRE(k.extract(std::regex("nwscript\\.nss"), "tmp/") == 1);
    }
}
