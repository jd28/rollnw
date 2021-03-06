#include <catch2/catch.hpp>

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
    auto manifests = n.manifests();
    if (manifests.size() > 0) {
        auto manifest = n.get(manifests[0]);
        auto resource = manifest->all();
        REQUIRE(resource.size() > 0);

        auto ba = manifest->demand(resource[0].name);
        REQUIRE(ba.size());
        REQUIRE(manifest->extract(std::regex(resource[0].name.filename()), "tmp/"));
    }
}
