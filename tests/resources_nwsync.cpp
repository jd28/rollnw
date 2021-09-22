#include <catch2/catch.hpp>

#include <nw/log.hpp>
#include <nw/resources/NWSync.hpp>
#include <nw/resources/ResourceType.hpp>
#include <nw/util/platform.hpp>

namespace fs = std::filesystem;
using namespace std::literals;

TEST_CASE("nwsync", "[containers]")
{
    auto paths = nw::get_nwn_paths();
    if (fs::exists(paths.user)) {
        auto n = nw::NWSync(paths.user / "nwsync/");
        REQUIRE(n.is_loaded());
        REQUIRE(n.shard_count() > 0);
        auto manifests = n.manifests();
        REQUIRE(manifests.size() == 1);

        auto manifest = n.get(manifests[0]);
        auto resource = manifest.all();
        REQUIRE(resource.size() > 0);

        auto ba = manifest.demand(resource[0]);
        REQUIRE(ba.size());
        REQUIRE(manifest.extract(std::regex("ranges.2da"), "tmp/"));
    }
}
