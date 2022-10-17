#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/resources/NWSync.hpp>
#include <nw/resources/ResourceType.hpp>

namespace fs = std::filesystem;
using namespace std::literals;

TEST_CASE("nwsync", "[containers]")
{
    auto path = nw::kernel::config().alias_path(nw::PathAlias::nwsync);
    auto n = nw::NWSync(path);
    REQUIRE(n.is_loaded());
    REQUIRE(n.shard_count() == 1);

    auto manifests = n.manifests();
    if (manifests.size() > 0) {
        auto manifest = n.get(manifests[0]);
        auto resource = manifest->all();
        REQUIRE(resource.size() > 0);

        REQUIRE(manifest->contains(resource[0].name));
        auto ba = manifest->demand(resource[0].name);
        REQUIRE(ba.size());
        REQUIRE(manifest->extract(std::regex(resource[0].name.filename()), "tmp/"));

        auto rd = manifest->stat(resource[0].name);
        REQUIRE(rd.mtime == 1648999682);
    }
}
